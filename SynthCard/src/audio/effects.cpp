#include "effects.h"
#include <stdio.h>
#include <string.h>

namespace synth {

const FxInfo kFxInfo[FX_COUNT] = {
    {"DLY TIME",  48, 127, FD_MS},
    {"DLY SYNC",   0, kDlySyncCount - 1, FD_SYNC},
    {"DLY FB",    50, 127, FD_PCT},
    {"DLY MIX",    0, 127, FD_PCT},
    {"REV SIZE",  70, 127, FD_PCT},
    {"REV DAMP",  50, 127, FD_PCT},
    {"REV MIX",    0, 127, FD_PCT},
    {"CHO RATE",  40, 127, FD_PCT},
    {"CHO DEPTH", 60, 127, FD_PCT},
    {"CHO MIX",    0, 127, FD_PCT},
    {"TILT",      64, 127, FD_TILT},
    {"COMP",      30, 127, FD_PCT},
    {"DRIVE",      0, 127, FD_PCT},
    {"DRIVE MIX", 90, 127, FD_PCT},
    {"MASTER",    96, 127, FD_PCT},
};

const char* const kDlySyncNames[kDlySyncCount] =
    {"FREE", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2"};

// Multiples of a quarter note. Index 0 is unused (free running).
static const float kDlySyncMul[kDlySyncCount] =
    {0.0f, 0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 1.0f, 2.0f};

const char* const kOutModeNames[OUT_MODE_COUNT] = {"SPEAKER", "LINE"};

void formatFx(const FxSettings& fx, uint8_t id, char* buf, int len) {
    if (id >= FX_COUNT || len < 2) { if (len) buf[0] = 0; return; }
    const int v = fx.p[id];
    switch (kFxInfo[id].disp) {
        case FD_MS:
            if (fx.p[FX_DLY_SYNC] != 0) snprintf(buf, len, "%s", kDlySyncNames[fx.p[FX_DLY_SYNC]]);
            else snprintf(buf, len, "%dms", 15 + (int)(v * (485.0f / 127.0f)));
            break;
        case FD_SYNC:
            snprintf(buf, len, "%s", kDlySyncNames[v < kDlySyncCount ? v : 0]);
            break;
        case FD_TILT: {
            // Bipolar: negative is bass-heavy, positive is bright.
            const int t = (v - 64) * 100 / 64;
            if (t == 0) snprintf(buf, len, "FLAT");
            else        snprintf(buf, len, "%+d", t);
            break;
        }
        default:
            snprintf(buf, len, "%d%%", (v * 100 + 63) / 127);
            break;
    }
}

void Effects::init() {
    memset(delay_, 0, sizeof(delay_));
    memset(diff_, 0, sizeof(diff_));
    memset(tApA_, 0, sizeof(tApA_));
    memset(tApB_, 0, sizeof(tApB_));
    memset(tDlyA_, 0, sizeof(tDlyA_));
    memset(tDlyB_, 0, sizeof(tDlyB_));
    memset(chorus_, 0, sizeof(chorus_));
    dWrite_ = cIdx_ = 0;
    dPrev_ = dCur_ = dIn_ = 0.0f;
    dPhase_ = 0;
    for (int i = 0; i < kDiffCount; ++i) diffIdx_[i] = 0;
    apAIdx_ = apBIdx_ = dlyAIdx_ = dlyBIdx_ = 0;
    tank_ = 0.0f;
    tiltLp_ = 0.0f;
    rDampLp_ = 0.0f;
    compEnv_ = 0.0f;
    compGain_ = compTarget_ = 1.0f;
    hpX1_ = hpY1_ = hpX2_ = hpY2_ = 0.0f;
    limGain_ = 1.0f;
}

void Effects::applySettings(const FxSettings& s, uint16_t bpm, uint8_t outMode) {
    // ---- delay ----
    const uint8_t sync = s.get(FX_DLY_SYNC);
    float ms;
    if (sync != 0 && bpm >= 40) {
        // A quarter note is 60000/bpm milliseconds.
        ms = kDlySyncMul[sync] * 60000.0f / (float)bpm;
    } else {
        ms = 15.0f + s.p[FX_DLY_TIME] * (485.0f / 127.0f);
    }
    // The line runs at half rate, so a millisecond costs half a sample.
    dLen_ = clampi((int)(ms * 0.001f * kSampleRate / kDelayRateDiv), 16, kDelayMax - 1);
    dFb_  = s.norm(FX_DLY_FB) * 0.92f;
    dMix_ = s.norm(FX_DLY_MIX);

    // ---- reverb ----
    const float size = s.norm(FX_REV_SIZE);
    // The tank's loop gain is the decay time. Capped short of unity so the
    // longest setting is a big room rather than a tail that never ends.
    rDecay_ = 0.30f + size * 0.62f;
    rDamp_  = s.norm(FX_REV_DAMP) * 0.75f;
    rMix_   = s.norm(FX_REV_MIX);
    // SIZE shortens the tank delays rather than the feedback alone, so a small
    // room is genuinely small rather than merely short.
    dlyALen_ = clampi((int)(kTankDlyA * (0.45f + size * 0.55f)), 64, kTankDlyA);
    dlyBLen_ = clampi((int)(kTankDlyB * (0.45f + size * 0.55f)), 64, kTankDlyB);
    if (dlyAIdx_ >= dlyALen_) dlyAIdx_ = 0;
    if (dlyBIdx_ >= dlyBLen_) dlyBIdx_ = 0;

    // ---- chorus ----
    cInc_   = (0.08f + s.norm(FX_CHO_RATE) * 3.5f) * kInvSampleRate;
    cDepth_ = s.norm(FX_CHO_DEPTH) * 180.0f;
    cMix_   = s.norm(FX_CHO_MIX);

    // ---- tone ----
    // One knob, two shelves moving in opposition. 64 is flat.
    const float tilt = s.norm(FX_TILT) * 2.0f - 1.0f;
    tiltLow_  = fastExp2(-tilt * 0.9f);
    tiltHigh_ = fastExp2( tilt * 0.9f);

    compAmt_ = s.norm(FX_COMP);
    drive_   = s.norm(FX_DRIVE);
    drvMix_  = s.norm(FX_DRV_MIX);
    master_  = s.norm(FX_MASTER);
    speakerMode_ = (outMode == OUT_SPEAKER);
}

static inline int16_t toI16(float v) {
    float x = v * 32767.0f;
    if (x >  32767.0f) x =  32767.0f;
    if (x < -32768.0f) x = -32768.0f;
    return (int16_t)x;
}
static constexpr float kFromI16 = 1.0f / 32768.0f;

// One allpass stage over an int16 line.
//
// The feedback term must be taken from the value WRITTEN to the line, not from
// the stage's input. Using the input gives H = (-g + (1+g^2)z^-N)/(1 - g z^-N),
// whose gain at DC is (1 - g + g^2)/(1 - g) - about 2.0 at g = 0.62. That is
// not an allpass at all, and a tank built from them does not decay: it
// oscillates. The previous Schroeder reverb had the same mistake, which is a
// large part of why it rang.
static inline float allpass(int16_t* buf, int& idx, int len, float x, float g) {
    const float b = buf[idx] * kFromI16;
    const float v = x + g * b;
    buf[idx] = toI16(clampf(v, -1.9f, 1.9f));
    if (++idx >= len) idx = 0;
    return -g * v + b;
}

// The reverb tank: two allpasses and two delays in a loop, with the first
// allpass modulated. Modulating it is the whole point - a static tank has
// fixed resonances that ring on every hit, which is what made the previous
// four-comb Schroeder reverb sound metallic.
float Effects::tankStep(float x) {
    modPh_ = wrap01(modPh_ + 0.7f * kInvSampleRate);
    const float mod = fastSin01(modPh_) * 8.0f;

    float t = x + tank_ * rDecay_;

    // Modulated allpass: read at a fractional offset from the write point.
    {
        float rpos = (float)apAIdx_ - mod;
        while (rpos < 0.0f) rpos += (float)kTankApA;
        while (rpos >= (float)kTankApA) rpos -= (float)kTankApA;
        const int ri = (int)rpos;
        const float fr = rpos - (float)ri;
        const int r2 = (ri + 1 >= kTankApA) ? 0 : ri + 1;
        const float bufv = lerpf(tApA_[ri] * kFromI16, tApA_[r2] * kFromI16, fr);
        const float v = t + 0.62f * bufv;
        tApA_[apAIdx_] = toI16(clampf(v, -1.9f, 1.9f));
        if (++apAIdx_ >= kTankApA) apAIdx_ = 0;
        t = -0.62f * v + bufv;
    }

    // Delay A, then damping, then the second allpass and delay B.
    {
        const float y = tDlyA_[dlyAIdx_] * kFromI16;
        tDlyA_[dlyAIdx_] = toI16(clampf(t, -1.9f, 1.9f));
        if (++dlyAIdx_ >= dlyALen_) dlyAIdx_ = 0;
        t = y;
    }
    // Damping: the tail loses its highs as it circulates, which is what a
    // real plate does and what stops a long reverb turning into hiss.
    rDampLp_ += (t - rDampLp_) * (1.0f - rDamp_);
    t = rDampLp_;

    t = allpass(tApB_, apBIdx_, kTankApB, t, 0.55f);

    const float tap = t;
    {
        const float y = tDlyB_[dlyBIdx_] * kFromI16;
        tDlyB_[dlyBIdx_] = toI16(clampf(t, -1.9f, 1.9f));
        if (++dlyBIdx_ >= dlyBLen_) dlyBIdx_ = 0;
        t = y;
    }
    tank_ = t;
    // Two taps rather than one: a single tap gives an audibly periodic tail.
    return tap * 0.6f + t * 0.5f;
}

void Effects::process(float* dry, const float* dlySend, const float* revSend, int n) {
    // Anything decaying into denormal range costs more than the whole rest of
    // the mix on an FPU that traps denormals; see flushDenormal.
    dLp_     = flushDenormal(dLp_);
    tank_    = flushDenormal(tank_);
    tiltLp_  = flushDenormal(tiltLp_);
    rDampLp_ = flushDenormal(rDampLp_);
    compEnv_ = flushDenormal(compEnv_);
    hpY1_    = flushDenormal(hpY1_);
    hpY2_    = flushDenormal(hpY2_);
    limGain_ = flushDenormal(limGain_);

    float pk = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float in = dry[i];

        // ---- delay, clocked at half rate --------------------------------
        // The input is averaged across the two samples of each tick, which is
        // the anti-alias filter; the output is interpolated back up.
        dIn_ += dlySend[i];
        if (++dPhase_ >= kDelayRateDiv) {
            dPhase_ = 0;
            int32_t rd = dWrite_ - dLen_;
            if (rd < 0) rd += kDelayMax;
            const float rv = delay_[rd] * kFromI16;
            dLp_ += (rv - dLp_) * 0.45f;
            delay_[dWrite_] = toI16(clampf(dIn_ * (1.0f / kDelayRateDiv) + dLp_ * dFb_, -1.6f, 1.6f));
            if (++dWrite_ >= kDelayMax) dWrite_ = 0;
            dPrev_ = dCur_;
            dCur_  = rv;
            dIn_   = 0.0f;
        }
        const float d = lerpf(dPrev_, dCur_, (float)dPhase_ / (float)kDelayRateDiv);

        // ---- reverb: four input diffusers, then the tank -----------------
        float rv = 0.0f;
        if (rMix_ > 0.001f) {
            float rin = revSend[i] + d * dMix_ * 0.35f;
            rin *= 0.35f;
            // Diffusing the input before the tank is what turns a handful of
            // discrete echoes into something that sounds like a room.
            for (int c = 0; c < kDiffCount; ++c)
                rin = allpass(diff_[c], diffIdx_[c], kDiffLen[c], rin, c < 2 ? 0.75f : 0.62f);
            rv = tankStep(rin);
        }

        float s = in + d * dMix_ + rv * rMix_ * 0.55f;

        // ---- chorus ------------------------------------------------------
        if (cMix_ > 0.001f) {
            cPhase_ = wrap01(cPhase_ + cInc_);
            const float mod  = 200.0f + cDepth_ * (fastSin01(cPhase_) * 0.5f + 0.5f);
            float rpos = (float)cIdx_ - mod;
            while (rpos < 0.0f) rpos += kChorusLen;
            const int   ri = (int)rpos;
            const float fr = rpos - (float)ri;
            const int   r2 = (ri + 1) % kChorusLen;
            const float cv = lerpf(chorus_[ri] * kFromI16, chorus_[r2] * kFromI16, fr);
            chorus_[cIdx_] = toI16(clampf(s, -1.8f, 1.8f));
            if (++cIdx_ >= kChorusLen) cIdx_ = 0;
            s = s * (1.0f - cMix_ * 0.5f) + cv * cMix_;
        }

        // ---- speaker conditioning ----------------------------------------
        // Two one-pole high-passes at ~120 Hz. Energy the speaker cannot
        // reproduce is removed before the limiter sees it, so a kick no longer
        // spends the whole mix's headroom on something inaudible.
        if (speakerMode_) {
            const float a = 0.9765f;                 // ~120 Hz at 32 kHz
            const float y1 = a * (hpY1_ + s - hpX1_);
            hpX1_ = s; hpY1_ = y1;
            const float y2 = a * (hpY2_ + y1 - hpX2_);
            hpX2_ = y1; hpY2_ = y2;
            s = y2;
        }

        // ---- tilt --------------------------------------------------------
        // One-pole split at ~700 Hz, the two halves scaled in opposition.
        tiltLp_ += (s - tiltLp_) * 0.125f;
        s = tiltLp_ * tiltLow_ + (s - tiltLp_) * tiltHigh_;

        // ---- drive -------------------------------------------------------
        if (drive_ > 0.001f) {
            const float wet = softClip(s * (1.0f + drive_ * 24.0f)) * (1.0f - drive_ * 0.5f);
            s = lerpf(s, wet, drvMix_);
        }

        // ---- compressor --------------------------------------------------
        // Soft knee, 3:1, ~50 ms release. This is what glues the drums
        // together; the limiter below is a safety net, not a sound.
        if (compAmt_ > 0.001f) {
            const float a = s < 0.0f ? -s : s;
            if (a > compEnv_) compEnv_ += (a - compEnv_) * 0.25f;      // ~0.1 ms attack
            else              compEnv_ += (a - compEnv_) * 0.0006f;    // ~50 ms release
            const float thr = 0.30f;
            if (compEnv_ > thr) {
                const float over = compEnv_ / thr;
                // 3:1 above the threshold, faded in by the COMP amount.
                compTarget_ = 1.0f / (1.0f + (over - 1.0f) * 0.667f);
                compTarget_ = lerpf(1.0f, compTarget_, compAmt_);
            } else {
                compTarget_ = 1.0f;
            }
            compGain_ += (compTarget_ - compGain_) * 0.02f;
            s *= compGain_;
        } else {
            compGain_ = 1.0f;
        }

        s *= master_;

        // ---- peak limiter: fast attack, slow release, no lookahead -------
        const float a = s < 0.0f ? -s : s;
        if (a > pk) pk = a;
        const float need = (a * limGain_ > 0.97f) ? (0.97f / a) : 1.0f;
        if (need < limGain_) limGain_ += (need - limGain_) * 0.35f;
        else                 limGain_ += (need - limGain_) * 0.0015f;
        s *= limGain_;

        dry[i] = clampf(s, -1.0f, 1.0f);
    }
    peak_ = pk;
}

}  // namespace synth
