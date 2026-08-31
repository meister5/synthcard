// SynthCard - ANALOG engine: two oscillators, sub and noise, with unison.
#include "engines.h"

namespace synth {

// Oscillator 1's unison copies are detuned symmetrically about the note, so
// the perceived pitch does not shift as SPREAD is raised.
static const float kSpread[kMaxUnison][kMaxUnison] = {
    { 0.0f,  0.0f,  0.0f,  0.0f},
    {-1.0f,  1.0f,  0.0f,  0.0f},
    {-1.0f,  0.0f,  1.0f,  0.0f},
    {-1.0f, -0.34f, 0.34f, 1.0f},
};

static inline float oscWave(uint8_t w, float ph, float inc, float pw, Rng& rng) {
    switch (w) {
        case 1: return (ph < 0.5f ? 1.0f : -1.0f) + polyBlep(ph, inc)
                       - polyBlep(wrap01(ph + 0.5f), inc);
        case 2: return (ph < pw ? 1.0f : -1.0f) + polyBlep(ph, inc)
                       - polyBlep(wrap01(ph + 1.0f - pw), inc);
        case 3: return ph < 0.5f ? (4.0f * ph - 1.0f) : (3.0f - 4.0f * ph);
        case 4: return fastSin01(ph);
        case 5: return rng.bipolar();
        default: return 2.0f * ph - 1.0f - polyBlep(ph, inc);
    }
}

uint8_t analogVoiceCost(const Patch& pt) {
    uint8_t n = (uint8_t)(pt.get(PA_UNISON) + 1);
    return n < 1 ? 1 : (n > kMaxUnison ? kMaxUnison : n);
}

uint8_t patchVoiceCost(const Patch& pt) {
    return pt.engine() == ENG_ANALOG ? analogVoiceCost(pt) : 1;
}

void analogNoteOn(AnalogState& s, const Patch& pt, Rng& rng) {
    s.uni = analogVoiceCost(pt);
    // Equal-power sum: four detuned saws are not four times as loud.
    s.uniGain = 1.0f / sqrtf((float)s.uni);

    const float cents = pt.norm(PA_UNI_DET) * 25.0f;      // up to +-25 cents
    for (uint8_t i = 0; i < kMaxUnison; ++i) {
        s.det[i] = powf(2.0f, kSpread[s.uni - 1][i] * cents / 1200.0f);
        // A small phase scatter stops the copies firing one shared transient.
        s.ph1[i] = rng.unipolar() * 0.02f + (float)i * (1.0f / (float)kMaxUnison);
        s.ph1[i] = wrap01(s.ph1[i]);
    }
    s.ph2   = rng.unipolar() * 0.02f;
    s.phSub = 0.0f;

    const float semi = (float)pt.get(PA_O2_SEMI) - 24.0f;
    const float cent = pt.bip(PA_O2_DETUNE) * 0.5f;
    s.rat2 = powf(2.0f, (semi + cent) / 12.0f);
}

void analogRender(AnalogState& s, const EngineCtx& c, float* out, int n) {
    const Patch& pt = *c.pt;
    const uint8_t w1 = pt.get(PA_O1_WAVE);
    const uint8_t w2 = pt.get(PA_O2_WAVE);
    const float l1  = pt.norm(PA_O1_LEVEL) * s.uniGain;
    const float l2  = pt.norm(PA_O2_LEVEL);
    const float lsb = pt.norm(PA_SUB_LEVEL);
    const float lnz = pt.norm(PA_NOISE);
    const bool  subSine = pt.get(PA_SUB_WAVE) != 0;
    const float pw = c.shape;
    Rng& rng = *c.rng;

    const float inc2   = clampf(c.inc * s.rat2, 0.0f, 0.49f);
    const float incSub = clampf(c.inc * 0.5f,   0.0f, 0.49f);
    const uint8_t uni = s.uni;

    for (int i = 0; i < n; ++i) {
        float v = 0.0f;
        for (uint8_t u = 0; u < uni; ++u) {
            const float inc = clampf(c.inc * s.det[u], 0.0f, 0.49f);
            s.ph1[u] = wrap01(s.ph1[u] + inc);
            v += oscWave(w1, s.ph1[u], inc, pw, rng);
        }
        float sig = v * l1;

        if (l2 > 0.001f) {
            s.ph2 = wrap01(s.ph2 + inc2);
            sig += oscWave(w2, s.ph2, inc2, pw, rng) * l2;
        }
        if (lsb > 0.001f) {
            s.phSub = wrap01(s.phSub + incSub);
            sig += (subSine ? fastSin01(s.phSub)
                            : (s.phSub < 0.5f ? 1.0f : -1.0f)) * lsb;
        }
        if (lnz > 0.001f) sig += rng.bipolar() * lnz;
        out[i] = sig;
    }
}

}  // namespace synth
