// SynthCard - fully synthesised drum engine (no samples, no SD dependency).
//
// Twelve lanes across five synthesis families, one family per translation
// unit. The family is picked once per lane per block, so no per-sample switch
// sits in the render loop.
#pragma once
#include "dsp.h"
#include "drum_voice.h"

namespace synth {

enum DrumLane : uint8_t {
    DL_KICK = 0, DL_SNARE, DL_CHH, DL_OHH, DL_RIDE, DL_CRASH,
    DL_CLAP, DL_TOM, DL_RIM, DL_COWBELL, DL_SHAKER, DL_PERC, DL_COUNT
};
extern const char* const kDrumNames[DL_COUNT];      // "KICK", "SNARE", ...
extern const char* const kDrumShort[DL_COUNT];      // "KIK", "SNR", ...

// SNAP is the transient layer: a click on a kick, the snap on a snare, the
// attack ramp on a shaker. DLY/REV are per-lane sends.
enum DrumParam : uint8_t {
    DP_TUNE = 0, DP_DECAY, DP_TONE, DP_LEVEL,
    DP_SNAP, DP_DRIVE, DP_DLY, DP_REV, DP_COUNT
};
extern const char* const kDrumParamNames[DP_COUNT];

// Performance macros, stored per lane and applied on top of the eight base
// parameters at trigger time. 64 is neutral.
enum DrumMacro : uint8_t { DM_PUNCH = 0, DM_TONE, DM_SPACE, DM_COUNT };
extern const char* const kDrumMacroNames[DM_COUNT];
constexpr uint8_t kMacroNeutral = 64;

// The DRUM screen walks the three macros and the eight parameters with one
// cursor, so the macro row and the detail page are two ends of a single list
// rather than a mode you have to switch between.
constexpr uint8_t kDrumCursorCount = DM_COUNT + DP_COUNT;

// Lanes sharing a non-zero group cut each other off: a closed hat silences an
// open hat and a ride, which is the behaviour a drum machine is expected to
// have. The cut is a 2 ms fade, because an instant one clicks.
extern const uint8_t kChokeGroup[DL_COUNT];

struct DrumKit {
    char    name[13];
    uint8_t p[DL_COUNT][DP_COUNT];
    uint8_t m[DL_COUNT][DM_COUNT];
    void reset();
    inline uint8_t get(uint8_t lane, uint8_t par) const {
        return (lane < DL_COUNT && par < DP_COUNT) ? p[lane][par] : 0;
    }
    inline void set(uint8_t lane, uint8_t par, int v) {
        if (lane < DL_COUNT && par < DP_COUNT) p[lane][par] = (uint8_t)clampi(v, 0, 127);
    }
    inline uint8_t macro(uint8_t lane, uint8_t mi) const {
        return (lane < DL_COUNT && mi < DM_COUNT) ? m[lane][mi] : kMacroNeutral;
    }
    inline void setMacro(uint8_t lane, uint8_t mi, int v) {
        if (lane < DL_COUNT && mi < DM_COUNT) m[lane][mi] = (uint8_t)clampi(v, 0, 127);
    }
    // Base parameter with the macros folded in, 0..127. This is what the
    // engine hears; the editor still shows and edits the base value.
    uint8_t effective(uint8_t lane, uint8_t par) const;
};

struct KitPreset {
    const char* name;
    uint8_t p[DL_COUNT][DP_COUNT];
};
extern const KitPreset kKits[];
extern const uint8_t   kKitCount;
void loadKit(DrumKit& dst, uint8_t index);

class DrumEngine {
public:
    void init();
    void setKit(const DrumKit* k) { kit_ = k; }
    void trigger(uint8_t lane, uint8_t vel);
    void allOff();
    // Adds into the main bus and the two send buses; sends are per lane now,
    // so the drum mix cannot be split downstream any more.
    void render(float* out, float* dly, float* rev, int n);
    float laneLevel(uint8_t lane) const;
    bool  laneActive(uint8_t lane) const;

private:
    DV    v_[DL_COUNT];
    // A block-sized scratch buffer so a family can write its lane in one pass
    // and the mixer can then split it across the main and send buses. Member
    // rather than stack: the audio task's stack is small and this is 512 B.
    float scratch_[kBlockSize] = {0};
    const DrumKit* kit_ = nullptr;
};

}  // namespace synth
