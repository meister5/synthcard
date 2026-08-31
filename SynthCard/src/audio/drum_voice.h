// SynthCard - the per-lane drum voice, shared by the five synthesis families.
//
// One struct covers every family rather than a union per family: the lanes are
// few, the fields overlap heavily, and a flat struct keeps the family code
// readable. Each family uses the subset it needs and ignores the rest.
#pragma once
#include "dsp.h"

namespace synth {

// The eight parameters a family receives, already normalised to 0..1 with the
// kit macros folded in, plus the hit's velocity.
struct DrumHit {
    float tune, decay, tone, level, snap, drive;
    float vel;                  // 0..1
};

constexpr int kMetalOsc = 6;

struct DV {
    bool     active = false;
    uint32_t t = 0;                     // samples since trigger

    float amp = 0.0f, ampCoef = 0.0f;
    float gain = 1.0f;                  // level * velocity
    float drive = 0.0f;

    // Body oscillators.
    float ph = 0.0f, ph2 = 0.0f, inc = 0.0f, inc2 = 0.0f;
    // Two-stage pitch envelope. Stage 1 is the fast initial drop that gives a
    // kick its thump; stage 2 is the slow settle underneath it.
    float pitch = 0.0f, pitchCoef = 0.0f;
    float pitch2 = 0.0f, pitch2Coef = 0.0f;

    // Two independent noise bands (shell and wires on a snare).
    float noiseAmp = 0.0f, noiseCoef = 0.0f;
    float noise2Amp = 0.0f, noise2Coef = 0.0f;

    // Transient layer.
    float snap = 0.0f, snapCoef = 0.0f;

    // Sub layer, for the kick's fundamental on headphones.
    float subAmp = 0.0f, subCoef = 0.0f, subPh = 0.0f, subInc = 0.0f;

    // Metallic oscillator bank.
    float mph[kMetalOsc] = {0}, minc[kMetalOsc] = {0};
    uint8_t mcount = 0;

    // Attack ramp, for a shaker (which is not a hat precisely because it has
    // one) and the crash swell.
    float atk = 1.0f, atkInc = 1.0f;

    float sendDly = 0.0f, sendRev = 0.0f;

    SVF filt, filt2;
    Rng rng;
};

// --- families -------------------------------------------------------------
// Each pair lives in its own translation unit so a lane's synthesis can be
// read, changed and tested without the other eleven in the way.
void membraneTrigger(DV&, const DrumHit&, uint8_t lane);
void membraneRender(DV&, uint8_t lane, float* out, int n);

void snareTrigger(DV&, const DrumHit&, uint8_t lane);
void snareRender(DV&, uint8_t lane, float* out, int n);

void metalTrigger(DV&, const DrumHit&, uint8_t lane);
void metalRender(DV&, uint8_t lane, float* out, int n);

void noiseTrigger(DV&, const DrumHit&, uint8_t lane);
void noiseRender(DV&, uint8_t lane, float* out, int n);

void percTrigger(DV&, const DrumHit&, uint8_t lane);
void percRender(DV&, uint8_t lane, float* out, int n);

// Per-sample decay factor such that the level falls 60 dB in `ms`, so the
// numbers in the kit tables are real decay times.
inline float decCoef(float ms) { return expf(-6.9f / (ms * 0.001f * kSampleRate)); }

}  // namespace synth
