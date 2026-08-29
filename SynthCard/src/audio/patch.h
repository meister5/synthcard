// SynthCard - patch (sound) model.
//
// Every synth parameter is a uint8_t in a flat array. That one decision buys
// a generic editor UI, trivial serialisation, and safe randomisation without
// a per-parameter special case anywhere.
#pragma once
#include <stdint.h>
#include <string.h>

namespace synth {

enum SynthParam : uint8_t {
    P_ENGINE = 0,   // 0 subtractive, 1 FM, 2 wavetable, 3 chip
    P_O1_WAVE,      // saw sqr pulse tri sine noise
    P_O1_LEVEL,
    P_O2_WAVE,
    P_O2_LEVEL,     // in FM engine: modulation index
    P_O2_SEMI,      // 0..48 -> -24..+24 semitones (FM: ratio)
    P_O2_DETUNE,    // 0..127 -> -50..+50 cents
    P_SUB_WAVE,     // 0 square, 1 sine
    P_SUB_LEVEL,
    P_NOISE,
    P_PW,           // pulse width / wavetable morph
    P_FINE,         // master fine tune, -50..+50 cents
    P_GLIDE,        // portamento time

    P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,

    P_FIL_TYPE,     // 0 off, 1 LP, 2 HP, 3 BP
    P_CUTOFF,
    P_RESO,
    P_FEG_AMT,      // bipolar, 64 = none
    P_KEYTRK,
    P_FEG_A, P_FEG_D, P_FEG_S, P_FEG_R,

    P_LFO_WAVE,     // sine tri square s&h saw
    P_LFO_RATE,
    P_LFO_AMT,
    P_LFO_DEST,     // pitch filter amp pw
    P_LFO_SYNC,     // 0 free, 1 retrig per note

    P_VOICE_MODE,   // 0 poly, 1 mono, 2 legato
    P_VELO_AMT,
    P_DRIVE,
    P_LEVEL,
    P_SEND_DLY,
    P_SEND_REV,
    P_BEND,         // pitch bend / mod wheel depth (semitones)
    P_COUNT
};

enum ParamDisp : uint8_t { D_NUM, D_PCT, D_BIPOLAR, D_LIST, D_MS, D_CENTS, D_SEMI };

struct ParamInfo {
    const char* name;
    uint8_t     max;
    uint8_t     def;
    ParamDisp   disp;
    const char* const* list;   // for D_LIST
};

extern const ParamInfo kSynthParamInfo[P_COUNT];

// Parameter pages for the SOUND screen; 6 slots each.
struct ParamPage { const char* name; uint8_t n; uint8_t p[6]; };
extern const ParamPage kSynthPages[];
extern const uint8_t   kSynthPageCount;

struct Patch {
    char    name[13];
    uint8_t p[P_COUNT];

    void reset() {
        memset(name, 0, sizeof(name));
        strncpy(name, "INIT", sizeof(name) - 1);
        for (uint8_t i = 0; i < P_COUNT; ++i) p[i] = kSynthParamInfo[i].def;
    }
    inline uint8_t get(uint8_t id) const { return id < P_COUNT ? p[id] : 0; }
    inline void set(uint8_t id, int v) {
        if (id >= P_COUNT) return;
        int m = kSynthParamInfo[id].max;
        p[id] = (uint8_t)(v < 0 ? 0 : (v > m ? m : v));
    }
    // Normalised 0..1 view used by the audio engine.
    inline float norm(uint8_t id) const {
        uint8_t m = kSynthParamInfo[id].max;
        return m ? (float)p[id] / (float)m : 0.0f;
    }
    // Bipolar -1..1 view (for D_BIPOLAR params).
    inline float bip(uint8_t id) const { return norm(id) * 2.0f - 1.0f; }
};

// Factory presets are stored as sparse overrides on top of INIT, which keeps
// the table readable and costs ~40 bytes per preset instead of ~40 * 1.
struct PresetOverride { uint8_t id, val; };
struct PresetEntry { const char* name; const char* group; const PresetOverride* ov; uint8_t n; };
extern const PresetEntry kPresets[];
extern const uint16_t    kPresetCount;

void loadPreset(Patch& dst, uint16_t index);

// Format a parameter's value into buf ("SAW", "62%", "+7st", ...).
void formatParam(const Patch& pt, uint8_t id, char* buf, int bufLen);

}  // namespace synth
