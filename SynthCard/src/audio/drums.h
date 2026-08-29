// SynthCard - fully synthesised drum engine (no samples, no SD dependency).
#pragma once
#include "dsp.h"
#include "voice.h"

namespace synth {

enum DrumLane : uint8_t {
    DL_KICK = 0, DL_SNARE, DL_CHH, DL_OHH, DL_CLAP, DL_TOM, DL_RIM, DL_CRASH, DL_PERC, DL_COUNT
};
extern const char* const kDrumNames[DL_COUNT];      // "KICK", "SNARE", ...
extern const char* const kDrumShort[DL_COUNT];      // "KIK", "SNR", ...

enum DrumParam : uint8_t { DP_TUNE = 0, DP_DECAY, DP_TONE, DP_LEVEL, DP_COUNT };
extern const char* const kDrumParamNames[DP_COUNT];

struct DrumKit {
    char    name[13];
    uint8_t p[DL_COUNT][DP_COUNT];
    uint8_t sendDly, sendRev;
    void reset();
    inline uint8_t get(uint8_t lane, uint8_t par) const {
        return (lane < DL_COUNT && par < DP_COUNT) ? p[lane][par] : 0;
    }
    inline void set(uint8_t lane, uint8_t par, int v) {
        if (lane < DL_COUNT && par < DP_COUNT) p[lane][par] = (uint8_t)clampi(v, 0, 127);
    }
};

struct KitPreset { const char* name; uint8_t p[DL_COUNT][DP_COUNT]; uint8_t sendDly, sendRev; };
extern const KitPreset kKits[];
extern const uint8_t   kKitCount;
void loadKit(DrumKit& dst, uint8_t index);

class DrumEngine {
public:
    void init();
    void setKit(const DrumKit* k) { kit_ = k; }
    void trigger(uint8_t lane, uint8_t vel);
    void allOff();
    void render(float* out, int n);
    inline float laneLevel(uint8_t lane) const { return lane < DL_COUNT ? v_[lane].amp : 0.0f; }
    inline bool  laneActive(uint8_t lane) const { return lane < DL_COUNT && v_[lane].active; }

private:
    struct DV {
        bool  active = false;
        float amp = 0.0f, ampCoef = 0.0f;
        float pitch = 0.0f, pitchCoef = 0.0f;
        float ph = 0.0f, ph2 = 0.0f, inc = 0.0f, inc2 = 0.0f;
        float noiseAmp = 0.0f, noiseCoef = 0.0f;
        float gain = 1.0f;
        uint32_t t = 0;
        SVF   filt;
        Rng   rng;
    };
    DV v_[DL_COUNT];
    const DrumKit* kit_ = nullptr;
};

}  // namespace synth
