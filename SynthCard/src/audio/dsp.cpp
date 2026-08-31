// SynthCard - out-of-line bodies for the shared DSP primitives in dsp.h.
#include "dsp.h"

namespace synth {

void Env::configure(float aMs, float dMs, float sus, float rMs, float rate) {
    sus_     = clampf(sus, 0.0f, 1.0f);
    atkInc_  = 1.0f / (clampf(aMs, 0.5f, 20000.0f) * 0.001f * rate);
    // -6.9 = ln(1000): the labelled time is the time to fall 60 dB, which is
    // what "decay" and "release" mean on a hardware synth.
    decCoef_ = clampf(1.0f - expf(-6.9f / (clampf(dMs, 1.0f, 20000.0f) * 0.001f * rate)), 0.0f, 1.0f);
    relCoef_ = clampf(1.0f - expf(-6.9f / (clampf(rMs, 1.0f, 20000.0f) * 0.001f * rate)), 0.0f, 1.0f);
}

void Env::gate(bool on) {
    if (on) { stage_ = E_ATK; }
    else if (stage_ != E_IDLE) { stage_ = E_REL; }
}

void SVF::setCoeffs(float cutoffHz, float q) {
    cutoffHz = clampf(cutoffHz, 20.0f, kSampleRate * 0.45f);
    // tan(pi*f/sr) via a 3-term series; accurate enough below 0.45*sr.
    float x = kPi * cutoffHz * kInvSampleRate;
    float x2 = x * x;
    float g = x * (1.0f + x2 * (0.3333333f + x2 * 0.1333333f));
    k_ = 1.0f / clampf(q, 0.5f, 12.0f);
    float a1 = 1.0f / (1.0f + g * (g + k_));
    a1_ = a1;
    a2_ = g * a1;
    a3_ = g * a2_;
}

float Lfo::step() {
    phase_ += inc_ * kCtrlChunk;
    bool wrapped = false;
    while (phase_ >= 1.0f) { phase_ -= 1.0f; wrapped = true; }
    switch (wave_) {
        case 1: return phase_ < 0.5f ? (phase_ * 4.0f - 1.0f) : (3.0f - phase_ * 4.0f);   // tri
        case 2: return phase_ < 0.5f ? 1.0f : -1.0f;                                      // square
        case 3: if (wrapped) sh_ = rng_.bipolar(); return sh_;                            // sample+hold
        case 4: return phase_ * 2.0f - 1.0f;                                              // saw
        default: return fastSin01(phase_);                                                // sine
    }
}

}  // namespace synth
