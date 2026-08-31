// SynthCard cost benchmark.
//
// A desktop x86 is not an LX7, so the absolute microseconds mean nothing. What
// the numbers are for is the RATIO between engines and lanes, and the change in
// that ratio between commits. If FM suddenly costs three times what ANALOG
// costs, something in FM got expensive, and that shows up here long before it
// shows up as a dropout on the device.
//
// The "budget" column scales the host measurement so ANALOG - the engine the
// firmware was tuned around - reads as its measured share on hardware, giving
// a rough per-voice cost in real-time terms.
#include "audio/voice.h"
#include "audio/drums.h"
#include "audio/effects.h"
#include "wav.h"

#include <chrono>
#include <cstdio>

using namespace synth;

static double secondsFor(int blocks) { return (double)blocks * kBlockSize / kSampleRate; }

template <typename F>
static double timeIt(int blocks, F&& body) {
    // One warm pass so the caches and branch predictors are not part of the
    // measurement.
    body();
    const auto t0 = std::chrono::steady_clock::now();
    body();
    const auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(t1 - t0).count() / secondsFor(blocks);
}

static double benchEngine(uint8_t eng, int blocks) {
    Patch pt;
    pt.reset();
    pt.setEngine(eng);
    pt.set(P_AMP_S, 127);
    pt.set(P_AMP_A, 0);
    return timeIt(blocks, [&] {
        Voice v;
        v.init(0x2468);
        v.setPatch(&pt);
        v.noteOn(60, 110, false);
        float b[kBlockSize];
        for (int i = 0; i < blocks; ++i) {
            memset(b, 0, sizeof(b));
            v.render(b, kBlockSize);
        }
    });
}

static double benchUnison(uint8_t width, int blocks) {
    Patch pt;
    pt.reset();
    pt.set(P_AMP_S, 127);
    pt.set(PA_UNISON, width - 1);
    return timeIt(blocks, [&] {
        Voice v;
        v.init(0x2468);
        v.setPatch(&pt);
        v.noteOn(60, 110, false);
        float b[kBlockSize];
        for (int i = 0; i < blocks; ++i) {
            memset(b, 0, sizeof(b));
            v.render(b, kBlockSize);
        }
    });
}

static double benchLane(uint8_t lane, int blocks) {
    DrumKit kit;
    loadKit(kit, 0);
    return timeIt(blocks, [&] {
        DrumEngine d;
        d.init();
        d.setKit(&kit);
        float b[kBlockSize], sd[kBlockSize], sr[kBlockSize];
        for (int i = 0; i < blocks; ++i) {
            // Retrigger often enough that the lane never falls silent and
            // stops being measured.
            if (i % 40 == 0) d.trigger(lane, 110);
            memset(b, 0, sizeof(b)); memset(sd, 0, sizeof(sd)); memset(sr, 0, sizeof(sr));
            d.render(b, sd, sr, kBlockSize);
        }
    });
}

int main() {
    const int blocks = 4000;                 // 16 s of audio per measurement
    printf("SynthCard cost benchmark (host, %d blocks = %.0f s of audio each)\n",
           blocks, secondsFor(blocks));
    printf("Ratios matter; absolute numbers do not - this is not an LX7.\n");

    const double base = benchEngine(ENG_ANALOG, blocks);

    printf("\n=== ENGINES (one voice) ===\n");
    printf("%-9s %10s %8s\n", "engine", "x realtime", "vs ANALOG");
    for (uint8_t e = 0; e < ENG_COUNT; ++e) {
        const double c = benchEngine(e, blocks);
        printf("%-9s %9.4f%% %7.2fx\n", kEngineNames[e], c * 100.0, c / base);
    }

    printf("\n=== ANALOG UNISON (charged as voice slots) ===\n");
    printf("%-9s %10s %8s\n", "width", "x realtime", "vs 1x");
    const double uni1 = benchUnison(1, blocks);
    for (uint8_t w = 1; w <= kMaxUnison; ++w) {
        const double c = benchUnison(w, blocks);
        printf("%-9d %9.4f%% %7.2fx\n", w, c * 100.0, c / uni1);
    }

    printf("\n=== DRUM LANES (one lane sounding) ===\n");
    printf("%-9s %10s %8s\n", "lane", "x realtime", "vs KICK");
    const double kick = benchLane(DL_KICK, blocks);
    for (uint8_t l = 0; l < DL_COUNT; ++l) {
        const double c = benchLane(l, blocks);
        printf("%-9s %9.4f%% %7.2fx\n", kDrumShort[l], c * 100.0, c / kick);
    }

    printf("\n=== WORST CASE (8 voices + 12 lanes + full effects) ===\n");
    {
        Patch pt;
        pt.reset();
        pt.set(P_AMP_S, 127);
        DrumKit kit;
        loadKit(kit, 4);                      // INDUSTRIAL: the busiest kit
        FxSettings fx;
        fx.reset();
        fx.set(FX_DLY_MIX, 90);
        fx.set(FX_REV_MIX, 90);
        fx.set(FX_CHO_MIX, 90);
        fx.set(FX_DRIVE, 90);

        const double c = timeIt(blocks, [&] {
            Voice v[8];
            DrumEngine d;
            Effects e;
            d.init(); d.setKit(&kit);
            e.init(); e.applySettings(fx);
            for (int i = 0; i < 8; ++i) {
                v[i].init(0x51u + i * 733u);
                v[i].setPatch(&pt);
                v[i].noteOn((uint8_t)(40 + i * 5), 110, false);
            }
            float mix[kBlockSize], sd[kBlockSize], sr[kBlockSize];
            for (int b = 0; b < blocks; ++b) {
                memset(mix, 0, sizeof(mix)); memset(sd, 0, sizeof(sd)); memset(sr, 0, sizeof(sr));
                if (b % 30 == 0) for (uint8_t l = 0; l < DL_COUNT; ++l) d.trigger(l, 110);
                for (int i = 0; i < 8; ++i) v[i].render(mix, kBlockSize);
                d.render(mix, sd, sr, kBlockSize);
                e.process(mix, sd, sr, kBlockSize);
            }
        });
        printf("  %.4f%% of realtime on this host, %.1fx one ANALOG voice\n", c * 100.0, c / base);
    }
    return 0;
}
