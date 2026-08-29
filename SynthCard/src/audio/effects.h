// SynthCard - master effects rack: delay, reverb, chorus, drive, limiter.
// All delay lines are int16 to keep the whole rack under ~45 KB of DRAM.
#pragma once
#include "dsp.h"

namespace synth {

enum FxParam : uint8_t {
    FX_DLY_TIME = 0, FX_DLY_FB, FX_DLY_MIX,
    FX_REV_SIZE, FX_REV_DAMP, FX_REV_MIX,
    FX_CHO_RATE, FX_CHO_DEPTH, FX_CHO_MIX,
    FX_DRIVE, FX_DRV_MIX,
    FX_MASTER,
    FX_COUNT
};

struct FxInfo { const char* name; uint8_t def; uint8_t disp; };
extern const FxInfo kFxInfo[FX_COUNT];

struct FxSettings {
    uint8_t p[FX_COUNT];
    void reset() { for (uint8_t i = 0; i < FX_COUNT; ++i) p[i] = kFxInfo[i].def; }
    inline float norm(uint8_t i) const { return i < FX_COUNT ? p[i] * (1.0f / 127.0f) : 0.0f; }
    inline void  set(uint8_t i, int v) { if (i < FX_COUNT) p[i] = (uint8_t)clampi(v, 0, 127); }
};
void formatFx(const FxSettings& fx, uint8_t id, char* buf, int len);

constexpr int kDelayMax  = 16000;   // 500 ms at 32 kHz

class Effects {
public:
    void init();
    void applySettings(const FxSettings& s);
    // dry[] is modified in place. dlySend/revSend are additional send buses.
    void process(float* dry, const float* dlySend, const float* revSend, int n);
    inline float peak() const { return peak_; }
    inline float limiterGain() const { return limGain_; }

private:
    static constexpr int kCombCount = 4;
    static constexpr int kApCount   = 2;
    static constexpr int kChorusLen = 640;

    int16_t delay_[kDelayMax];
    int32_t dWrite_ = 0, dLen_ = 8000;
    float   dFb_ = 0.3f, dMix_ = 0.0f, dLp_ = 0.0f;

    int16_t comb_[kCombCount][1024];
    int16_t ap_[kApCount][512];
    int     combLen_[kCombCount] = {809, 863, 927, 983};
    int     apLen_[kApCount]     = {403, 321};
    int     combIdx_[kCombCount] = {0, 0, 0, 0};
    int     apIdx_[kApCount]     = {0, 0};
    float   combStore_[kCombCount] = {0, 0, 0, 0};
    float   rFb_ = 0.82f, rDamp_ = 0.3f, rMix_ = 0.0f;

    int16_t chorus_[kChorusLen];
    int     cIdx_ = 0;
    float   cPhase_ = 0.0f, cInc_ = 0.0f, cDepth_ = 0.0f, cMix_ = 0.0f;

    float drive_ = 0.0f, drvMix_ = 0.0f, master_ = 0.7f;
    float limGain_ = 1.0f, peak_ = 0.0f;
};

}  // namespace synth
