// SynthCard - PLUCK engine: Karplus-Strong with a body resonator.
//
// The delay lines come from a shared pool. Putting a line inside EngineState
// would cost every voice ~1 KB whether or not pluck is in use; four pooled
// lines cover any realistic chord and cost 4 KB total.
#include "engines.h"

namespace synth {

// ------------------------------------------------------------------- pool --
static int16_t     s_lines[kPluckLines][kPluckLen];
static PluckState* s_owner[kPluckLines] = {nullptr};
static uint32_t    s_ticket[kPluckLines] = {0};
static uint32_t    s_nextTicket = 1;

void pluckPoolReset() {
    for (int i = 0; i < kPluckLines; ++i) { s_owner[i] = nullptr; s_ticket[i] = 0; }
    s_nextTicket = 1;
}

int pluckLinesFree() {
    int n = 0;
    for (int i = 0; i < kPluckLines; ++i) if (!s_owner[i]) ++n;
    return n;
}

// Hands out a line, reclaiming the oldest if the pool is dry. A reclaimed
// voice has its buf nulled, renders silence and is retired by its amp
// envelope - abrupt, but it takes five simultaneous plucks to reach.
static int16_t* acquire(PluckState* st) {
    int best = -1;
    for (int i = 0; i < kPluckLines; ++i) {
        if (!s_owner[i]) { best = i; break; }
    }
    if (best < 0) {
        uint32_t oldest = 0xFFFFFFFFu;
        for (int i = 0; i < kPluckLines; ++i)
            if (s_ticket[i] < oldest) { oldest = s_ticket[i]; best = i; }
        if (s_owner[best]) s_owner[best]->buf = nullptr;
    }
    s_owner[best]  = st;
    s_ticket[best] = s_nextTicket++;
    return s_lines[best];
}

void pluckRelease(PluckState& s) {
    for (int i = 0; i < kPluckLines; ++i)
        if (s_owner[i] == &s) { s_owner[i] = nullptr; s_ticket[i] = 0; }
    s.buf = nullptr;
}

// ------------------------------------------------------------------ voice --
void pluckNoteOn(PluckState& s, const Patch& pt, float hz, float vel, Rng& rng) {
    // Reuse the line we already hold rather than churning the pool on retrigger.
    bool held = false;
    for (int i = 0; i < kPluckLines; ++i) if (s_owner[i] == &s) held = true;
    if (!held || !s.buf) s.buf = acquire(&s);

    const float period = kSampleRate / clampf(hz, 30.0f, 4000.0f);
    int len = (int)period - 1;
    if (len < 4) len = 4;
    if (len > kPluckLen - 2) len = kPluckLen - 2;
    s.len  = len;
    // Allpass coefficient for the leftover fractional delay, so tuning is
    // exact rather than quantised to whole samples.
    const float frac = period - (float)(len + 1);
    s.frac = (1.0f - frac) / (1.0f + frac);
    s.ap   = 0.0f;
    s.lp   = 0.0f;
    s.wr   = 0;

    const uint8_t mode = pt.get(PP_EXCITE);
    const float bright = pt.norm(PP_BRIGHT);
    const float pick   = pt.norm(PP_PICK);
    s.damp    = 0.02f + pt.norm(PP_DAMP) * 0.55f;
    s.bodyAmt = pt.norm(PP_BODY);
    s.body1 = s.body2 = 0.0f;
    s.bodyFilt.reset();
    s.bodyFilt.setCoeffs(clampf(220.0f + pt.norm(PP_SPREAD) * 900.0f, 100.0f, 4000.0f), 2.4f);
    s.bow = (mode == 3) ? (0.02f + bright * 0.05f) : 0.0f;

    // Excitation. A one-pole lowpass sets brightness; the comb at the pick
    // position notches out the harmonic whose node sits under the pick, which
    // is what makes a plucked string sound plucked rather than struck.
    const int pickOff = 1 + (int)(pick * (float)(len - 2));
    float lp = 0.0f;
    const float lpCoef = 0.08f + bright * 0.9f;
    const int burst = (mode == 2) ? (len / 5 + 1) : len;   // MALLET: short burst
    for (int i = 0; i < len; ++i) {
        float x = (i < burst) ? rng.bipolar() : 0.0f;
        lp += (x - lp) * lpCoef;
        s.buf[i] = (int16_t)(clampf(lp, -1.0f, 1.0f) * 26000.0f);
    }
    if (mode == 1 || mode == 2) {
        for (int i = len - 1; i >= pickOff; --i)
            s.buf[i] = (int16_t)((s.buf[i] - s.buf[i - pickOff]) / 2);
    }
    const float g = clampf(0.35f + vel * 0.65f, 0.0f, 1.0f);
    if (g < 0.999f)
        for (int i = 0; i < len; ++i) s.buf[i] = (int16_t)((float)s.buf[i] * g);
}

void pluckRender(PluckState& s, const EngineCtx& c, float* out, int n) {
    if (!s.buf) { for (int i = 0; i < n; ++i) out[i] = 0.0f; return; }
    const float damp = s.damp;
    const float a    = s.frac;
    const float bodyAmt = s.bodyAmt;
    const int   len  = s.len;
    Rng& rng = *c.rng;

    for (int i = 0; i < n; ++i) {
        const int rd = s.wr;                      // reading before writing == len delay
        float x = (float)s.buf[rd] * (1.0f / 32768.0f);

        // Fractional delay: one-pole allpass.
        const float y = a * x + s.ap;
        s.ap = x - a * y;

        // Loop damping: the string loses its highs first.
        s.lp += (y - s.lp) * (1.0f - damp);
        float fed = s.lp;
        if (s.bow > 0.0f) fed += rng.bipolar() * s.bow;   // BOW keeps feeding energy

        s.buf[rd] = (int16_t)(clampf(fed, -1.0f, 1.0f) * 32767.0f);
        s.wr = (s.wr + 1 >= len) ? 0 : s.wr + 1;

        float sig = y;
        if (bodyAmt > 0.001f)
            sig = lerpf(sig, s.bodyFilt.process(sig, 3) * 2.2f, bodyAmt * 0.6f);
        out[i] = sig;
    }
}

}  // namespace synth
