// SynthCard - patch (sound) model.
//
// Every synth parameter is a uint8_t in a flat array. That one decision buys
// a generic editor UI, trivial serialisation, and safe randomisation without
// a per-parameter special case anywhere.
//
// Six engines would need ~88 parameters if each engine's controls got their
// own slot, and FM's operator knobs mean nothing to ORGAN. So the array is
// split: slots below P_ENG_BASE are common to every engine, and the 20 slots
// above it are an overlay whose names, ranges and defaults come from whichever
// engine P_ENGINE selects. Look-ups go through paramInfo(engine, id) instead
// of a bare table index; a Patch always knows its own engine, so no caller
// has to pass one.
#pragma once
#include <stdint.h>
#include <string.h>

namespace synth {

enum SynthEngine : uint8_t {
    ENG_ANALOG = 0, ENG_FM, ENG_WT, ENG_PLUCK, ENG_ORGAN, ENG_CHIP, ENG_COUNT
};
extern const char* const kEngineNames[ENG_COUNT];

// ---------------------------------------------------------------- params --
enum SynthParam : uint8_t {
    P_ENGINE = 0,
    P_FINE,                     // master fine tune, -50..+50 cents
    P_GLIDE,                    // portamento time
    P_BEND,                     // pitch bend depth (semitones)

    P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,

    P_FIL_TYPE,                 // 0 off, 1 LP, 2 HP, 3 BP, 4 notch
    P_FIL_POLES,                // 0 = 2-pole (12 dB), 1 = 4-pole (24 dB)
    P_CUTOFF,
    P_RESO,
    P_FEG_AMT,                  // bipolar, 64 = none
    P_KEYTRK,
    P_VEL_CUT,                  // velocity -> cutoff
    P_FEG_A, P_FEG_D, P_FEG_S, P_FEG_R,

    P_LFO_WAVE,                 // sine tri square s&h saw
    P_LFO_RATE,
    P_LFO_AMT,
    P_LFO_DEST,                 // pitch filter amp shape
    P_LFO_SYNC,                 // 0 free, 1 retrig per note

    P_VOICE_MODE,               // 0 poly, 1 mono, 2 legato
    P_VELO_AMT,
    P_DRIVE,
    P_LEVEL,
    P_SEND_DLY,
    P_SEND_REV,

    P_COMMON_COUNT,             // 30

    // Two spare slots so a future common parameter does not shift the
    // overlay and invalidate every saved patch.
    P_ENG_BASE  = 32,
    P_ENG_SLOTS = 20,
    P_COUNT     = P_ENG_BASE + P_ENG_SLOTS   // 52
};

// --- overlay slot names, one set per engine -------------------------------
enum AnalogParam : uint8_t {
    PA_O1_WAVE = P_ENG_BASE, PA_O1_LEVEL, PA_O2_WAVE, PA_O2_LEVEL, PA_O2_SEMI,
    PA_O2_DETUNE, PA_SUB_WAVE, PA_SUB_LEVEL, PA_NOISE, PA_PW,
    PA_UNISON, PA_UNI_DET, PA_COUNT_
};
enum FmParam : uint8_t {
    PF_ALGO = P_ENG_BASE, PF_FB,
    PF_R1, PF_L1, PF_A1, PF_D1,
    PF_R2, PF_L2, PF_A2, PF_D2,
    PF_R3, PF_L3, PF_A3, PF_D3,
    PF_R4, PF_L4, PF_A4, PF_D4, PF_COUNT_
};
enum WtParam : uint8_t {
    PW_TABLE = P_ENG_BASE, PW_MORPH, PW_WARP, PW_O2_LEVEL, PW_O2_SEMI,
    PW_O2_DETUNE, PW_SUB_LEVEL, PW_NOISE, PW_COUNT_
};
enum PluckParam : uint8_t {
    PP_EXCITE = P_ENG_BASE, PP_BRIGHT, PP_DAMP, PP_PICK, PP_BODY, PP_SPREAD,
    PP_COUNT_
};
enum OrganParam : uint8_t {
    PO_D1 = P_ENG_BASE, PO_D2, PO_D3, PO_D4, PO_D5, PO_D6, PO_D7, PO_D8, PO_D9,
    PO_CLICK, PO_ROTARY, PO_COUNT_
};
enum ChipParam : uint8_t {
    PC_DUTY = P_ENG_BASE, PC_ARP, PC_ARP_SPD, PC_VIB_DEP, PC_VIB_DLY,
    PC_NOISE_MODE, PC_O2_LEVEL, PC_O2_SEMI, PC_COUNT_
};

enum ParamDisp : uint8_t { D_NUM, D_PCT, D_BIPOLAR, D_LIST, D_MS, D_CENTS, D_SEMI, D_RATIO };

struct ParamInfo {
    const char* name;
    uint8_t     max;
    uint8_t     def;
    ParamDisp   disp;
    const char* const* list;   // for D_LIST
};

// Engine-aware look-up. `engine` is clamped, `id` out of range yields a safe
// inert entry, so no caller can index off the end of a table.
const ParamInfo& paramInfo(uint8_t engine, uint8_t id);

// Parameter pages for the SOUND screen; 6 slots each. The page set depends on
// the engine: its own pages come first, the common ones after.
struct ParamPage { const char* name; uint8_t n; uint8_t p[6]; };
const ParamPage* synthPages(uint8_t engine);
uint8_t          synthPageCount(uint8_t engine);

struct Patch {
    char    name[13];
    uint8_t p[P_COUNT];

