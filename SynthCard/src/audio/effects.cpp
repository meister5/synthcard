#include "effects.h"
#include <stdio.h>
#include <string.h>

namespace synth {

const FxInfo kFxInfo[FX_COUNT] = {
    {"DLY TIME", 48, 1}, {"DLY FB", 50, 0}, {"DLY MIX", 0, 0},
    {"REV SIZE", 70, 0}, {"REV DAMP", 50, 0}, {"REV MIX", 0, 0},
    {"CHO RATE", 40, 0}, {"CHO DEPTH", 60, 0}, {"CHO MIX", 0, 0},
    {"DRIVE", 0, 0}, {"DRIVE MIX", 90, 0},
    {"MASTER", 96, 0},
};

void formatFx(const FxSettings& fx, uint8_t id, char* buf, int len) {
    if (id >= FX_COUNT || len < 2) { if (len) buf[0] = 0; return; }
    if (kFxInfo[id].disp == 1) {           // milliseconds
        int ms = 15 + (int)(fx.p[id] * (485.0f / 127.0f));
        snprintf(buf, len, "%dms", ms);
    } else {
        snprintf(buf, len, "%d%%", (fx.p[id] * 100 + 63) / 127);
    }
}

void Effects::init() {
    memset(delay_, 0, sizeof(delay_));
    memset(comb_, 0, sizeof(comb_));
    memset(ap_, 0, sizeof(ap_));
    memset(chorus_, 0, sizeof(chorus_));
    dWrite_ = cIdx_ = 0;
    for (int i = 0; i < kCombCount; ++i) { combIdx_[i] = 0; combStore_[i] = 0.0f; }
    for (int i = 0; i < kApCount; ++i) apIdx_[i] = 0;
    limGain_ = 1.0f;
}

void Effects::applySettings(const FxSettings& s) {
    int ms = 15 + (int)(s.p[FX_DLY_TIME] * (485.0f / 127.0f));
    dLen_  = clampi((int)(ms * 0.001f * kSampleRate), 32, kDelayMax - 1);
    dFb_   = s.norm(FX_DLY_FB) * 0.92f;
    dMix_  = s.norm(FX_DLY_MIX);

    float size = s.norm(FX_REV_SIZE);
    rFb_   = 0.70f + size * 0.28f;
    rDamp_ = s.norm(FX_REV_DAMP) * 0.75f;
    rMix_  = s.norm(FX_REV_MIX);
    for (int i = 0; i < kCombCount; ++i) {
        static const int base[kCombCount] = {809, 863, 927, 983};
        combLen_[i] = clampi((int)(base[i] * (0.55f + size * 0.45f)), 64, 1023);
        if (combIdx_[i] >= combLen_[i]) combIdx_[i] = 0;
    }

    cInc_   = (0.08f + s.norm(FX_CHO_RATE) * 3.5f) * kInvSampleRate;
    cDepth_ = s.norm(FX_CHO_DEPTH) * 180.0f;
    cMix_   = s.norm(FX_CHO_MIX);

    drive_  = s.norm(FX_DRIVE);
    drvMix_ = s.norm(FX_DRV_MIX);
    master_ = s.norm(FX_MASTER);
}

static inline int16_t toI16(float v) {
    float x = v * 32767.0f;
    if (x >  32767.0f) x =  32767.0f;
    if (x < -32768.0f) x = -32768.0f;
    return (int16_t)x;
}
static constexpr float kFromI16 = 1.0f / 32768.0f;

void Effects::process(float* dry, const float* dlySend, const float* revSend, int n) {
    float pk = 0.0f;
    for (int i = 0; i < n; ++i) {
        float in = dry[i];

        // ---- delay (feedback line, one-pole damped) ----
        int32_t rd = dWrite_ - dLen_;
        if (rd < 0) rd += kDelayMax;
        float d = delay_[rd] * kFromI16;
        dLp_ += (d - dLp_) * 0.45f;
        delay_[dWrite_] = toI16(clampf(dlySend[i] + dLp_ * dFb_, -1.6f, 1.6f));
        if (++dWrite_ >= kDelayMax) dWrite_ = 0;

        // ---- reverb (4 damped combs -> 2 allpasses) ----
        float rin = revSend[i] + d * dMix_ * 0.35f;
        float rv = 0.0f;
        for (int c = 0; c < kCombCount; ++c) {
            float y = comb_[c][combIdx_[c]] * kFromI16;
            rv += y;
            combStore_[c] += (y - combStore_[c]) * (1.0f - rDamp_);
            comb_[c][combIdx_[c]] = toI16(clampf(rin * 0.22f + combStore_[c] * rFb_, -1.8f, 1.8f));
            if (++combIdx_[c] >= combLen_[c]) combIdx_[c] = 0;
        }
        for (int a = 0; a < kApCount; ++a) {
            float bufv = ap_[a][apIdx_[a]] * kFromI16;
            float out  = -rv + bufv;
            ap_[a][apIdx_[a]] = toI16(clampf(rv + bufv * 0.5f, -1.8f, 1.8f));
            if (++apIdx_[a] >= apLen_[a]) apIdx_[a] = 0;
            rv = out;
        }

        float s = in + d * dMix_ + rv * rMix_ * 0.6f;

        // ---- chorus ----
        if (cMix_ > 0.001f) {
            cPhase_ = wrap01(cPhase_ + cInc_);
            float mod  = 200.0f + cDepth_ * (fastSin01(cPhase_) * 0.5f + 0.5f);
            float rpos = (float)cIdx_ - mod;
            while (rpos < 0.0f) rpos += kChorusLen;
            int   ri = (int)rpos;
            float fr = rpos - (float)ri;
            int   r2 = (ri + 1) % kChorusLen;
            float cv = lerpf(chorus_[ri] * kFromI16, chorus_[r2] * kFromI16, fr);
            chorus_[cIdx_] = toI16(clampf(s, -1.8f, 1.8f));
            if (++cIdx_ >= kChorusLen) cIdx_ = 0;
            s = s * (1.0f - cMix_ * 0.5f) + cv * cMix_;
        }

        // ---- drive ----
        if (drive_ > 0.001f) {
            float wet = softClip(s * (1.0f + drive_ * 24.0f)) * (1.0f - drive_ * 0.5f);
            s = lerpf(s, wet, drvMix_);
        }

        s *= master_;

        // ---- peak limiter: fast attack, slow release, no lookahead ----
        float a = s < 0.0f ? -s : s;
        if (a > pk) pk = a;
        float need = (a * limGain_ > 0.97f) ? (0.97f / a) : 1.0f;
        if (need < limGain_) limGain_ += (need - limGain_) * 0.35f;
        else                 limGain_ += (need - limGain_) * 0.0015f;
        s *= limGain_;

        dry[i] = clampf(s, -1.0f, 1.0f);
    }
    peak_ = pk;
}

}  // namespace synth
