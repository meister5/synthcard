// SynthCard - shared DSP primitives.
// Pure C++: no Arduino / M5 dependency so this builds on the host for tests.
#pragma once
#include <stdint.h>
#include <math.h>

namespace synth {

// ---- global audio format -------------------------------------------------
// 32 kHz mono. The ES8311 runs at 48 kHz; M5Unified box-resamples our blocks
// up to it. 32 kHz buys ~16 kHz of bandwidth while leaving ~7500 CPU cycles
// per sample for the whole engine.
constexpr float kSampleRate = 32000.0f;
constexpr int   kBlockSize  = 128;          // 4 ms per block
// Control-rate chunk: filter/LFO/operator coefficients are refreshed every 16
// samples (0.5 ms). Fast enough that a 20 ms envelope is smooth, cheap enough
// that the transcendentals cost almost nothing per sample.
constexpr int   kCtrlChunk  = 16;
constexpr float kInvSampleRate = 1.0f / kSampleRate;

constexpr float kPi  = 3.14159265358979f;
constexpr float kTau = 6.28318530717959f;

// ---- small fast helpers --------------------------------------------------
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int   clampi(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t)    { return a + (b - a) * t; }

// Wrap a phase accumulator into [0,1).
inline float wrap01(float p) { while (p >= 1.0f) p -= 1.0f; while (p < 0.0f) p += 1.0f; return p; }

// Cheap odd polynomial sine over [0,1) phase. Max error ~0.001 - inaudible
// here and roughly 5x faster than sinf() on the LX7.
inline float fastSin01(float phase) {
    float x = phase - 0.5f;             // [-0.5,0.5)
    float t = x * 2.0f;                 // [-1,1)
    float a = t < 0.0f ? -t : t;
    float y = t * (1.0f - a) * 4.0f;    // parabola approximation
    return -(0.775f * y + 0.225f * y * (y < 0.0f ? -y : y));
}

// tanh-ish saturator. Monotonic, unity slope at 0, saturates to +-1.
inline float softClip(float x) {
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

// PolyBLEP correction for a discontinuity at phase 0 with step size dt.
inline float polyBlep(float t, float dt) {
    if (t < dt)          { t /= dt;         return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt)   { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// MIDI note -> Hz. note 69 = A4 = 440 Hz.
inline float noteToHz(float note) { return 440.0f * powf(2.0f, (note - 69.0f) * (1.0f / 12.0f)); }

// Exponential one-pole smoother for control-rate parameters.
struct Smoother {
    float value = 0.0f, coeff = 0.01f;
    void setTimeMs(float ms) { coeff = 1.0f - expf(-1.0f / (ms * 0.001f * kSampleRate)); }
    void reset(float v) { value = v; }
    inline float process(float target) { value += (target - value) * coeff; return value; }
};

// 32-bit xorshift. Deterministic, allocation-free, good enough for noise.
struct Rng {
    uint32_t s = 0x9E3779B9u;
    inline uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    inline float bipolar() { return (float)(int32_t)next() * (1.0f / 2147483648.0f); }
    inline float unipolar() { return (float)next() * (1.0f / 4294967296.0f); }
    inline uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

// 2^x for |x| within a few octaves. Two Taylor terms, ~0.1 cent of error over
// +-2 octaves, and roughly 20x faster than powf() on the LX7. The engines call
// this once per control chunk per voice, where powf() showed up in profiling.
inline float fastExp2(float x) {
    int   i = (int)(x + (x < 0.0f ? -0.5f : 0.5f));   // nearest integer octave
    float f = x - (float)i;                           // remainder in [-0.5,0.5]
    float p = 1.0f + f * (0.6931472f + f * (0.2402265f + f * 0.0555041f));
    // Scale by 2^i without a loop: bias the float exponent directly.
    union { float f; uint32_t u; } s;
    s.u = (uint32_t)((127 + i) << 23);
    return p * s.f;
}

// Note number -> Hz via fastExp2. Used per note-on and per control chunk.
inline float noteToHzFast(float note) { return 440.0f * fastExp2((note - 69.0f) * (1.0f / 12.0f)); }

// ---- shared voice primitives --------------------------------------------
// These live here rather than in voice.h because the engines and the drum
// families need them too, and a drum lane should not have to include a
// polyphonic synth voice to get a filter.

// Linear attack, exponential decay/release. Click-free and cheap.
class Env {
public:
    enum Stage : uint8_t { E_IDLE, E_ATK, E_DEC, E_SUS, E_REL };  // DEC is an Arduino macro
    // rate lets a control-rate envelope (stepped once per control chunk)
    // use the same wall-clock times as a per-sample one.
    void configure(float aMs, float dMs, float sus, float rMs, float rate = kSampleRate);
    void gate(bool on);
    void reset() { stage_ = E_IDLE; level_ = 0.0f; }
    inline float process() {
        switch (stage_) {
            case E_ATK:
                level_ += atkInc_;
                if (level_ >= 1.0f) { level_ = 1.0f; stage_ = E_DEC; }
                break;
            case E_DEC:
                level_ += (sus_ - level_) * decCoef_;
                if (level_ - sus_ < 0.001f && sus_ - level_ < 0.001f) { level_ = sus_; stage_ = E_SUS; }
                break;
            case E_SUS: level_ = sus_; break;
            case E_REL:
                level_ -= level_ * relCoef_;
                if (level_ < 0.0004f) { level_ = 0.0f; stage_ = E_IDLE; }
                break;
            default: level_ = 0.0f; break;
        }
        return level_;
    }
    inline bool  idle()  const { return stage_ == E_IDLE; }
    inline bool  releasing() const { return stage_ == E_REL; }
    inline float level() const { return level_; }
private:
    Stage stage_ = E_IDLE;
    float level_ = 0.0f, sus_ = 1.0f;
    float atkInc_ = 1.0f, decCoef_ = 0.01f, relCoef_ = 0.01f;
};

// Cytomic / TPT state-variable filter. Stable up to Nyquist, one shared
// topology for LP / HP / BP / notch.
class SVF {
public:
    void reset() { ic1_ = ic2_ = 0.0f; }
    void setCoeffs(float cutoffHz, float q);
    inline float process(float in, uint8_t type) {
        float v3 = in - ic2_;
        float v1 = a1_ * ic1_ + a2_ * v3;
        float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = 2.0f * v1 - ic1_;
        ic2_ = 2.0f * v2 - ic2_;
        switch (type) {
            case 1: return v2;                       // low
            case 2: return in - k_ * v1 - v2;        // high
            case 3: return v1;                       // band
            case 4: return in - k_ * v1;             // notch (low + high)
            default: return in;
        }
    }
private:
    float a1_ = 1.0f, a2_ = 0.0f, a3_ = 0.0f, k_ = 1.0f;
    float ic1_ = 0.0f, ic2_ = 0.0f;
};

class Lfo {
public:
    void configure(uint8_t wave, float rateHz) { wave_ = wave; inc_ = rateHz * kInvSampleRate; }
    void retrigger() { phase_ = 0.0f; }
    float step();                       // called once per control chunk
private:
    uint8_t wave_ = 0;
    float   phase_ = 0.0f, inc_ = 0.001f, sh_ = 0.0f;
    Rng     rng_;
};

}  // namespace synth
