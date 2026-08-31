// SynthCard host renderer.
//
// Renders the drum kits, the synth engines and a short sequenced demo to
// out/*.wav, and prints the measurements that say whether each sound is doing
// what it claims. The numbers can be checked here; the files are for listening
// to somewhere with speakers, because "does the kick slap" is not a property
// any assertion can decide.
#include "audio/drums.h"
#include "audio/voice.h"
#include "audio/effects.h"
#include "sequencer/sequencer.h"
#include "music/music.h"
#include "wav.h"

#include <sys/stat.h>
#include <algorithm>
#include <utility>

using namespace synth;
static const char* kOutDir = "out";

// ---------------------------------------------------------------- helpers --
static std::vector<float> renderDrum(DrumKit& kit, uint8_t lane, uint8_t vel, float seconds) {
    DrumEngine d;
    d.init();
    d.setKit(&kit);
    d.trigger(lane, vel);
    const int total = (int)(seconds * kSampleRate);
    std::vector<float> out;
    out.reserve(total);
    float buf[kBlockSize], sd[kBlockSize], sr[kBlockSize];
    for (int done = 0; done < total; done += kBlockSize) {
        memset(buf, 0, sizeof(buf));
        memset(sd, 0, sizeof(sd));
        memset(sr, 0, sizeof(sr));
        d.render(buf, sd, sr, kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) out.push_back(buf[i]);
    }
    return out;
}

static std::vector<float> renderVoice(const Patch& pt, uint8_t note, uint8_t vel,
                                      float holdSec, float tailSec) {
    Voice v;
    v.init(0x1234);
    v.setPatch(&pt);
    v.noteOn(note, vel, false);
    const int hold = (int)(holdSec * kSampleRate);
    const int tail = (int)(tailSec * kSampleRate);
    std::vector<float> out;
    out.reserve(hold + tail);
    float buf[kBlockSize];
    for (int done = 0; done < hold + tail; done += kBlockSize) {
        if (done >= hold && done - kBlockSize < hold) v.noteOff();
        memset(buf, 0, sizeof(buf));
        v.render(buf, kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) out.push_back(buf[i]);
    }
    return out;
}

static void append(std::vector<float>& dst, const std::vector<float>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}
static void silence(std::vector<float>& dst, float sec) {
    dst.insert(dst.end(), (size_t)(sec * kSampleRate), 0.0f);
}

// ------------------------------------------------------------------ drums --
static void renderKits() {
    printf("\n=== DRUM KITS ===\n");
    printf("%-11s %-8s %6s %8s %9s %8s %7s\n",
           "kit", "lane", "peak", "peak@ms", "decay60", "centroid", ">200Hz");

    for (uint8_t k = 0; k < kKitCount; ++k) {
        DrumKit kit;
        loadKit(kit, k);
        std::vector<float> all;
        for (uint8_t lane = 0; lane < DL_COUNT; ++lane) {
            auto hit = renderDrum(kit, lane, 110, 2.0f);
            append(all, hit);
            silence(all, 0.06f);

            // Only report the detail table for the first kit; twelve kits by
            // twelve lanes is a wall of numbers nobody reads.
            if (k == 0) {
                auto sp = wav::spectrum(hit, 0, 8192, kSampleRate);
                printf("%-11s %-8s %6.3f %8.2f %9.1f %8.0f %6.0f%%\n",
                       kit.name, kDrumShort[lane], wav::peak(hit),
                       wav::peakTimeMs(hit, kSampleRate), wav::decay60Ms(hit, kSampleRate),
                       wav::centroidHz(sp), wav::energyAbove(sp, 200.0f) * 100.0f);
            }
        }
        char path[128];
        snprintf(path, sizeof(path), "%s/kit-%02d-%s.wav", kOutDir, k, kit.name);
        wav::write(path, all, (int)kSampleRate);
    }
}

