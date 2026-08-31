// Host-side tests for the Arduino-free half of SynthCard: DSP, voices, drums,
// effects, sequencer timing, music theory and the project file format.
#include "audio/dsp.h"
#include "audio/patch.h"
#include "audio/voice.h"
#include "audio/drums.h"
#include "audio/effects.h"
#include "sequencer/sequencer.h"
#include "music/music.h"
#include "storage/storage.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include "wav.h"

using namespace synth;

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { ++g_run; if (!(cond)) { ++g_fail; \
    printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static bool finite_(float v) { return v == v && v > -1e6f && v < 1e6f; }

// ---------------------------------------------------------------------------
static void testDsp() {
    CHECK(fabsf(fastSin01(0.0f)) < 0.01f, "sin(0)=%f", fastSin01(0.0f));
    CHECK(fabsf(fastSin01(0.25f) - 1.0f) < 0.01f, "sin(pi/2)=%f", fastSin01(0.25f));
    CHECK(fabsf(fastSin01(0.5f)) < 0.01f, "sin(pi)=%f", fastSin01(0.5f));
    CHECK(fabsf(fastSin01(0.75f) + 1.0f) < 0.01f, "sin(3pi/2)=%f", fastSin01(0.75f));
    for (int i = 0; i < 64; ++i) {
        float p = i / 64.0f;
        float err = fabsf(fastSin01(p) - sinf(p * kTau));
        CHECK(err < 0.02f, "fastSin01 error %f at %f", err, p);
    }
    CHECK(softClip(100.0f) <= 1.0f && softClip(-100.0f) >= -1.0f, "softClip unbounded");
    CHECK(fabsf(softClip(0.0f)) < 1e-6f, "softClip(0) != 0");
    CHECK(fabsf(noteToHz(69) - 440.0f) < 0.01f, "A4 = %f", noteToHz(69));
    CHECK(fabsf(noteToHz(57) - 220.0f) < 0.01f, "A3 = %f", noteToHz(57));

    Rng r;
    int buckets[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4000; ++i) buckets[r.below(4)]++;
    for (int i = 0; i < 4; ++i) CHECK(buckets[i] > 700, "rng bucket %d = %d", i, buckets[i]);
}

// ---------------------------------------------------------------------------
static void testEnvelope() {
    Env e;
    e.configure(1.0f, 50.0f, 0.5f, 20.0f);
    e.gate(true);
    float peak = 0.0f;
    for (int i = 0; i < (int)(kSampleRate * 0.5f); ++i) peak = fmaxf(peak, e.process());
    CHECK(peak > 0.99f, "attack peak %f", peak);
    CHECK(fabsf(e.level() - 0.5f) < 0.02f, "sustain %f", e.level());
    e.gate(false);
    for (int i = 0; i < (int)(kSampleRate * 0.5f); ++i) e.process();
    CHECK(e.idle(), "envelope did not release to idle (level %f)", e.level());

    // A zero-sustain envelope must still land on exactly zero, not drift.
    Env d;
    d.configure(1.0f, 20.0f, 0.0f, 5.0f);
    d.gate(true);
    for (int i = 0; i < (int)(kSampleRate * 0.4f); ++i) d.process();
    CHECK(d.level() < 0.01f, "zero sustain leaked %f", d.level());
}

// ---------------------------------------------------------------------------
static void testFilter() {
    // A low-pass well below the test tone must attenuate it substantially.
    for (uint8_t type = 1; type <= 3; ++type) {
        SVF f;
        f.reset();
        f.setCoeffs(400.0f, 1.0f);
        float ph = 0.0f, peak = 0.0f;
        for (int i = 0; i < 4000; ++i) {
            ph = wrap01(ph + 4000.0f * kInvSampleRate);
            float y = f.process(fastSin01(ph), type);
            CHECK(finite_(y), "filter type %d blew up: %f", type, y);
            if (i > 2000) peak = fmaxf(peak, fabsf(y));
        }
        if (type == 1) CHECK(peak < 0.3f, "LPF passed 4 kHz at cutoff 400: %f", peak);
        if (type == 2) CHECK(peak > 0.5f, "HPF blocked 4 kHz at cutoff 400: %f", peak);
    }
    // High resonance at a high cutoff must stay stable.
    SVF f;
    f.reset();
    f.setCoeffs(kSampleRate * 0.44f, 12.0f);
    for (int i = 0; i < 20000; ++i) {
        float y = f.process((i % 97) * 0.02f - 1.0f, 1);
        CHECK(finite_(y), "resonant filter diverged at %d: %f", i, y);
        if (!finite_(y)) break;
    }
}

// ---------------------------------------------------------------------------
static void testPresets() {
    CHECK(kPresetCount >= 20, "only %d presets", (int)kPresetCount);
    Patch p;
    for (uint16_t i = 0; i < kPresetCount; ++i) {
        loadPreset(p, i);
        CHECK(p.name[0] != 0, "preset %d has no name", i);
        const uint8_t eng = p.engine();
        for (uint8_t id = 0; id < P_COUNT; ++id)
            CHECK(p.p[id] <= paramInfo(eng, id).max,
                  "preset %s param %s = %d > max %d", p.name, paramInfo(eng, id).name,
                  p.p[id], paramInfo(eng, id).max);
        char buf[24];
        for (uint8_t id = 0; id < P_COUNT; ++id) {
            formatParam(p, id, buf, sizeof(buf));
            CHECK(buf[0] != 0, "param %s formatted empty", paramInfo(eng, id).name);
        }
    }
    // Every page slot of every engine must reference a real parameter, and
    // every parameter an engine declares must be reachable from one of its
    // pages - otherwise a knob exists that no one can turn.
    for (uint8_t eng = 0; eng < ENG_COUNT; ++eng) {
        const ParamPage* pages = synthPages(eng);
        const uint8_t nPages = synthPageCount(eng);
        bool seen[P_COUNT] = {false};
        for (uint8_t pg = 0; pg < nPages; ++pg) {
            CHECK(pages[pg].n <= 6, "%s page %s has %d slots", kEngineNames[eng],
                  pages[pg].name, pages[pg].n);
            for (uint8_t i = 0; i < pages[pg].n; ++i) {
                CHECK(pages[pg].p[i] < P_COUNT, "%s page %s slot %d out of range",
                      kEngineNames[eng], pages[pg].name, i);
                seen[pages[pg].p[i]] = true;
            }
        }
        for (uint8_t id = 0; id < P_COUNT; ++id) {
            if (id == P_BEND) continue;
            if (paramInfo(eng, id).max == 0) continue;      // inert overlay slot
            CHECK(seen[id], "%s param %s is not on any page", kEngineNames[eng],
                  paramInfo(eng, id).name);
        }
    }
    // Switching engine must leave every parameter inside its new range.
    for (uint8_t eng = 0; eng < ENG_COUNT; ++eng) {
        Patch q; q.reset(); q.setEngine(eng);
        CHECK(q.engine() == eng, "setEngine(%d) did not stick", eng);
        for (uint8_t id = 0; id < P_COUNT; ++id)
            CHECK(q.p[id] <= paramInfo(eng, id).max, "%s default %s out of range",
                  kEngineNames[eng], paramInfo(eng, id).name);
    }
}

// ---------------------------------------------------------------------------
static void testVoice() {
    Patch p;
    float buf[kBlockSize];
    for (uint16_t i = 0; i < kPresetCount; ++i) {
        loadPreset(p, i);
        Voice v;
        v.init(1234 + i);
        v.setPatch(&p);
        v.noteOn(60, 100, false);
        float peak = 0.0f;
        bool bad = false;
        for (int b = 0; b < 40 && !bad; ++b) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, kBlockSize);
            for (int s = 0; s < kBlockSize; ++s) {
                if (!finite_(buf[s])) { bad = true; break; }
                peak = fmaxf(peak, fabsf(buf[s]));
            }
        }
        CHECK(!bad, "preset %s produced a non-finite sample", p.name);
        CHECK(peak > 0.001f, "preset %s was silent", p.name);
        CHECK(peak < 4.0f, "preset %s peaked at %f", p.name, peak);
        // ...and it must eventually go quiet after note off.
        v.noteOff();
        for (int b = 0; b < 2600 && v.active(); ++b) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, kBlockSize);
        }
        CHECK(!v.active(), "preset %s never released", p.name);
    }
}

