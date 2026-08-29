// SynthCard - one polyphonic synth voice.
#pragma once
#include "dsp.h"
#include "patch.h"

namespace synth {

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
// topology for LP / HP / BP.
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

class Voice {
public:
    void init(uint32_t seed);
    void setPatch(const Patch* p) { patch_ = p; }
    const Patch* patch() const { return patch_; }

    void noteOn(uint8_t note, uint8_t vel, bool legato);
    void noteOff();
    void kill();                                  // immediate, for voice steal
    inline bool  active() const { return active_; }
    inline bool  released() const { return ampEnv_.releasing() || !gate_; }
    inline uint8_t note() const { return note_; }
    inline uint32_t age() const { return age_; }
    inline void touch(uint32_t t) { age_ = t; }
    inline float envLevel() const { return ampEnv_.level(); }

    // Adds this voice into out[] (does not clear it).
    void render(float* out, int n);

private:
    void updateControl();

    const Patch* patch_ = nullptr;
    bool     active_ = false, gate_ = false;
    uint8_t  note_ = 60, vel_ = 100;
    uint32_t age_ = 0;

    float ph1_ = 0.0f, ph2_ = 0.0f, phSub_ = 0.0f;
    float inc1_ = 0.0f, inc2_ = 0.0f, incSub_ = 0.0f;
    float targetHz_ = 440.0f, curHz_ = 440.0f, glideCoef_ = 1.0f;
    float velGain_ = 1.0f;

    Env  ampEnv_, filEnv_;
    SVF  filter_;
    Lfo  lfo_;
    Rng  rng_;
    float lfoVal_ = 0.0f;
    float pwm_ = 0.5f;
    float cutoffBase_ = 8000.0f;
};

// Converts an envelope-time parameter (0..127) to milliseconds.
inline float envMs(uint8_t v) { float x = v * (1.0f / 127.0f); return 1.0f + 7999.0f * x * x * x; }

}  // namespace synth