// The one measurement that is specific to this device: how much of the kick
// survives a transducer with nothing below ~200 Hz.
static void reportKickAudibility() {
    printf("\n=== KICK THROUGH A 200 Hz HIGH-PASS (what the Cardputer speaker can move) ===\n");
    printf("  peak level surviving the high-pass, relative to the full-band peak\n");
    for (uint8_t k = 0; k < kKitCount; ++k) {
        DrumKit kit;
        loadKit(kit, k);
        auto hit = renderDrum(kit, DL_KICK, 120, 1.5f);
        const float db = wav::highPassPeakDb(hit, 200.0f);
        printf("  %-11s %6.1f dB  %s\n", kit.name, db,
               db > -9.0f ? "strong" : (db > -18.0f ? "audible" : "TOO QUIET TO HEAR"));
    }
}

static void renderChoke() {
    // A closed hat must silence an open one. Rendered so the behaviour can be
    // heard as well as asserted.
    DrumKit kit;
    loadKit(kit, 0);
    DrumEngine d;
    d.init();
    d.setKit(&kit);
    std::vector<float> out;
    float buf[kBlockSize], sd[kBlockSize], sr[kBlockSize];
    const int step = (int)(0.12f * kSampleRate);
    for (int hit = 0; hit < 8; ++hit) {
        d.trigger(hit % 2 == 0 ? DL_OHH : DL_CHH, 110);
        for (int done = 0; done < step; done += kBlockSize) {
            memset(buf, 0, sizeof(buf)); memset(sd, 0, sizeof(sd)); memset(sr, 0, sizeof(sr));
            d.render(buf, sd, sr, kBlockSize);
            for (int i = 0; i < kBlockSize; ++i) out.push_back(buf[i]);
        }
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/choke-hats.wav", kOutDir);
    wav::write(path, out, (int)kSampleRate);
}

// ---------------------------------------------------------------- engines --
static void renderEngines() {
    printf("\n=== ENGINES ===\n");
    printf("%-9s %-14s %6s %8s %8s %7s %7s\n",
           "engine", "preset", "peak", "dc", "centroid", "alias84", "alias96");
    printf("  alias = energy away from a harmonic. PLUCK's rises at the top of the\n"
           "  keyboard because a 15-sample string loop disperses in the tuning\n"
           "  allpass - real string inharmonicity, not folded-back aliasing.\n");

    for (uint8_t eng = 0; eng < ENG_COUNT; ++eng) {
        // The first preset that uses this engine, so each file is a real
        // sound rather than an INIT patch.
        int found = -1;
        for (uint16_t i = 0; i < kPresetCount; ++i) {
            Patch p; loadPreset(p, i);
            if (p.engine() == eng) { found = i; break; }
        }
        if (found < 0) continue;
        Patch pt; loadPreset(pt, (uint16_t)found);

        std::vector<float> all;
        // A chord, so the voice is heard the way it will actually be used.
        for (uint8_t n : {48, 60, 64, 67, 72}) {
            append(all, renderVoice(pt, n, 100, 0.45f, 0.55f));
        }
        // A filter sweep across the range, to hear the filter and catch any
        // instability at the extremes.
        Patch sweep = pt;
        for (int c = 10; c <= 120; c += 10) {
            sweep.set(P_CUTOFF, c);
            append(all, renderVoice(sweep, 55, 110, 0.16f, 0.06f));
        }

        // Aliasing is measured high up the keyboard, where a naive oscillator
        // folds harmonics back down into the audible band - but on a bare
        // patch, not on the preset. A preset's LFO sweeping the wave shape
        // puts sidebands around every harmonic, and those are indistinguishable
        // from aliasing to any "energy away from a harmonic" measure. Isolating
        // the oscillator is the only way this number means what it says.
        Patch high;
        high.reset();
        high.setEngine(eng);
        high.set(P_FIL_TYPE, 0);
        high.set(P_LFO_AMT, 0);
        high.set(P_AMP_S, 127);
        high.set(P_AMP_A, 0);
        // ORGAN's lowest drawbar is the 16' footage, an octave BELOW the note,
        // so its harmonic grid starts at f0/2. Measuring against f0 would call
        // every even-footage drawbar an alias.
        const float sub = (eng == ENG_ORGAN) ? 0.5f : 1.0f;
        auto measureAlias = [&](uint8_t note) {
            auto x = renderVoice(high, note, 110, 0.5f, 0.1f);
            auto s2 = wav::spectrum(x, 2048, 8192, kSampleRate);
            const float f = 440.0f * powf(2.0f, (note - 69) / 12.0f) * sub;
            return std::make_pair(wav::aliasEnergy(s2, f) * 100.0f, wav::centroidHz(s2));
        };
        const auto a84 = measureAlias(84);
        const auto a96 = measureAlias(96);

        char path[128];
        snprintf(path, sizeof(path), "%s/engine-%d-%s.wav", kOutDir, eng, kEngineNames[eng]);
        wav::write(path, all, (int)kSampleRate);

        printf("%-9s %-14s %6.3f %8.4f %8.0f %6.0f%% %6.0f%%\n",
               kEngineNames[eng], pt.name, wav::peak(all), wav::dcOffset(all),
               a84.second, a84.first, a96.first);
    }
}

static void renderPresets() {
    // Every preset, back to back, so a dud is easy to find by ear.
    std::vector<float> all;
    for (uint16_t i = 0; i < kPresetCount; ++i) {
        Patch pt; loadPreset(pt, i);
        append(all, renderVoice(pt, 60, 100, 0.35f, 0.35f));
        silence(all, 0.05f);
    }
    char path[128];
    snprintf(path, sizeof(path), "%s/all-presets.wav", kOutDir);
    wav::write(path, all, (int)kSampleRate);
    printf("\n  all-presets.wav: %d presets, %.1f s\n",
           (int)kPresetCount, all.size() / kSampleRate);
}

// ------------------------------------------------------------------- demo --
// Drives the real sequencer through the real voices, so the demo exercises
// the same timing path the firmware uses.
class DemoSink : public SeqSink {
public:
    DemoSink(Project& p) : proj_(p) {
        for (int i = 0; i < kVoices; ++i) { v_[i].init(0x9E37u + i * 977u); track_[i] = 0; note_[i] = 0; }
        drums_.init();
        drums_.setKit(&proj_.kit);
        fx_.init();
    }
    void seqNoteOn(uint8_t track, uint8_t note, uint8_t vel, bool slide) override {
        int slot = -1;
        for (int i = 0; i < kVoices; ++i) if (!v_[i].active()) { slot = i; break; }
        if (slot < 0) slot = 0;
        v_[slot].setPatch(&proj_.patch[track % kMelTracks]);
        v_[slot].noteOn(note, vel, slide);
        track_[slot] = track; note_[slot] = note;
    }
    void seqNoteOff(uint8_t track, uint8_t note) override {
        for (int i = 0; i < kVoices; ++i)
            if (v_[i].active() && track_[i] == track && note_[i] == note) v_[i].noteOff();
    }
    void seqDrum(uint8_t lane, uint8_t vel) override { drums_.trigger(lane, vel); }
    void seqClick(bool) override {}

    void run(Sequencer& seq, std::vector<float>& out, float seconds) {
        fx_.applySettings(proj_.fx);
        const int total = (int)(seconds * kSampleRate);
        float mel[kMelTracks][kBlockSize], drum[kBlockSize];
        float mix[kBlockSize], sd[kBlockSize], sr[kBlockSize];
        for (int done = 0; done < total; done += kBlockSize) {
            for (int t = 0; t < kMelTracks; ++t) memset(mel[t], 0, sizeof(mel[t]));
            memset(drum, 0, sizeof(drum));
            memset(sd, 0, sizeof(sd));
            memset(sr, 0, sizeof(sr));

            int off = 0;
            while (off < kBlockSize) {
                int seg = std::min(seq.samplesUntilNext(), kBlockSize - off);
                if (seg < 1) seg = 1;
                for (int i = 0; i < kVoices; ++i) {
                    if (!v_[i].active()) continue;
                    v_[i].render(mel[track_[i] % kMelTracks] + off, seg);
                }
                drums_.render(drum + off, sd + off, sr + off, seg);
                seq.advance(seg);
                off += seg;
            }
            const float d0 = proj_.patch[0].norm(P_SEND_DLY), r0 = proj_.patch[0].norm(P_SEND_REV);
            const float d1 = proj_.patch[1].norm(P_SEND_DLY), r1 = proj_.patch[1].norm(P_SEND_REV);
            for (int i = 0; i < kBlockSize; ++i) {
                mix[i] = mel[0][i] + mel[1][i] + drum[i];
                sd[i] += mel[0][i] * d0 + mel[1][i] * d1;
                sr[i] += mel[0][i] * r0 + mel[1][i] * r1;
            }
            fx_.process(mix, sd, sr, kBlockSize);
            for (int i = 0; i < kBlockSize; ++i) out.push_back(mix[i]);
        }
    }

private:
    static constexpr int kVoices = 8;
    Project& proj_;
    Voice    v_[kVoices];
    uint8_t  track_[kVoices], note_[kVoices];
    DrumEngine drums_;
    Effects    fx_;
};

static void renderDemo() {
    Project proj;
    proj.reset();
    proj.bpm = 124;
    loadKit(proj.kit, 1);                 // 909
    loadPreset(proj.patch[0], 3);         // ACID LEAD
    loadPreset(proj.patch[1], 9);         // ACID BASS
    proj.fx.set(FX_DLY_MIX, 40);
    proj.fx.set(FX_REV_MIX, 46);

    Rng rng; rng.s = 20260831u;
    Pattern& p = proj.pat[0];
    p.length = 16;
    randomDrums(p, rng, 70);
    randomBass(p, 1, rng, proj.root, proj.scale, 3);
    randomMelody(p, 0, rng, proj.root, proj.scale, 5);

    DemoSink sink(proj);
    Sequencer seq;
    seq.init(&proj, &sink);
    seq.refreshTiming();
    seq.play();

    std::vector<float> out;
    sink.run(seq, out, 16.0f);
    char path[128];
    snprintf(path, sizeof(path), "%s/demo-song.wav", kOutDir);
    wav::write(path, out, (int)kSampleRate);
    printf("\n  demo-song.wav: 16 s, peak %.3f, dc %.5f\n",
           wav::peak(out), wav::dcOffset(out));
}

// The reverb's impulse response, so the tail can be heard and its density
// measured. A four-comb Schroeder produces a visibly periodic echo train; a
// diffused plate should not.
static void renderReverb() {
    printf("\n=== REVERB ===\n");
    for (int size : {30, 70, 120}) {
        FxSettings fx;
        fx.reset();
        fx.set(FX_REV_SIZE, size);
        fx.set(FX_REV_MIX, 127);
        fx.set(FX_REV_DAMP, 40);
        fx.set(FX_DLY_MIX, 0);
        fx.set(FX_COMP, 0);
        Effects e;
        e.init();
        e.applySettings(fx, 120, OUT_LINE);

        std::vector<float> out;
        float mix[kBlockSize], sd[kBlockSize], sr[kBlockSize];
        for (int b = 0; b < 1200; ++b) {                // 4.8 s
            memset(mix, 0, sizeof(mix));
            memset(sd, 0, sizeof(sd));
            memset(sr, 0, sizeof(sr));
            if (b == 0) { sr[0] = 1.0f; }               // one impulse into the send
            e.process(mix, sd, sr, kBlockSize);
            for (int i = 0; i < kBlockSize; ++i) out.push_back(mix[i]);
        }
        // Echo density: how many times the tail crosses zero per second. A
        // sparse comb network gives a low number and an audibly periodic
        // tail; a diffused plate gives a high one.
        // Measured over a fixed early window, so the figure does not shrink
        // simply because a longer tail leaves more silence at the end.
        const size_t w0 = 3200, w1 = 3200 + (size_t)(0.4f * kSampleRate);
        int crossings = 0;
        for (size_t i = w0 + 1; i < w1 && i < out.size(); ++i)
            if ((out[i] >= 0.0f) != (out[i - 1] >= 0.0f)) ++crossings;
        const float secs = 0.4f;
        printf("  size %3d: peak %.3f  RT60 %.2f s  %.0f zero-crossings/s\n",
               size, wav::peak(out), wav::decay60Ms(out, kSampleRate) / 1000.0f,
               crossings / secs);
        char path[128];
        snprintf(path, sizeof(path), "%s/reverb-size%03d.wav", kOutDir, size);
        wav::write(path, out, (int)kSampleRate);
    }
}

int main() {
    mkdir(kOutDir, 0755);
    printf("SynthCard renderer -> %s/\n", kOutDir);
    renderKits();
    reportKickAudibility();
    renderChoke();
    renderReverb();
    renderEngines();
    renderPresets();
    renderDemo();
    printf("\nDone. Listen to %s/*.wav\n", kOutDir);
    return 0;
}