static void testVoiceRange() {
    // The whole MIDI range must stay stable, including notes above Nyquist/2.
    Patch p;
    loadPreset(p, 0);
    float buf[kBlockSize];
    for (int note = 12; note <= 108; note += 6) {
        Voice v;
        v.init(77);
        v.setPatch(&p);
        v.noteOn((uint8_t)note, 110, false);
        for (int b = 0; b < 8; ++b) {
            memset(buf, 0, sizeof(buf));
            v.render(buf, kBlockSize);
            for (int s = 0; s < kBlockSize; ++s)
                if (!finite_(buf[s])) { CHECK(false, "note %d unstable", note); return; }
        }
    }
}

// ---------------------------------------------------------------------------
static void testDrums() {
    DrumKit kit;
    for (uint8_t k = 0; k < kKitCount; ++k) {
        loadKit(kit, k);
        DrumEngine d;
        d.init();
        d.setKit(&kit);
        for (uint8_t lane = 0; lane < DL_COUNT; ++lane) {
            d.allOff();
            d.trigger(lane, 110);
            float peak = 0.0f, buf[kBlockSize], sd[kBlockSize], sr[kBlockSize];
            for (int b = 0; b < 30; ++b) {
                memset(buf, 0, sizeof(buf));
                memset(sd, 0, sizeof(sd));
                memset(sr, 0, sizeof(sr));
                d.render(buf, sd, sr, kBlockSize);
                for (int s = 0; s < kBlockSize; ++s) {
                    CHECK(finite_(buf[s]), "kit %s lane %s non-finite", kit.name, kDrumNames[lane]);
                    peak = fmaxf(peak, fabsf(buf[s]));
                }
            }
            CHECK(peak > 0.005f, "kit %s lane %s silent (%f)", kit.name, kDrumNames[lane], peak);
            CHECK(peak < 2.0f, "kit %s lane %s too hot (%f)", kit.name, kDrumNames[lane], peak);
        }
        // Everything must decay to silence rather than hang on.
        d.allOff();
        for (uint8_t lane = 0; lane < DL_COUNT; ++lane) d.trigger(lane, 127);
        float buf[kBlockSize], sd[kBlockSize], sr[kBlockSize];
        for (int b = 0; b < 2000; ++b) {
            memset(buf, 0, sizeof(buf)); memset(sd, 0, sizeof(sd)); memset(sr, 0, sizeof(sr));
            d.render(buf, sd, sr, kBlockSize);
        }
        for (uint8_t lane = 0; lane < DL_COUNT; ++lane)
            CHECK(!d.laneActive(lane), "kit %s lane %s never decayed", kit.name, kDrumNames[lane]);
    }
}

// ---------------------------------------------------------------------------
static void testEffects() {
    static Effects fx;
    fx.init();
    FxSettings s;
    s.reset();
    s.set(FX_DLY_MIX, 100); s.set(FX_DLY_FB, 110);
    s.set(FX_REV_MIX, 100); s.set(FX_REV_SIZE, 120);
    s.set(FX_CHO_MIX, 90);  s.set(FX_DRIVE, 120);
    fx.applySettings(s);

    float dry[kBlockSize], sd[kBlockSize], sr[kBlockSize];
    float loudest = 0.0f;
    for (int b = 0; b < 600; ++b) {
        for (int i = 0; i < kBlockSize; ++i) {
            float v = fastSin01(((b * kBlockSize + i) % 200) / 200.0f) * 0.9f;
            dry[i] = v; sd[i] = v; sr[i] = v;
        }
        fx.process(dry, sd, sr, kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) {
            CHECK(finite_(dry[i]), "fx non-finite at block %d", b);
            if (!finite_(dry[i])) return;
            loudest = fmaxf(loudest, fabsf(dry[i]));
        }
    }
    CHECK(loudest <= 1.0f, "master limiter let %f through", loudest);
    CHECK(loudest > 0.1f, "fx chain silent (%f)", loudest);

    // Silence in must settle to silence out (no self-oscillation).
    for (int b = 0; b < 4000; ++b) {
        memset(dry, 0, sizeof(dry)); memset(sd, 0, sizeof(sd)); memset(sr, 0, sizeof(sr));
        fx.process(dry, sd, sr, kBlockSize);
    }
    float tail = 0.0f;
    for (int i = 0; i < kBlockSize; ++i) tail = fmaxf(tail, fabsf(dry[i]));
    CHECK(tail < 0.02f, "fx tail never settled: %f", tail);
}

// ---------------------------------------------------------------------------
namespace {
struct CountingSink : SeqSink {
    std::vector<int> onSteps, offSteps;
    int noteOns = 0, noteOffs = 0, drums = 0;
    long sample = 0;
    void seqNoteOn(uint8_t, uint8_t, uint8_t, bool) override { ++noteOns; onSteps.push_back((int)sample); }
    void seqNoteOff(uint8_t, uint8_t) override { ++noteOffs; offSteps.push_back((int)sample); }
    void seqDrum(uint8_t, uint8_t) override { ++drums; }
    void seqClick(bool accent) override { ++clicks; if (accent) ++accents; }
    int clicks = 0, accents = 0;
};

// Runs the clock for `samples`, splitting at every event boundary the way the
// audio engine does.
static void runSeq(Sequencer& seq, CountingSink& sink, long samples) {
    long target = sink.sample + samples;
    while (sink.sample < target) {
        int seg = seq.samplesUntilNext();
        if (seg > 64) seg = 64;
        if (seg > (int)(target - sink.sample)) seg = (int)(target - sink.sample);
        if (seg < 1) seg = 1;
        seq.advance(seg);
        sink.sample += seg;
    }
}
}  // namespace

static void testSequencerTiming() {
    Project proj;
    proj.reset();
    proj.bpm = 120;
    proj.swing = 0;
    Pattern& p = proj.pat[0];
    p.length = 16;
    for (int i = 0; i < 16; ++i) { p.mel[0][i].note = 60; p.mel[0][i].vel = 100; p.mel[0][i].setGate(8); }
    for (int i = 0; i < 16; i += 4) p.drum[DL_KICK][i] = 100;

    CountingSink sink;
    Sequencer seq;
    seq.init(&proj, &sink);
    seq.play();

    // One 16th at 120 BPM = 0.125 s = 4000 samples at 32 kHz.
    const int expectStep = 4000;
    const int total = expectStep * 16;      // exactly one bar
    while (sink.sample < total) {
        int seg = seq.samplesUntilNext();
        if (seg > 64) seg = 64;
        if (seg > total - (int)sink.sample) seg = total - (int)sink.sample;
        seq.advance(seg);
        sink.sample += seg;
    }
    CHECK(sink.noteOns == 16, "expected 16 note-ons in one bar, got %d", sink.noteOns);
    CHECK(sink.drums == 4, "expected 4 kicks in one bar, got %d", sink.drums);
    for (size_t i = 1; i < sink.onSteps.size(); ++i) {
        int delta = sink.onSteps[i] - sink.onSteps[i - 1];
        CHECK(abs(delta - expectStep) <= 64, "step %d spacing %d, expected %d", (int)i, delta, expectStep);
    }

    // Swing must lengthen the even steps and shorten the odd ones.
    proj.swing = 60;
    CountingSink sw;
    Sequencer seq2;
    seq2.init(&proj, &sw);
    seq2.play();
    while (sw.sample < total * 2) {
        int seg = seq2.samplesUntilNext();
        if (seg > 64) seg = 64;
        seq2.advance(seg);
        sw.sample += seg;
    }
    CHECK(sw.onSteps.size() >= 4, "swing produced too few notes");
    if (sw.onSteps.size() >= 4) {
        int even = sw.onSteps[1] - sw.onSteps[0];
        int odd  = sw.onSteps[2] - sw.onSteps[1];
        CHECK(even > odd + 200, "swing did not shuffle (even %d, odd %d)", even, odd);
    }
}

