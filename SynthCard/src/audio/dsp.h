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

}  // namespace synth
