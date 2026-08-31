// SynthCard - master effects rack: delay, plate reverb, chorus, tilt,
// compressor, drive and a limiter.
//
// All delay lines are int16. The rack fits in ~27 KB, down from ~44 KB, with a
// better reverb than before - the delay line runs at half rate (same 500 ms for
// half the memory, and a darker tape-like character that suits a delay anyway)
// and the reverb's improvement comes from its topology rather than its size.
#pragma once
#include "dsp.h"

namespace synth {

enum FxParam : uint8_t {
    FX_DLY_TIME = 0, FX_DLY_SYNC, FX_DLY_FB, FX_DLY_MIX,
    FX_REV_SIZE, FX_REV_DAMP, FX_REV_MIX,
    FX_CHO_RATE, FX_CHO_DEPTH, FX_CHO_MIX,
    FX_TILT, FX_COMP,
    FX_DRIVE, FX_DRV_MIX,
    FX_MASTER,
    FX_COUNT
};

// Display kinds for formatFx.
enum FxDisp : uint8_t { FD_PCT = 0, FD_MS, FD_SYNC, FD_TILT };

struct FxInfo { const char* name; uint8_t def; uint8_t max; FxDisp disp; };
extern const FxInfo kFxInfo[FX_COUNT];

// Delay divisions, as a multiple of a quarter note. Index 0 is free-running.
enum { kDlySyncCount = 7 };
extern const char* const kDlySyncNames[kDlySyncCount];

struct FxSettings {
    uint8_t p[FX_COUNT];
    void reset() { for (uint8_t i = 0; i < FX_COUNT; ++i) p[i] = kFxInfo[i].def; }
    inline float norm(uint8_t i) const {
        if (i >= FX_COUNT) return 0.0f;
        const uint8_t m = kFxInfo[i].max;
        return m ? (float)p[i] / (float)m : 0.0f;
    }
    inline void set(uint8_t i, int v) {
        if (i < FX_COUNT) p[i] = (uint8_t)clampi(v, 0, kFxInfo[i].max);
    }
    inline uint8_t get(uint8_t i) const { return i < FX_COUNT ? p[i] : 0; }
};
void formatFx(const FxSettings& fx, uint8_t id, char* buf, int len);

// Output mode. The Cardputer's speaker reproduces almost nothing below
// ~200 Hz, but that inaudible energy still drives the limiter - so a big kick
// ducks the whole mix in exchange for something nobody can hear. SPEAKER
// high-passes the master and tilts to compensate; LINE leaves both alone.
enum OutMode : uint8_t { OUT_SPEAKER = 0, OUT_LINE, OUT_MODE_COUNT };
extern const char* const kOutModeNames[OUT_MODE_COUNT];

// The delay line runs at half rate: 500 ms costs 8000 samples instead of
// 16000, and the lost bandwidth is exactly what a delay is usually asked to
// throw away anyway.
constexpr int kDelayRateDiv = 2;
constexpr int kDelayMax     = 8000;          // 500 ms at 16 kHz

// Plate reverb sizes. Four input diffusers feed a tank of two allpasses and
// two delays; the tank allpasses are modulated, which is what stops the fixed
// comb resonances that made the old Schroeder reverb ring metallically.
constexpr int kDiffCount = 4;
constexpr int kDiffLen[kDiffCount] = {142, 107, 379, 277};
constexpr int kTankApA = 672, kTankDlyA = 1250;
constexpr int kTankApB = 908, kTankDlyB = 1300;

class Effects {
public:
    void init();
    // bpm is needed for the tempo-synced delay; outMode picks the speaker
    // conditioning. Both are cheap enough to re-apply every block.
    void applySettings(const FxSettings& s, uint16_t bpm = 120, uint8_t outMode = OUT_SPEAKER);
    // dry[] is modified in place. dlySend/revSend are additional send buses.
    void process(float* dry, const float* dlySend, const float* revSend, int n);
    inline float peak() const { return peak_; }
    inline float limiterGain() const { return limGain_; }
    inline float compGain() const { return compGain_; }

private:
    float tankStep(float x);

    // ---- delay, half rate ----
    int16_t delay_[kDelayMax];
    int32_t dWrite_ = 0, dLen_ = 4000;
    float   dFb_ = 0.3f, dMix_ = 0.0f, dLp_ = 0.0f;
    float   dPrev_ = 0.0f, dCur_ = 0.0f;     // for interpolating between ticks
    float   dIn_ = 0.0f;                     // 2-sample input average
    uint8_t dPhase_ = 0;

    // ---- plate reverb ----
    int16_t diff_[kDiffCount][379];
    int     diffIdx_[kDiffCount] = {0, 0, 0, 0};
    int16_t tApA_[kTankApA], tApB_[kTankApB];
    int16_t tDlyA_[kTankDlyA], tDlyB_[kTankDlyB];
    int     apAIdx_ = 0, apBIdx_ = 0, dlyAIdx_ = 0, dlyBIdx_ = 0;
    float   tank_ = 0.0f, rDamp_ = 0.3f, rDecay_ = 0.5f, rMix_ = 0.0f;
    float   rDampLp_ = 0.0f;            // the tank's damping filter state
    float   modPh_ = 0.0f;
    int     dlyALen_ = kTankDlyA, dlyBLen_ = kTankDlyB;

    // ---- chorus ----
    static constexpr int kChorusLen = 640;
    int16_t chorus_[kChorusLen];
    int     cIdx_ = 0;
    float   cPhase_ = 0.0f, cInc_ = 0.0f, cDepth_ = 0.0f, cMix_ = 0.0f;

    // ---- tone and dynamics ----
    float tiltLp_ = 0.0f, tiltLow_ = 1.0f, tiltHigh_ = 1.0f;
    float compAmt_ = 0.0f, compEnv_ = 0.0f, compGain_ = 1.0f, compTarget_ = 1.0f;
    float hpX1_ = 0.0f, hpY1_ = 0.0f, hpX2_ = 0.0f, hpY2_ = 0.0f;
    bool  speakerMode_ = true;

    float drive_ = 0.0f, drvMix_ = 0.0f, master_ = 0.7f;
    float limGain_ = 1.0f, peak_ = 0.0f;
};

}  // namespace synth