static void testSequencerPatternsAndSong() {
    Project proj;
    proj.reset();
    proj.bpm = 240;
    proj.pat[0].length = 4;
    proj.pat[1].length = 4;
    proj.song.length = 2;
    proj.song.slot[0] = {0, 1};
    proj.song.slot[1] = {1, 1};
    for (int i = 0; i < 4; ++i) { proj.pat[0].mel[0][i].note = 60; proj.pat[1].mel[0][i].note = 64; }

    CountingSink sink;
    Sequencer seq;
    seq.init(&proj, &sink);
    seq.setSongMode(true);
    seq.play();
    long n = 0;
    bool sawSecond = false;
    while (n < 400000) {
        int seg = seq.samplesUntilNext();
        if (seg > 64) seg = 64;
        seq.advance(seg);
        n += seg;
        if (seq.currentPattern() == 1) sawSecond = true;
    }
    CHECK(sawSecond, "song mode never advanced to the second slot");

    // Queued pattern changes must land on the pattern boundary, not instantly.
    Sequencer q;
    CountingSink qs;
    proj.reset();
    proj.pat[0].length = 4;
    q.init(&proj, &qs);
    q.play();
    q.selectPattern(3, false);
    CHECK(q.currentPattern() == 0, "queued change applied immediately");
    for (long i = 0; i < 200000; i += 64) q.advance(64);
    CHECK(q.currentPattern() == 3, "queued change never applied");
}

static void testRecording() {
    Project proj;
    proj.reset();
    proj.bpm = 120;
    proj.pat[0].length = 16;
    CountingSink sink;
    Sequencer seq;
    seq.init(&proj, &sink);
    seq.setRecording(true);
    seq.play();
    // Land in the middle of step 2 and record; it should snap to step 2 or 3.
    for (int i = 0; i < 4000 * 2 + 1200; i += 64) seq.advance(64);
    seq.recordNote(0, 67, 120);
    int found = -1;
    for (int i = 0; i < 16; ++i) if (proj.pat[0].mel[0][i].note == 67) found = i;
    CHECK(found == 2 || found == 3, "recorded note landed on step %d", found);
    CHECK(proj.pat[0].mel[0][found >= 0 ? found : 0].vel == 120, "velocity not recorded");

    seq.recordDrum(DL_SNARE, 100);
    int d = -1;
    for (int i = 0; i < 16; ++i) if (proj.pat[0].drum[DL_SNARE][i]) d = i;
    CHECK(d >= 0, "drum was not recorded");
}

// ---------------------------------------------------------------------------
// A note held across several steps must come back out that long, and holding
// a key while recording must capture the length rather than a fixed stub.
static void testGateLengths() {
    Project proj;
    proj.reset();
    proj.bpm = 120;                        // one 16th = 4000 samples at 32 kHz
    proj.pat[0].length = 8;
    for (int i = 0; i < 8; ++i) proj.pat[0].mel[0][i].note = 0;
    proj.pat[0].mel[0][0].note = 60;
    proj.pat[0].mel[0][0].vel  = 100;
    proj.pat[0].mel[0][0].setGate(16);       // 2 whole steps

    CountingSink sink;
    Sequencer seq;
    seq.init(&proj, &sink);
    seq.play();
    runSeq(seq, sink, 4000 * 6);
    CHECK(sink.noteOns == 1, "expected one note, got %d", sink.noteOns);
    CHECK(sink.offSteps.size() == 1, "expected one note off, got %d", (int)sink.offSteps.size());
    if (sink.onSteps.size() && sink.offSteps.size()) {
        int held = sink.offSteps[0] - sink.onSteps[0];
        CHECK(abs(held - 8000) <= 128, "gate 16 held %d samples, expected 8000", held);
    }
    // A fractional gate must still be a fraction of one step.
    proj.pat[0].mel[0][0].setGate(7);        // 8/16 of a step
    CountingSink s2;
    Sequencer q;
    q.init(&proj, &s2);
    q.play();
    runSeq(q, s2, 4000 * 4);
    if (s2.onSteps.size() && s2.offSteps.size()) {
        int held = s2.offSteps[0] - s2.onSteps[0];
        CHECK(abs(held - 2000) <= 128, "gate 7 held %d samples, expected 2000", held);
    }
    // Encoding round trip.
    CHECK(gateSixteenths(0) == 1, "gate 0 = %d/16", gateSixteenths(0));
    CHECK(gateSixteenths(15) == 16, "gate 15 = %d/16", gateSixteenths(15));
    CHECK(gateSixteenths(16) == 32, "gate 16 = %d/16", gateSixteenths(16));
    CHECK(gateSixteenths(kGateMax) == 17 * 16, "gate max = %d/16", gateSixteenths(kGateMax));
}

static void testRecordedLength() {
    Project proj;
    proj.reset();
    proj.bpm = 120;
    proj.pat[0].length = 16;
    CountingSink sink;
    Sequencer seq;
    seq.init(&proj, &sink);
    seq.setRecording(true);
    seq.play();

    struct Case { long heldSamples; uint8_t wantGate; const char* what; };
    const Case cases[] = {
        {1000,  3,  "quarter step"},
        {2000,  7,  "half step"},
        {4000, 15,  "one step"},
        {8000, 16,  "two steps"},
        {16000, 18, "four steps"},
    };
    for (const Case& c : cases) {
        proj.pat[0].clearTrack(0);
        runSeq(seq, sink, 4000);                  // land on a step boundary
        seq.recordNote(0, 64, 110);
        runSeq(seq, sink, c.heldSamples);
        seq.recordNoteOff(0, 64);
        int found = -1;
        for (int i = 0; i < 16; ++i) if (proj.pat[0].mel[0][i].note == 64) found = i;
        CHECK(found >= 0, "%s: nothing recorded", c.what);
        if (found < 0) continue;
        uint8_t g = proj.pat[0].mel[0][found].gate();
        CHECK(g == c.wantGate, "%s: recorded gate %d, expected %d", c.what, g, c.wantGate);
    }

    // A release for a note that was never recorded must be harmless.
    seq.recordNoteOff(0, 99);
    seq.recordNoteOff(1, 64);
}

static void testEraseStep() {
    Project proj;
    proj.reset();
    proj.bpm = 120;
    proj.pat[0].length = 4;
    for (int i = 0; i < 4; ++i) {
        proj.pat[0].mel[0][i].note = 60;
        proj.pat[0].drum[DL_KICK][i] = 100;
    }
    CountingSink sink;
    Sequencer seq;
    seq.init(&proj, &sink);
    seq.play();
    runSeq(seq, sink, 4000 + 100);              // sitting on step 1
    int s = seq.nearestStep();
    seq.eraseStep(0);
    CHECK(proj.pat[0].mel[0][s].note == 0, "melodic erase left note %d on step %d",
          proj.pat[0].mel[0][s].note, s);
    seq.eraseStep((uint8_t)(kMelTracks + DL_KICK));
    CHECK(proj.pat[0].drum[DL_KICK][s] == 0, "drum erase left step %d", s);
    // Other steps must be untouched.
    int other = (s + 2) % 4;
    CHECK(proj.pat[0].mel[0][other].note == 60, "erase hit the wrong step");
    // Erasing while stopped must do nothing (no playhead to erase under).
    seq.stop();
    seq.eraseStep(0);
    CHECK(proj.pat[0].mel[0][other].note == 60, "erase while stopped removed a note");
}

