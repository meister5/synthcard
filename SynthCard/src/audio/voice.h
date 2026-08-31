// SynthCard - one polyphonic synth voice.
//
// Voice owns everything that is the same whichever engine is playing: glide,
// the amp and filter envelopes, the filter (2 or 4 pole), the LFO, velocity
// and the output stage. Oscillator generation is delegated to one of the six
// engines in engines.h, chosen once per control chunk.
//
// The shared DSP primitives (Env, SVF, Lfo) live in dsp.h so the drum families
// can use them without including a polyphonic voice.
#pragma once
#include "dsp.h"
#include "patch.h"
#include "engines.h"

namespace synth {

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
    // Voice slots this note occupies: 1, or the unison width on ANALOG.
    inline uint8_t cost() const { return cost_; }

    // Adds this voice into out[] (does not clear it).
    void render(float* out, int n);

private:
    void updateControl();
    void startEngine(bool legato);
    void renderEngine(float* buf, int n);

    const Patch* patch_ = nullptr;
    bool     active_ = false, gate_ = false;
    uint8_t  note_ = 60, vel_ = 100;
    uint8_t  cost_ = 1;
    uint32_t age_ = 0;

    EngineState eng_;
    uint8_t     engId_ = ENG_ANALOG;      // latched at note-on
    EngineCtx   ctx_;

    float targetHz_ = 440.0f, curHz_ = 440.0f, glideCoef_ = 1.0f;
    float velGain_ = 1.0f;

    Env  ampEnv_, filEnv_;
    SVF  filter_, filter2_;
    Lfo  lfo_;
    Rng  rng_;
    float lfoVal_ = 0.0f;
    float ampLfo_ = 1.0f;
};

// Kept for callers that already spoke in envelope-parameter units.
inline float envMs(uint8_t v) { return envMsFor(v); }

}  // namespace synth