    inline uint8_t engine() const {
        uint8_t e = p[P_ENGINE];
        return e < ENG_COUNT ? e : 0;
    }
    void reset();
    // Rewrites only the overlay block with `eng`'s defaults, so switching
    // engines keeps the filter, envelopes and levels the player set up.
    void setEngine(uint8_t eng);

    inline uint8_t get(uint8_t id) const { return id < P_COUNT ? p[id] : 0; }
    inline void set(uint8_t id, int v) {
        if (id >= P_COUNT) return;
        int m = paramInfo(engine(), id).max;
        p[id] = (uint8_t)(v < 0 ? 0 : (v > m ? m : v));
    }
    // Normalised 0..1 view used by the audio engine.
    inline float norm(uint8_t id) const {
        uint8_t m = paramInfo(engine(), id).max;
        return m ? (float)p[id] / (float)m : 0.0f;
    }
    // Bipolar -1..1 view (for D_BIPOLAR params).
    inline float bip(uint8_t id) const { return norm(id) * 2.0f - 1.0f; }
};

// Factory presets are stored as sparse overrides on top of INIT, which keeps
// the table readable and costs ~40 bytes per preset instead of ~52.
// P_ENGINE must be applied first so the overlay defaults are in place before
// the engine-specific overrides land on top; loadPreset() guarantees that.
struct PresetOverride { uint8_t id, val; };
struct PresetEntry { const char* name; const char* group; const PresetOverride* ov; uint8_t n; };
extern const PresetEntry kPresets[];
extern const uint16_t    kPresetCount;

void loadPreset(Patch& dst, uint16_t index);

// Format a parameter's value into buf ("SAW", "62%", "+7st", ...).
void formatParam(const Patch& pt, uint8_t id, char* buf, int bufLen);

// Converts an envelope-time parameter (0..127) to milliseconds. Shared by the
// voice and by formatParam so the label always matches what is heard.
inline float envMsFor(uint8_t v) {
    float x = v * (1.0f / 127.0f);
    return 1.0f + 7999.0f * x * x * x;
}

// FM operator ratio for a PF_R* value: half-integer steps from 0.5x to 16x.
float fmRatio(uint8_t v);

// Overlay slots that stay musical under randomisation - the ones that shape
// the sound rather than the ones that can silence it. 0xFF terminated.
const uint8_t* engineRandomSlots(uint8_t engine);

}  // namespace synth