static void testMetronome() {
    Project proj;
    proj.reset();
    proj.bpm = 120;
    proj.pat[0].length = 16;
    CountingSink sink;
    Sequencer seq;
    seq.init(&proj, &sink);

    // Off by default.
    seq.play();
    runSeq(seq, sink, 4000 * 16);
    CHECK(sink.clicks == 0, "metronome ticked while off (%d)", sink.clicks);

    // On: a quarter note click, accented on the downbeat.
    CountingSink on;
    Sequencer s2;
    s2.init(&proj, &on);
    s2.setMetronome(METRO_ON);
    s2.play();
    runSeq(s2, on, 4000 * 16);
    CHECK(on.clicks == 4, "expected 4 clicks in a bar, got %d", on.clicks);
    CHECK(on.accents == 1, "expected 1 accented click, got %d", on.accents);

    // REC ONLY: silent until armed.
    CountingSink rec;
    Sequencer s3;
    s3.init(&proj, &rec);
    s3.setMetronome(METRO_REC);
    s3.play();
    runSeq(s3, rec, 4000 * 16);
    CHECK(rec.clicks == 0, "rec-only metronome ticked while not recording (%d)", rec.clicks);
    s3.setRecording(true);
    runSeq(s3, rec, 4000 * 16);
    CHECK(rec.clicks == 4, "rec-only metronome gave %d clicks once armed", rec.clicks);
}

// Pattern length is now editable, so the clock has to follow it mid-flight.
static void testPatternLength() {
    Project proj;
    proj.reset();
    proj.bpm = 240;
    proj.pat[0].length = 4;
    for (int i = 0; i < kMaxSteps; ++i) proj.pat[0].mel[0][i].note = 60;
    CountingSink sink;
    Sequencer seq;
    seq.init(&proj, &sink);
    seq.play();
    const int stepLen = 2000;                       // 240 BPM at 32 kHz
    runSeq(seq, sink, stepLen * 4);
    CHECK(sink.noteOns == 4, "4-step pattern gave %d notes in one lap", sink.noteOns);
    CHECK(seq.step() == 0, "4-step pattern did not wrap (step %d)", seq.step());

    proj.pat[0].length = 32;
    int before = sink.noteOns;
    runSeq(seq, sink, stepLen * 8);
    CHECK(sink.noteOns - before == 8, "after growing to 32 steps got %d notes in 8 steps",
          sink.noteOns - before);
    CHECK(seq.step() == 8, "expected step 8, got %d", seq.step());

    // Shrinking under the playhead must not run off the end of the array.
    proj.pat[0].length = 2;
    for (int i = 0; i < 200; ++i) {
        runSeq(seq, sink, stepLen);
        CHECK(seq.step() < 2, "step %d escaped a 2-step pattern", seq.step());
        if (seq.step() >= 2) break;
    }
}

static void testChords() {
    uint8_t out[4];
    // OFF is always exactly the note you gave it.
    for (int n = 24; n < 100; ++n) {
        int c = buildChord((uint8_t)n, CHORD_OFF, 0, 0, out);
        CHECK(c == 1 && out[0] == n, "CHORD_OFF changed note %d", n);
    }
    // Chromatic falls back to a plain major triad / dominant seventh.
    CHECK(buildChord(60, CHORD_TRIAD, 0, 0, out) == 3, "chromatic triad size");
    CHECK(out[0] == 60 && out[1] == 64 && out[2] == 67, "chromatic triad = %d %d %d",
          out[0], out[1], out[2]);
    CHECK(buildChord(60, CHORD_SEVENTH, 0, 0, out) == 4, "chromatic 7th size");
    CHECK(out[3] == 71, "chromatic 7th top = %d", out[3]);
    CHECK(buildChord(60, CHORD_POWER, 0, 0, out) == 3, "power chord size");
    CHECK(out[1] == 67 && out[2] == 72, "power chord = %d %d", out[1], out[2]);

    // In a scale, every chord tone must stay in that scale, at every root.
    for (uint8_t sc = 1; sc < kScaleCount; ++sc) {
        for (uint8_t root = 0; root < 12; ++root) {
            for (int n = 36; n < 84; ++n) {
                uint8_t base = scaleQuantize(n, root, sc);
                for (uint8_t type = CHORD_TRIAD; type <= CHORD_SEVENTH; ++type) {
                    int c = buildChord(base, type, root, sc, out);
                    CHECK(c >= 1 && c <= 4, "chord count %d", c);
                    CHECK(out[0] == base, "chord root moved");
                    for (int k = 0; k < c; ++k) {
                        int rel = ((out[k] - root) % 12 + 12) % 12;
                        bool in = false;
                        for (uint8_t i = 0; i < kScales[sc].n; ++i)
                            if (kScales[sc].iv[i] == rel) in = true;
                        CHECK(in, "scale %s root %d: chord tone %d is out of key",
                              kScales[sc].name, root, out[k]);
                        CHECK(out[k] >= out[0], "chord tone %d below the root %d", out[k], out[0]);
                        if (!in) return;
                    }
                }
            }
        }
    }
}

// A step carrying a chord must voice every tone, and release every tone.
static void testChordPlayback() {
    Project proj;
    proj.reset();
    proj.bpm = 120;
    proj.scale = 1;                       // major
    proj.root = 0;
    proj.pat[0].length = 4;
    for (int i = 0; i < 4; ++i) proj.pat[0].mel[0][i].note = 0;
    Step& st = proj.pat[0].mel[0][0];
    st.note = 60; st.vel = 100; st.setGate(7); st.setChord(CHORD_TRIAD);

    CountingSink sink;
    Sequencer seq;
    seq.init(&proj, &sink);
    seq.play();
    runSeq(seq, sink, 4000 * 4);
    CHECK(sink.noteOns == 3, "triad step gave %d note-ons, expected 3", sink.noteOns);
    CHECK(sink.noteOffs == 3, "triad step gave %d note-offs, expected 3", sink.noteOffs);

    // Every tone must be released together at the gate, or voices leak.
    if (sink.offSteps.size() == 3)
        CHECK(sink.offSteps[0] == sink.offSteps[2], "chord tones released at different times");

    // Switching the whole song to a minor key must reshape the chord, because
    // the step stores a root and a type rather than fixed pitches.
    proj.scale = 2;                       // natural minor
    CountingSink minorSink;
    Sequencer q;
    q.init(&proj, &minorSink);
    q.play();
    runSeq(q, minorSink, 4000);
    CHECK(minorSink.noteOns == 3, "minor triad gave %d note-ons", minorSink.noteOns);
    // A mono patch must collapse the chord to its root rather than voice-steal
    // its way to the top note.
    proj.scale = 1;
    proj.patch[0].set(P_VOICE_MODE, 1);          // mono
    CountingSink monoSink;
    Sequencer m;
    m.init(&proj, &monoSink);
    m.play();
    runSeq(m, monoSink, 4000);
    CHECK(monoSink.noteOns == 1, "mono patch voiced %d notes of a triad", monoSink.noteOns);
    proj.patch[0].set(P_VOICE_MODE, 0);

    uint8_t maj[4], min[4];
    buildChord(60, CHORD_TRIAD, 0, 1, maj);
    buildChord(60, CHORD_TRIAD, 0, 2, min);
    CHECK(maj[1] == 64 && min[1] == 63, "major third %d, minor third %d", maj[1], min[1]);
}

