// SynthCard - WAVETABLE engine: 16 mip-mapped tables with morph and warp.
//
// The mip level is chosen at note-on from the fundamental, so the harmonic
// content is always band-limited for the note being played. That is the whole
// reason this engine does not fizz at the top of the keyboard.
#include "engines.h"
#include "wavetables.h"

namespace synth {

// Highest harmonic each level carries. Level L holds kWtSize[L]/2 harmonics.
static inline uint8_t pickMip(float hz) {
    if (hz < 1.0f) hz = 1.0f;
    const float limit = kSampleRate * 0.45f;
    for (uint8_t L = 0; L < kWtLevels; ++L)
        if ((float)(kWtSize[L] / 2) * hz < limit) return L;
    return kWtLevels - 1;
}

// Interpolation inside one mip level of one table.
//
// The small levels are Catmull-Rom rather than linear. Linear interpolation of
// a 16- or 32-sample table is itself a distortion, and it was putting ~28 % of
// the top-octave energy off-harmonic - the aliasing the mip-maps exist to
// prevent, reintroduced by the reader. The large tables stay linear because
// there the error is already inaudible and this runs per sample per voice.
static inline float tableRead(int table, uint8_t mip, float ph) {
    const int size = kWtSize[mip];
    const int16_t* t = &kWaveTables[table][kWtOffset[mip]];
    const float x = ph * (float)size;
    int i1 = (int)x;
    if (i1 >= size) i1 = size - 1;
    const float fr = x - (float)i1;
    const int mask = size - 1;                 // every level is a power of two
    const int i2 = (i1 + 1) & mask;

    if (size > 64) return lerpf((float)t[i1], (float)t[i2], fr) * (1.0f / 32768.0f);

    const int i0 = (i1 - 1) & mask;
    const int i3 = (i1 + 2) & mask;
    const float y0 = (float)t[i0], y1 = (float)t[i1];
    const float y2 = (float)t[i2], y3 = (float)t[i3];
    const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c = -0.5f * y0 + 0.5f * y2;
    return (((a * fr + b) * fr + c) * fr + y1) * (1.0f / 32768.0f);
}

// CZ-style phase distortion: a two-segment bend that moves the waveform's
// midpoint, which brightens or softens without changing the fundamental.
static inline float warpPhase(float ph, float bp) {
    return ph < bp ? (ph * 0.5f / bp)
                   : (0.5f + (ph - bp) * 0.5f / (1.0f - bp));
}

void wtNoteOn(WtState& s, const Patch& pt, float inc, Rng& rng) {
    s.ph    = 0.0f;
    s.ph2   = rng.unipolar() * 0.02f;
    s.phSub = 0.0f;
    s.mip   = pickMip(inc * kSampleRate);
    const float semi = (float)pt.get(PW_O2_SEMI) - 24.0f;
    const float cent = pt.bip(PW_O2_DETUNE) * 0.5f;
    s.rat2 = powf(2.0f, (semi + cent) / 12.0f);
}

void wtRender(WtState& s, const EngineCtx& c, float* out, int n) {
    const Patch& pt = *c.pt;
    const int   ta  = pt.get(PW_TABLE) % kWtTables;
    const int   tb  = (ta + 1) % kWtTables;
    // MORPH is driven by c.shape so the LFO can sweep it.
    const float mix = clampf(c.shape, 0.0f, 1.0f);
    const float warp = pt.bip(PW_WARP);
    const float bp = clampf(0.5f - warp * 0.4f, 0.08f, 0.92f);
    const bool  doWarp = warp < -0.01f || warp > 0.01f;
    const float l2  = pt.norm(PW_O2_LEVEL);
    const float lsb = pt.norm(PW_SUB_LEVEL);
    const float lnz = pt.norm(PW_NOISE);
    Rng& rng = *c.rng;

    const float inc2   = clampf(c.inc * s.rat2, 0.0f, 0.49f);
    const float incSub = clampf(c.inc * 0.5f,   0.0f, 0.49f);
    const uint8_t mip = s.mip;

    for (int i = 0; i < n; ++i) {
        s.ph = wrap01(s.ph + c.inc);
        const float ph = doWarp ? warpPhase(s.ph, bp) : s.ph;
        float sig = mix < 0.001f ? tableRead(ta, mip, ph)
                                 : lerpf(tableRead(ta, mip, ph), tableRead(tb, mip, ph), mix);
        if (l2 > 0.001f) {
            s.ph2 = wrap01(s.ph2 + inc2);
            sig += (2.0f * s.ph2 - 1.0f - polyBlep(s.ph2, inc2)) * l2;
        }
        if (lsb > 0.001f) {
            s.phSub = wrap01(s.phSub + incSub);
            sig += fastSin01(s.phSub) * lsb;
        }
        if (lnz > 0.001f) sig += rng.bipolar() * lnz;
        out[i] = sig;
    }
}

}  // namespace synth
