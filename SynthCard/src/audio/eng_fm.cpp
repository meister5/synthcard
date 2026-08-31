// SynthCard - FM engine: four operators, eight algorithms, op-1 feedback.
//
// Each operator has its own attack/decay envelope, stepped once per control
// chunk. An operator's decay settles at its level scaled by the patch's amp
// sustain, so a plucky patch gets a decaying timbre and a sustained one holds
// its timbre - without spending four more parameters on operator sustain.
#include "engines.h"

namespace synth {

// mod[i] is a bitmask of the operators that modulate operator i.
// carrier is a bitmask of the operators that reach the output.
struct FmAlgo { uint8_t mod[4], carrier; };

static const FmAlgo kAlgos[8] = {
    // CHAIN   4 -> 3 -> 2 -> 1
    {{0x2, 0x4, 0x8, 0x0}, 0x1},
    // STACK   (2 + 3) -> 1, 4 -> 3
    {{0x6, 0x0, 0x8, 0x0}, 0x1},
    // TRIMOD  (2 + 3 + 4) -> 1
    {{0xE, 0x0, 0x0, 0x0}, 0x1},
    // TWIN    2 -> 1, 4 -> 3, both carry
    {{0x2, 0x0, 0x8, 0x0}, 0x5},
    // 3+1     3 -> 2 -> 1, plus 4 bare
    {{0x2, 0x4, 0x0, 0x0}, 0x9},
    // FAN     2 -> 1, (3 + 4) -> 2
    {{0x2, 0xC, 0x0, 0x0}, 0x1},
    // 1+3+4   2 -> 1, 3 and 4 bare
    {{0x2, 0x0, 0x0, 0x0}, 0xD},
    // ADD     four bare sines
    {{0x0, 0x0, 0x0, 0x0}, 0xF},
};

void fmNoteOn(FmState& s, const Patch& pt, Rng& rng) {
    const float sus = pt.norm(P_AMP_S);
    const uint8_t rat[4] = {PF_R1, PF_R2, PF_R3, PF_R4};
    const uint8_t lvl[4] = {PF_L1, PF_L2, PF_L3, PF_L4};
    const uint8_t atk[4] = {PF_A1, PF_A2, PF_A3, PF_A4};
    const uint8_t dec[4] = {PF_D1, PF_D2, PF_D3, PF_D4};

    for (int i = 0; i < 4; ++i) {
        s.op[i].ph  = (i == 0) ? 0.0f : rng.unipolar() * 0.02f;
        s.op[i].out = 0.0f;
        s.lvl[i] = pt.norm(lvl[i]);
        s.inc[i] = fmRatio(pt.get(rat[i]));

        const float aMs = envMsFor(pt.get(atk[i]));
        const float dMs = envMsFor(pt.get(dec[i]));
        const float rate = kSampleRate / (float)kCtrlChunk;
        s.envAtk[i] = 1.0f / (clampf(aMs, 0.5f, 20000.0f) * 0.001f * rate);
        s.envDec[i] = clampf(1.0f - expf(-6.9f / (clampf(dMs, 1.0f, 20000.0f) * 0.001f * rate)),
                             0.0f, 1.0f);
        s.envSus[i] = sus;
        s.envLevel[i] = 0.0f;
        s.stage[i] = 0;
    }
    s.fb = pt.norm(PF_FB) * 2.0f;
}

// One control-chunk step of all four operator envelopes.
static inline void stepEnvs(FmState& s) {
    for (int i = 0; i < 4; ++i) {
        if (s.stage[i] == 0) {
            s.envLevel[i] += s.envAtk[i];
            if (s.envLevel[i] >= 1.0f) { s.envLevel[i] = 1.0f; s.stage[i] = 1; }
        } else {
            s.envLevel[i] += (s.envSus[i] - s.envLevel[i]) * s.envDec[i];
        }
    }
}

void fmRender(FmState& s, const EngineCtx& c, float* out, int n) {
    const Patch& pt = *c.pt;
    const FmAlgo& al = kAlgos[pt.get(PF_ALGO) & 7];

    stepEnvs(s);

    float amp[4], inc[4];
    for (int i = 0; i < 4; ++i) {
        amp[i] = s.lvl[i] * s.envLevel[i];
        inc[i] = clampf(c.inc * s.inc[i], 0.0f, 0.49f);
    }
    // Only carriers are normalised; a four-carrier algorithm must not be four
    // times louder than a single-carrier one.
    int carriers = 0;
    for (int i = 0; i < 4; ++i) if (al.carrier & (1 << i)) ++carriers;
    const float cGain = carriers > 1 ? 1.0f / sqrtf((float)carriers) : 1.0f;

    for (int i = 0; i < n; ++i) {
        // Operators are evaluated 4 -> 1 so a modulator is always fresh by the
        // time its carrier reads it; feedback on op 1 uses last sample's value.
        for (int o = 3; o >= 0; --o) {
            float mod = 0.0f;
            const uint8_t m = al.mod[o];
            if (m & 0x1) mod += s.op[0].out;
            if (m & 0x2) mod += s.op[1].out;
            if (m & 0x4) mod += s.op[2].out;
            if (m & 0x8) mod += s.op[3].out;
            if (o == 0 && s.fb > 0.001f) mod += s.op[0].out * s.fb;
            s.op[o].ph = wrap01(s.op[o].ph + inc[o]);
            s.op[o].out = fastSin01(wrap01(s.op[o].ph + mod)) * amp[o];
        }
        float sig = 0.0f;
        if (al.carrier & 0x1) sig += s.op[0].out;
        if (al.carrier & 0x2) sig += s.op[1].out;
        if (al.carrier & 0x4) sig += s.op[2].out;
        if (al.carrier & 0x8) sig += s.op[3].out;
        out[i] = sig * cGain;
    }
}

}  // namespace synth