static void testScales() {
    CHECK(kScaleCount >= 8, "only %d scales", (int)kScaleCount);
    for (uint8_t sc = 1; sc < kScaleCount; ++sc) {
        for (uint8_t root = 0; root < 12; ++root) {
            for (int n = 24; n < 108; ++n) {
                uint8_t q = scaleQuantize(n, root, sc);
                int rel = ((q - root) % 12 + 12) % 12;
                bool in = false;
                for (uint8_t i = 0; i < kScales[sc].n; ++i) if (kScales[sc].iv[i] == rel) in = true;
                CHECK(in, "scale %s root %d: note %d quantised to %d (rel %d) which is out of scale",
                      kScales[sc].name, root, n, q, rel);
                CHECK(abs((int)q - n) <= 6, "quantise moved %d to %d", n, q);
                if (!in) return;
            }
        }
    }
    // Degrees must ascend and wrap octaves correctly.
    for (uint8_t sc = 0; sc < kScaleCount; ++sc) {
        uint8_t prev = 0;
        for (int d = 0; d < 20; ++d) {
            uint8_t v = scaleDegree(48, d, sc);
            CHECK(d == 0 || v > prev, "scale %s degree %d not ascending (%d after %d)",
                  kScales[sc].name, d, v, prev);
            prev = v;
        }
        CHECK(scaleDegree(48, kScales[sc].n, sc) == 60, "scale %s octave wrap = %d",
              kScales[sc].name, scaleDegree(48, kScales[sc].n, sc));
    }
    char nb[8];
    noteName(60, nb, sizeof(nb));
    CHECK(strcmp(nb, "C4") == 0, "note 60 named %s", nb);
}

static void testEuclid() {
    for (int steps = 4; steps <= 32; ++steps) {
        for (int hits = 1; hits <= steps; ++hits) {
            int n = 0;
            for (int i = 0; i < steps; ++i) if (euclid(i, steps, hits, 0)) ++n;
            CHECK(n == hits, "euclid(%d,%d) produced %d hits", steps, hits, n);
            if (n != hits) return;
        }
    }
    // Rotation preserves the hit count and actually shifts the pattern.
    int a = 0, b = 0, diff = 0;
    for (int i = 0; i < 16; ++i) {
        bool x = euclid(i, 16, 5, 0), y = euclid(i, 16, 5, 3);
        a += x; b += y;
        if (x != y) ++diff;
    }
    CHECK(a == b && a == 5, "rotation changed hit count (%d vs %d)", a, b);
    CHECK(diff > 0, "rotation did not shift the pattern");
    CHECK(euclid(0, 16, 0, 0) == false, "zero hits produced a hit");
}

namespace {
struct ArpCollector : ArpSink {
    std::vector<int> ons;
    void arpNoteOn(uint8_t n, uint8_t) override { ons.push_back(n); }
    void arpNoteOff(uint8_t) override {}
};
}  // namespace

static void testArp() {
    ArpCollector col;
    Arp arp;
    arp.init(&col);
    arp.configure(ARP_UP, 3, 1, 8, 120);
    arp.setEnabled(true);
    arp.noteOn(60, 100);
    arp.noteOn(64, 100);
    arp.noteOn(67, 100);
    for (long i = 0; i < 32000; i += 32) arp.advance(32);
    CHECK(col.ons.size() >= 6, "arp fired only %d notes", (int)col.ons.size());
    if (col.ons.size() >= 6) {
        CHECK(col.ons[0] == 60 && col.ons[1] == 64 && col.ons[2] == 67 && col.ons[3] == 60,
              "arp up order was %d %d %d %d", col.ons[0], col.ons[1], col.ons[2], col.ons[3]);
    }
    // Octave range must reach above the held notes.
    ArpCollector c2;
    Arp a2;
    a2.init(&c2);
    a2.configure(ARP_UP, 3, 2, 8, 200);
    a2.setEnabled(true);
    a2.noteOn(60, 100);
    for (long i = 0; i < 64000; i += 32) a2.advance(32);
    bool sawOctave = false;
    for (int n : c2.ons) if (n == 72) sawOctave = true;
    CHECK(sawOctave, "arp octaves=2 never played the upper octave");

    // Releasing every note must stop it.
    arp.noteOff(60); arp.noteOff(64); arp.noteOff(67);
    size_t before = col.ons.size();
    for (long i = 0; i < 32000; i += 32) arp.advance(32);
    CHECK(col.ons.size() == before, "arp kept firing after all notes were released");
}

static void testGenerators() {
    Rng rng;
    rng.s = 42;
    Project proj;
    proj.reset();
    Pattern& p = proj.pat[0];
    randomDrums(p, rng, 70);
    int hits = 0;
    for (int l = 0; l < DL_COUNT; ++l)
        for (int i = 0; i < p.length; ++i) if (p.drum[l][i]) ++hits;
    CHECK(hits > 4, "random beat only produced %d hits", hits);

    randomBass(p, 1, rng, 0, 4, 3);
    int notes = 0;
    for (int i = 0; i < p.length; ++i) {
        uint8_t n = p.mel[1][i].note;
        if (!n) continue;
        ++notes;
        int rel = n % 12;
        bool in = false;
        for (uint8_t k = 0; k < kScales[4].n; ++k) if (kScales[4].iv[k] == rel) in = true;
        CHECK(in, "random bass produced out-of-scale note %d", n);
    }
    CHECK(notes > 2, "random bass produced %d notes", notes);

    Patch pt;
    loadPreset(pt, 0);
    for (int i = 0; i < 200; ++i) {
        randomizePatch(pt, rng, 80);
        for (uint8_t id = 0; id < P_COUNT; ++id)
            CHECK(pt.p[id] <= paramInfo(pt.engine(), id).max, "randomiser blew param %s to %d",
                  paramInfo(pt.engine(), id).name, pt.p[id]);
    }
    // A randomised patch must still make sound.
    Voice v;
    v.init(9);
    v.setPatch(&pt);
    v.noteOn(60, 110, false);
    float buf[kBlockSize], peak = 0.0f;
    for (int b = 0; b < 60; ++b) {
        memset(buf, 0, sizeof(buf));
        v.render(buf, kBlockSize);
        for (int s = 0; s < kBlockSize; ++s) { CHECK(finite_(buf[s]), "random patch unstable"); peak = fmaxf(peak, fabsf(buf[s])); }
    }
    CHECK(peak > 0.0005f, "random patch was silent (%f)", peak);

    applyEuclid(p, DL_CHH, 5, 2, 100);
    int e = 0;
    for (int i = 0; i < p.length; ++i) if (p.drum[DL_CHH][i]) ++e;
    CHECK(e == 5, "applyEuclid wrote %d hits", e);
}

