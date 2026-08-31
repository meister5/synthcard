// SynthCard - oscillator engines.
//
// Voice owns everything shared (envelopes, filter, glide, LFO, output stage)
// and delegates raw oscillator generation to one of these. Each engine is a
// POD state struct plus a free function that fills one control chunk, so the
// engine is chosen once per chunk rather than tested once per sample, and each
// engine can be exercised on the host without instantiating the others.
//
// All six states live in a union inside Voice, which keeps a voice a fixed
// size and means nothing in the render path allocates. Karplus-Strong's delay
// line is deliberately NOT in the union - see the pool below.
#pragma once
#include "dsp.h"
#include "patch.h"

namespace synth {

// Control-rate context, refreshed by Voice once per chunk. An engine never
// reads pitch or modulation from anywhere else.
struct EngineCtx {
    const Patch* pt;
    float   inc;        // fundamental increment in cycles/sample
    float   shape;      // pulse width / wavetable morph, 0..1, LFO applied
    float   lfo;        // LFO value already scaled by amount, -1..1
    float   vel;        // 0..1
    uint8_t note;
    Rng*    rng;
};

// --------------------------------------------------------------- ANALOG ----
constexpr int kMaxUnison = 4;
struct AnalogState {
    float ph1[kMaxUnison];
    float det[kMaxUnison];      // per-copy detune multiplier
    float ph2, phSub;
    float rat2;                 // osc 2 frequency ratio, fixed at note-on
    uint8_t uni;                // 1..kMaxUnison
    float   uniGain;
};
void analogNoteOn(AnalogState&, const Patch&, Rng&);
void analogRender(AnalogState&, const EngineCtx&, float* out, int n);
// How many voice slots a patch consumes. Unison is charged honestly rather
// than silently melting the CPU.
uint8_t analogVoiceCost(const Patch&);

// ------------------------------------------------------------------- FM ----
struct FmOp { float ph, out; };
struct FmState {
    FmOp op[4];
    float lvl[4], inc[4];
    float envLevel[4], envAtk[4], envDec[4], envSus[4];
    uint8_t stage[4];           // 0 attack, 1 decay
    float fb;
};
void fmNoteOn(FmState&, const Patch&, Rng&);
void fmRender(FmState&, const EngineCtx&, float* out, int n);

// -------------------------------------------------------------- WAVETBL ----
struct WtState {
    float ph, ph2, phSub;
    float rat2;
    uint8_t mip;                // octave band chosen at note-on
};
void wtNoteOn(WtState&, const Patch&, float inc, Rng&);
void wtRender(WtState&, const EngineCtx&, float* out, int n);

// ---------------------------------------------------------------- PLUCK ----
// The delay line comes from a shared pool: putting it in the union would cost
// every voice ~1 KB whether or not pluck is in use. buf == nullptr means the
// line was reclaimed by a later note; the engine then renders silence and the
// amp envelope retires the voice.
constexpr int kPluckLines = 4;
constexpr int kPluckLen   = 512;        // floors pluck at ~62 Hz

struct PluckState {
    int16_t* buf;
    int      len;                       // active loop length in samples
    float    frac;                      // fractional-delay coefficient
    int      wr;
    float    ap;                        // allpass interpolator state
    float    lp;                        // loop lowpass state
    float    damp;
    float    bow;               // continuous excitation for the BOW mode
    float    body1, body2, bodyAmt;
    SVF      bodyFilt;
};
void pluckNoteOn(PluckState&, const Patch&, float hz, float vel, Rng&);
void pluckRender(PluckState&, const EngineCtx&, float* out, int n);
void pluckRelease(PluckState&);
void pluckPoolReset();
int  pluckLinesFree();                  // for tests

// ---------------------------------------------------------------- ORGAN ----
constexpr int kDrawbars = 9;
struct OrganState {
    float ph[kDrawbars];
    float lvl[kDrawbars];               // 0 for partials above Nyquist
    float ratio[kDrawbars];
    float click, clickCoef;
    float rotPh;
    float norm;
};
void organNoteOn(OrganState&, const Patch&, float hz, Rng&);
void organRender(OrganState&, const EngineCtx&, float* out, int n);

// ----------------------------------------------------------------- CHIP ----
struct ChipState {
    uint32_t lfsr;
    float    ph, ph2;
    float    rat2;
    float    duty;
    uint8_t  arpMode, arpStep;
    int      arpCountdown, arpPeriod;
    float    arpOffset;                 // semitones added by the chip arp
    float    vibPh, vibDepth;
    int      vibDelay;                  // samples remaining before vibrato
    uint8_t  noiseMode;
    int      noiseDiv, noiseCount;
    float    noiseVal;
};
void chipNoteOn(ChipState&, const Patch&, Rng&);
void chipRender(ChipState&, const EngineCtx&, float* out, int n);

// ------------------------------------------------------------------ union --
union EngineState {
    AnalogState analog;
    FmState     fm;
    WtState     wt;
    PluckState  pluck;
    OrganState  organ;
    ChipState   chip;
    // PluckState holds an SVF, whose member initialisers make it non-trivial,
    // which would delete this union's default constructor. Voice clears the
    // whole union at init and at every engine change, so starting from a
    // value-initialised AnalogState is enough.
    EngineState() : analog{} {}
};

// How many voice slots a patch costs (1 for everything except ANALOG unison).
uint8_t patchVoiceCost(const Patch&);

}  // namespace synth
