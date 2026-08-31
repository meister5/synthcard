// SynthCard - ORGAN engine: nine additive drawbars at Hammond footages.
//
// Additive is the one thing a subtractive engine genuinely cannot fake, and it
// is cheap: nine sines and no filter. Partials that would land above Nyquist
// are muted at note-on, so the top of the keyboard stays clean instead of
// folding harmonics back down.
#include "engines.h"

namespace synth {

// 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'
static const float kFootage[kDrawbars] =
    {0.5f, 1.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 8.0f};

static const uint8_t kBarParam[kDrawbars] =
    {PO_D1, PO_D2, PO_D3, PO_D4, PO_D5, PO_D6, PO_D7, PO_D8, PO_D9};

void organNoteOn(OrganState& s, const Patch& pt, float hz, Rng& rng) {
    const float limit = kSampleRate * 0.45f;
    float sum = 0.0f;
    for (int i = 0; i < kDrawbars; ++i) {
        s.ratio[i] = kFootage[i];
        const float lvl = (float)pt.get(kBarParam[i]) * (1.0f / 8.0f);
        s.lvl[i] = (hz * kFootage[i] < limit) ? lvl : 0.0f;
        sum += s.lvl[i];
        // Hammond tone wheels are not phase-locked; a fixed scatter per
        // drawbar avoids a single fat transient on every note.
        s.ph[i] = wrap01(0.13f * (float)i + rng.unipolar() * 0.05f);
    }
    s.norm = sum > 0.001f ? 1.0f / sum : 0.0f;

    // Key click: the real thing is a contact transient, not a filtered noise
    // burst, so it is a fast-decaying impulse rather than a percussion voice.
    s.click     = pt.norm(PO_CLICK);
    s.clickCoef = expf(-6.9f / (0.004f * kSampleRate));      // 4 ms
    s.rotPh     = 0.0f;
}

void organRender(OrganState& s, const EngineCtx& c, float* out, int n) {
    const Patch& pt = *c.pt;
    const float rot = pt.norm(PO_ROTARY);
    // One slow rotary: a little vibrato and a little tremolo, in antiphase,
    // which is most of what a rotating speaker does to a mono signal.
    const float rotInc = 6.4f * kInvSampleRate;
    const float rotDepth = rot * 0.004f;
    const float tremDepth = rot * 0.22f;
    Rng& rng = *c.rng;

    float inc[kDrawbars];
    for (int i = 0; i < kDrawbars; ++i)
        inc[i] = clampf(c.inc * s.ratio[i], 0.0f, 0.49f);

    for (int i = 0; i < n; ++i) {
        float rotMod = 0.0f, trem = 1.0f;
        if (rot > 0.001f) {
            s.rotPh = wrap01(s.rotPh + rotInc);
            const float lfo = fastSin01(s.rotPh);
            rotMod = lfo * rotDepth;
            trem   = 1.0f - tremDepth * (0.5f - 0.5f * lfo);
        }
        float sig = 0.0f;
        for (int d = 0; d < kDrawbars; ++d) {
            if (s.lvl[d] <= 0.0f) continue;
            s.ph[d] = wrap01(s.ph[d] + inc[d] * (1.0f + rotMod));
            sig += fastSin01(s.ph[d]) * s.lvl[d];
        }
        sig *= s.norm * trem;

        if (s.click > 0.0005f) {
            sig += rng.bipolar() * s.click * 0.7f;
            s.click *= s.clickCoef;
        }
        out[i] = sig;
    }
}

}  // namespace synth