// ---------------------------------------------------------------------------
static void testProjectFormat() {
    static uint8_t buf[kProjectBufSize];
    Project a;
    a.reset();
    strncpy(a.name, "TESTSONG", kNameLen - 1);
    a.bpm = 137;
    a.swing = 42;
    a.scale = 5; a.root = 7; a.octave = 3;
    a.arpOn = 1; a.arpMode = 2; a.arpRate = 4; a.arpOct = 3; a.arpGate = 11;
    a.song.length = 9;
    for (int i = 0; i < 9; ++i) a.song.slot[i] = {(uint8_t)(i % kPatternCount), (uint8_t)(i + 1)};
    Rng rng; rng.s = 7;
    for (int i = 0; i < kPatternCount; ++i) {
        a.pat[i].length = (uint8_t)(8 + i * 5);
        randomDrums(a.pat[i], rng, 60);
        randomMelody(a.pat[i], 0, rng, 0, 2, 4);
        randomBass(a.pat[i], 1, rng, 0, 2, 3);
        for (int t = 0; t < kMelTracks; ++t)
            for (int st = 0; st < kMaxSteps; ++st)
                a.pat[i].mel[t][st].setChord((i + t + st) % CHORD_COUNT);
        a.pat[i].muteDrum = (uint16_t)(i * 3);
        a.pat[i].muteMel = (uint8_t)(i & 1);
    }
    loadPreset(a.patch[0], 3);
    loadPreset(a.patch[1], 8);
    loadKit(a.kit, 4);
    a.fx.set(FX_DLY_MIX, 77);
    a.fx.set(FX_REV_MIX, 51);

    int n = projectSerialize(a, buf, sizeof(buf));
    CHECK(n > 0, "serialize failed");
    // Save and load stage through the undo buffer, so the format must fit
    // inside one with room to spare. This has already been violated once by a
    // change that shrank both sides equally and left the file's own header
    // sticking out the end, so the margin is asserted rather than assumed.
    CHECK(n <= kProjectBufSize, "project needs %d bytes, staging buffer is %d",
          n, kProjectBufSize);
    CHECK(kProjectBufSize - n >= 16,
          "only %d bytes of headroom in the staging buffer", kProjectBufSize - n);
    if (n <= 0) return;
    printf("  project size: %d bytes (staging buffer %d, headroom %d)\n",
           n, kProjectBufSize, kProjectBufSize - n);

    Project b;
    b.reset();
    CHECK(projectDeserialize(b, buf, n), "deserialize rejected a good file");
    CHECK(strcmp(a.name, b.name) == 0, "name lost: %s vs %s", a.name, b.name);
    CHECK(a.bpm == b.bpm && a.swing == b.swing, "tempo lost");
    CHECK(a.scale == b.scale && a.root == b.root && a.octave == b.octave, "key lost");
    CHECK(a.arpMode == b.arpMode && a.arpGate == b.arpGate, "arp lost");
    CHECK(a.song.length == b.song.length, "song length lost");
    for (int i = 0; i < kSongSlots; ++i)
        CHECK(a.song.slot[i].pattern == b.song.slot[i].pattern &&
              a.song.slot[i].repeat == b.song.slot[i].repeat, "song slot %d lost", i);
    for (int i = 0; i < kPatternCount; ++i) {
        CHECK(a.pat[i].length == b.pat[i].length, "pattern %d length lost", i);
        CHECK(a.pat[i].muteDrum == b.pat[i].muteDrum, "pattern %d drum mutes lost", i);
        for (int t = 0; t < kMelTracks; ++t)
            for (int s = 0; s < kMaxSteps; ++s)
                CHECK(memcmp(&a.pat[i].mel[t][s], &b.pat[i].mel[t][s], sizeof(Step)) == 0,
                      "pattern %d track %d step %d lost", i, t, s);
        for (int l = 0; l < DL_COUNT; ++l)
            CHECK(memcmp(a.pat[i].drum[l], b.pat[i].drum[l], kMaxSteps) == 0,
                  "pattern %d drum lane %d lost", i, l);
    }
    for (int t = 0; t < kMelTracks; ++t) {
        CHECK(strcmp(a.patch[t].name, b.patch[t].name) == 0, "patch %d name lost", t);
        CHECK(memcmp(a.patch[t].p, b.patch[t].p, P_COUNT) == 0, "patch %d params lost", t);
    }
    CHECK(memcmp(a.kit.p, b.kit.p, sizeof(a.kit.p)) == 0, "kit lost");
    CHECK(memcmp(a.fx.p, b.fx.p, FX_COUNT) == 0, "fx lost");

    // Corruption and truncation must be rejected, never crash.
    Project c;
    buf[40] ^= 0xFF;
    CHECK(!projectDeserialize(c, buf, n), "accepted a corrupted file");
    buf[40] ^= 0xFF;
    for (int cut = 1; cut < n; cut += 137)
        CHECK(!projectDeserialize(c, buf, cut), "accepted a file truncated to %d bytes", cut);
    uint8_t junk[64];
    for (int i = 0; i < 64; ++i) junk[i] = (uint8_t)(i * 7);
    CHECK(!projectDeserialize(c, junk, 64), "accepted junk");
    CHECK(!projectDeserialize(c, buf, 4), "accepted a 4-byte file");
}

// Builds a project file in the v1 layout (before Step gained a chord byte) so
// that songs saved by the shipped firmware are proven to still load.
// A faithful v2 writer: nine drum lanes in the old order, four drum
// parameters, a kit-wide pair of sends, and a 38-entry patch in the old flat
// layout. Nothing here may be expressed in terms of the current constants, or
// the test would drift along with the format it is supposed to pin down.
static constexpr int kOldDrums      = 9;
static constexpr int kOldDrumParams = 4;
static constexpr int kOldPatchCount = 38;

// Old parameter indices, named so the assertions below read as intent.
enum : uint8_t {
    O_ENGINE = 0, O_O1_WAVE, O_O1_LEVEL, O_O2_WAVE, O_O2_LEVEL, O_O2_SEMI,
    O_O2_DETUNE, O_SUB_WAVE, O_SUB_LEVEL, O_NOISE, O_PW, O_FINE, O_GLIDE,
    O_AMP_A, O_AMP_D, O_AMP_S, O_AMP_R,
    O_FIL_TYPE, O_CUTOFF, O_RESO, O_FEG_AMT, O_KEYTRK,
    O_FEG_A, O_FEG_D, O_FEG_S, O_FEG_R,
    O_LFO_WAVE, O_LFO_RATE, O_LFO_AMT, O_LFO_DEST, O_LFO_SYNC,
    O_VOICE_MODE, O_VELO_AMT, O_DRIVE, O_LEVEL, O_SEND_DLY, O_SEND_REV, O_BEND
};

static int writeV2Blob(const Project& p, const uint8_t oldPatch[kOldPatchCount],
                       const uint8_t oldKit[kOldDrums][kOldDrumParams],
                       uint8_t sendDly, uint8_t sendRev, uint8_t* b) {
    int n = 0;
    auto u8  = [&](uint8_t v) { b[n++] = v; };
    auto u16 = [&](uint16_t v) { u8((uint8_t)(v & 0xFF)); u8((uint8_t)(v >> 8)); };
    auto u32 = [&](uint32_t v) { u16((uint16_t)(v & 0xFFFF)); u16((uint16_t)(v >> 16)); };
    auto raw = [&](const void* s2, int l) { memcpy(b + n, s2, l); n += l; };

    u32(kProjectMagic);
    u16(2);
    raw(p.name, kNameLen);
    u16(p.bpm);
    u8(p.swing); u8(p.scale); u8(p.root); u8(p.octave);
    u8(p.arpOn); u8(p.arpMode); u8(p.arpRate); u8(p.arpOct); u8(p.arpGate);
    u8(kPatternCount); u8(kMelTracks); u8(kOldDrums); u8(kMaxSteps);
    for (int i = 0; i < kPatternCount; ++i) {
        const Pattern& pa = p.pat[i];
        raw(pa.name, 9);
        u8(pa.length); u8(pa.muteMel); u16(pa.muteDrum);
        for (int t = 0; t < kMelTracks; ++t)
            for (int st = 0; st < kMaxSteps; ++st) {          // v2: 5 bytes
                u8(pa.mel[t][st].note); u8(pa.mel[t][st].vel);
                u8(pa.mel[t][st].gate()); u8(pa.mel[t][st].flags);
                u8(pa.mel[t][st].chord());
            }
        // v2 wrote nine lanes in the old order; the reader has to put them
        // back where they belong now.
        for (int l = 0; l < kOldDrums; ++l) raw(pa.drum[l], kMaxSteps);
    }
    u8(p.song.length);
    for (int i = 0; i < kSongSlots; ++i) { u8(p.song.slot[i].pattern); u8(p.song.slot[i].repeat); }
    u8(kOldPatchCount);
    for (int t = 0; t < kMelTracks; ++t) { raw(p.patch[t].name, 13); raw(oldPatch, kOldPatchCount); }
    raw(p.kit.name, 13);
    for (int l = 0; l < kOldDrums; ++l) raw(oldKit[l], kOldDrumParams);
    u8(sendDly); u8(sendRev);
    u8(FX_COUNT);
    raw(p.fx.p, FX_COUNT);

    uint32_t h = 2166136261u;                                 // FNV-1a, as shipped
    for (int i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
    u32(h);
    return n;
}

static void testV1Compatibility() {
    static uint8_t buf[kProjectBufSize];
    Project src;
    src.reset();
    strncpy(src.name, "OLDSONG", kNameLen - 1);
    src.bpm = 143;
    Rng rng; rng.s = 99;
    for (int i = 0; i < kPatternCount; ++i) {
        src.pat[i].length = (uint8_t)(4 + i);
        randomDrums(src.pat[i], rng, 60);
        randomMelody(src.pat[i], 0, rng, 0, 2, 4);
    }

    // A v2 subtractive patch with values chosen so a mis-mapped parameter
    // cannot pass by coincidence.
    uint8_t oldPatch[kOldPatchCount] = {0};
    oldPatch[O_ENGINE] = 0;              // v2 subtractive -> ANALOG
    oldPatch[O_O1_WAVE] = 2; oldPatch[O_O1_LEVEL] = 111;
    oldPatch[O_O2_WAVE] = 1; oldPatch[O_O2_LEVEL] = 83;
    oldPatch[O_O2_SEMI] = 31; oldPatch[O_O2_DETUNE] = 70;
    oldPatch[O_SUB_WAVE] = 1; oldPatch[O_SUB_LEVEL] = 44;
    oldPatch[O_NOISE] = 17; oldPatch[O_PW] = 99;
    oldPatch[O_FINE] = 70; oldPatch[O_GLIDE] = 23;
    oldPatch[O_AMP_A] = 7; oldPatch[O_AMP_D] = 61; oldPatch[O_AMP_S] = 88; oldPatch[O_AMP_R] = 45;
    oldPatch[O_FIL_TYPE] = 1; oldPatch[O_CUTOFF] = 97; oldPatch[O_RESO] = 66;
    oldPatch[O_FEG_AMT] = 90; oldPatch[O_KEYTRK] = 33;
    oldPatch[O_FEG_A] = 3; oldPatch[O_FEG_D] = 55; oldPatch[O_FEG_S] = 21; oldPatch[O_FEG_R] = 39;
    oldPatch[O_LFO_WAVE] = 3; oldPatch[O_LFO_RATE] = 76; oldPatch[O_LFO_AMT] = 29;
    oldPatch[O_LFO_DEST] = 1; oldPatch[O_LFO_SYNC] = 1;
    oldPatch[O_VOICE_MODE] = 1; oldPatch[O_VELO_AMT] = 64; oldPatch[O_DRIVE] = 52;
    oldPatch[O_LEVEL] = 101; oldPatch[O_SEND_DLY] = 27; oldPatch[O_SEND_REV] = 38;
    oldPatch[O_BEND] = 5;

    uint8_t oldKit[kOldDrums][kOldDrumParams];
    for (int l = 0; l < kOldDrums; ++l)
        for (int d = 0; d < kOldDrumParams; ++d)
            oldKit[l][d] = (uint8_t)(10 + l * 8 + d);
    const uint8_t oldDly = 31, oldRev = 47;

    const int n = writeV2Blob(src, oldPatch, oldKit, oldDly, oldRev, buf);
    Project got;
    CHECK(projectDeserialize(got, buf, n), "a v2 project no longer loads");
    CHECK(strcmp(got.name, "OLDSONG") == 0, "v2 name lost: %s", got.name);
    CHECK(got.bpm == 143, "v2 tempo lost: %d", got.bpm);

    // Steps and melodic tracks come back untouched.
    for (int i = 0; i < kPatternCount; ++i) {
        CHECK(got.pat[i].length == src.pat[i].length, "v2 pattern %d length lost", i);
        for (int t = 0; t < kMelTracks; ++t)
            for (int st = 0; st < kMaxSteps; ++st)
                CHECK(got.pat[i].mel[t][st].note == src.pat[i].mel[t][st].note,
                      "v2 pattern %d note %d lost", i, st);
    }

    // The drum lanes must be remapped, not copied by index. v2 lane 4 was
    // CLAP, which is lane 6 now; copying by index would silently turn every
    // saved clap into a ride.
    static const uint8_t expectLane[kOldDrums] = {
        DL_KICK, DL_SNARE, DL_CHH, DL_OHH, DL_CLAP, DL_TOM, DL_RIM, DL_CRASH, DL_PERC
    };
    for (int i = 0; i < kPatternCount; ++i)
        for (int l = 0; l < kOldDrums; ++l)
            CHECK(memcmp(got.pat[i].drum[expectLane[l]], src.pat[i].drum[l], kMaxSteps) == 0,
                  "v2 pattern %d lane %d did not land on %s", i, l, kDrumNames[expectLane[l]]);
    // The three lanes v2 never had must come back empty rather than filled
    // with somebody else's notes.
    for (int i = 0; i < kPatternCount; ++i) {
        static const uint8_t fresh[3] = {DL_RIDE, DL_COWBELL, DL_SHAKER};
        for (int f = 0; f < 3; ++f)
            for (int st = 0; st < kMaxSteps; ++st)
                CHECK(got.pat[i].drum[fresh[f]][st] == 0,
                      "new lane %s came back with data", kDrumNames[fresh[f]]);
    }

    // The patch converts: engine, then every parameter that still means the
    // same thing, then the oscillator block.
    const Patch& gp = got.patch[0];
    CHECK(gp.engine() == ENG_ANALOG, "v2 engine 0 should map to ANALOG, got %d", gp.engine());
    CHECK(gp.get(P_CUTOFF) == 97, "v2 cutoff lost: %d", gp.get(P_CUTOFF));
    CHECK(gp.get(P_RESO) == 66, "v2 reso lost: %d", gp.get(P_RESO));
    CHECK(gp.get(P_AMP_A) == 7 && gp.get(P_AMP_D) == 61 &&
          gp.get(P_AMP_S) == 88 && gp.get(P_AMP_R) == 45, "v2 amp envelope lost");
    CHECK(gp.get(P_FEG_D) == 55, "v2 filter envelope lost");
    CHECK(gp.get(P_LFO_RATE) == 76 && gp.get(P_LFO_DEST) == 1, "v2 LFO lost");
    CHECK(gp.get(P_VOICE_MODE) == 1, "v2 voice mode lost");
    CHECK(gp.get(P_DRIVE) == 52 && gp.get(P_LEVEL) == 101, "v2 output stage lost");
    CHECK(gp.get(P_SEND_DLY) == 27 && gp.get(P_SEND_REV) == 38, "v2 sends lost");
    CHECK(gp.get(P_BEND) == 5, "v2 bend lost");
    CHECK(gp.get(PA_O1_WAVE) == 2 && gp.get(PA_O1_LEVEL) == 111, "v2 osc 1 lost");
    CHECK(gp.get(PA_O2_WAVE) == 1 && gp.get(PA_O2_LEVEL) == 83, "v2 osc 2 lost");
    CHECK(gp.get(PA_SUB_LEVEL) == 44 && gp.get(PA_NOISE) == 17, "v2 sub/noise lost");
    CHECK(gp.get(PA_PW) == 99, "v2 pulse width lost");

    // The kit's four old parameters land on the same four lanes, remapped,
    // and the kit-wide sends become each lane's own send.
    for (int l = 0; l < kOldDrums; ++l)
        for (int d = 0; d < kOldDrumParams; ++d)
            CHECK(got.kit.get(expectLane[l], (uint8_t)d) == oldKit[l][d],
                  "v2 kit lane %d param %d lost", l, d);
    for (int l = 0; l < DL_COUNT; ++l) {
        CHECK(got.kit.get((uint8_t)l, DP_DLY) == oldDly, "v2 kit delay send lost on lane %d", l);
        CHECK(got.kit.get((uint8_t)l, DP_REV) == oldRev, "v2 kit reverb send lost on lane %d", l);
        for (int m = 0; m < DM_COUNT; ++m)
            CHECK(got.kit.macro((uint8_t)l, (uint8_t)m) == kMacroNeutral,
                  "imported kit should start with neutral macros");
    }
}

// ---------------------------------------------------------------------------
// Sound-quality regressions. These pin the specific defects that were found by
// listening to the renderer's output and measuring it; every one of them was a
// real bug that the "is it finite and non-silent" tests above did not catch.
static std::vector<float> renderLane(DrumKit& kit, uint8_t lane, uint8_t vel, float sec) {
    DrumEngine d;
    d.init();
    d.setKit(&kit);
    d.trigger(lane, vel);
    std::vector<float> out;
    float b[kBlockSize], sd[kBlockSize], sr[kBlockSize];
    for (int done = 0; done < (int)(sec * kSampleRate); done += kBlockSize) {
        memset(b, 0, sizeof(b)); memset(sd, 0, sizeof(sd)); memset(sr, 0, sizeof(sr));
        d.render(b, sd, sr, kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) out.push_back(b[i]);
    }
    return out;
}

static std::vector<float> renderNote(const Patch& pt, uint8_t note, float sec) {
    Voice v;
    v.init(0x1234);
    v.setPatch(&pt);
    v.noteOn(note, 110, false);
    std::vector<float> out;
    float b[kBlockSize];
    for (int done = 0; done < (int)(sec * kSampleRate); done += kBlockSize) {
        memset(b, 0, sizeof(b));
        v.render(b, kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) out.push_back(b[i]);
    }
    return out;
}

static void testDrumSound() {
    for (uint8_t k = 0; k < kKitCount; ++k) {
        DrumKit kit;
        loadKit(kit, k);

        // The kick has to survive the speaker. The Cardputer reproduces almost
        // nothing below ~200 Hz, so a kick whose level collapses through a
        // 200 Hz high-pass is one you cannot hear on the device however good
        // it looks on a scope.
        auto kick = renderLane(kit, DL_KICK, 120, 1.2f);
        const float hp = wav::highPassPeakDb(kick, 200.0f);
        CHECK(hp > -18.0f, "kit %s kick is %.1f dB through a 200 Hz high-pass - inaudible on the device",
              kit.name, hp);

        // Lane balance: nothing may sit more than 14 dB off the kick, or it is
        // either inaudible in a mix or drowning everything else. The metal
        // lanes were 14 dB down before their gain staging was fixed.
        const float ref = wav::peak(kick);
        CHECK(ref > 0.01f, "kit %s kick is silent", kit.name);
        for (uint8_t lane = 0; lane < DL_COUNT; ++lane) {
            const float p = wav::peak(renderLane(kit, lane, 110, 1.2f));
            const float db = 20.0f * log10f(fmaxf(p, 1e-6f) / ref);
            CHECK(db > -14.0f && db < 10.0f,
                  "kit %s lane %s is %+.1f dB against the kick", kit.name, kDrumShort[lane], db);
        }
    }

    // A closed hat must actually cut an open one, and over a fade rather than
    // a hard edge, because a hard edge clicks.
    DrumKit kit;
    loadKit(kit, 0);
    DrumEngine d;
    d.init();
    d.setKit(&kit);
    d.trigger(DL_OHH, 120);
    float b[kBlockSize], sd[kBlockSize], sr[kBlockSize];
    for (int i = 0; i < 20; ++i) {
        memset(b, 0, sizeof(b)); memset(sd, 0, sizeof(sd)); memset(sr, 0, sizeof(sr));
        d.render(b, sd, sr, kBlockSize);
    }
    const float before = d.laneLevel(DL_OHH);
    CHECK(before > 0.1f, "open hat should still be ringing before the choke");
    d.trigger(DL_CHH, 120);
    // 3 ms is a little over the 2 ms fade.
    for (int i = 0; i < 3 * 32 / kBlockSize + 1; ++i) {
        memset(b, 0, sizeof(b)); memset(sd, 0, sizeof(sd)); memset(sr, 0, sizeof(sr));
        d.render(b, sd, sr, kBlockSize);
    }
    CHECK(d.laneLevel(DL_OHH) < before * 0.1f,
          "closed hat did not choke the open hat (%.3f -> %.3f)", before, d.laneLevel(DL_OHH));
    CHECK(kChokeGroup[DL_CHH] == kChokeGroup[DL_OHH] && kChokeGroup[DL_CHH] != 0,
          "hats must share a choke group");
}

static void testEngineSound() {
    // Every engine, played bare, must be in tune and free of folded-back
    // harmonics. Both were broken: PLUCK was an octave out at some notes and
    // collapsed into a 2 Hz rumble at the top of the keyboard, and the
    // wavetable reader was re-introducing the aliasing the mip-maps exist to
    // remove.
    for (uint8_t eng = 0; eng < ENG_COUNT; ++eng) {
        Patch pt;
        pt.reset();
        pt.setEngine(eng);
        pt.set(P_FIL_TYPE, 0);
        pt.set(P_LFO_AMT, 0);
        pt.set(P_AMP_A, 0);
        pt.set(P_AMP_S, 127);

        for (uint8_t note : {48, 60, 72, 84}) {
            auto x = renderNote(pt, note, 0.5f);
            CHECK(wav::peak(x) > 0.005f, "%s is silent at note %d", kEngineNames[eng], note);
            CHECK(wav::peak(x) < 2.0f, "%s clips at note %d (%.2f)", kEngineNames[eng], note,
                  wav::peak(x));
            CHECK(fabsf(wav::dcOffset(x)) < 0.02f, "%s has DC at note %d (%.4f)",
                  kEngineNames[eng], note, wav::dcOffset(x));

            auto sp = wav::spectrum(x, 2048, 8192, kSampleRate);
            const float played = 440.0f * powf(2.0f, (note - 69) / 12.0f);
            // ORGAN's 16' drawbar sits an octave below the note, so its
            // harmonic grid starts there.
            const float f0 = (eng == ENG_ORGAN) ? played * 0.5f : played;
            const float alias = wav::aliasEnergy(sp, f0) * 100.0f;
            CHECK(alias < 12.0f, "%s note %d: %.0f%% of its energy is off-harmonic",
                  kEngineNames[eng], note, alias);

            // The strongest partial must belong to the note. This is what
            // catches a mistuned string or an oscillator running at the wrong
            // rate, which no amount of "is it finite" would notice.
            size_t best = 1;
            for (size_t i = 2; i < sp.mag.size(); ++i) if (sp.mag[i] > sp.mag[best]) best = i;
            const float strongest = best * sp.binHz;
            const float ratio = strongest / f0;
            const float nearest = roundf(ratio);
            CHECK(nearest >= 1.0f && fabsf(ratio - nearest) < 0.04f,
                  "%s note %d: strongest partial %.0f Hz is not a harmonic of %.0f Hz",
                  kEngineNames[eng], note, strongest, f0);
        }
    }

    // The pluck pool is finite; running out must degrade rather than corrupt.
    pluckPoolReset();
    CHECK(pluckLinesFree() == kPluckLines, "pluck pool did not reset");
    {
        Patch pt; pt.reset(); pt.setEngine(ENG_PLUCK);
        Voice v[kPluckLines + 2];
        for (int i = 0; i < kPluckLines + 2; ++i) {
            v[i].init(0x100u + i);
            v[i].setPatch(&pt);
            v[i].noteOn((uint8_t)(50 + i), 100, false);
        }
        float b[kBlockSize];
        for (int i = 0; i < kPluckLines + 2; ++i) {
            memset(b, 0, sizeof(b));
            v[i].render(b, kBlockSize);
            for (int s = 0; s < kBlockSize; ++s)
                CHECK(finite_(b[s]), "pluck voice %d produced a non-finite sample past the pool", i);
        }
    }
    pluckPoolReset();
}

// ---------------------------------------------------------------------------
int main() {
    printf("SynthCard unit tests\n");
    testDsp();
    testEnvelope();
    testFilter();
    testPresets();
    testVoice();
    testVoiceRange();
    testDrums();
    testDrumSound();
    testEngineSound();
    testEffects();
    testSequencerTiming();
    testSequencerPatternsAndSong();
    testRecording();
    testGateLengths();
    testRecordedLength();
    testEraseStep();
    testChords();
    testChordPlayback();
    testMetronome();
    testPatternLength();
    testScales();
    testEuclid();
    testArp();
    testGenerators();
    testProjectFormat();
    testV1Compatibility();
    printf("%d checks, %d failures\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
