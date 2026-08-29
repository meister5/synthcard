#include "patch.h"
#include <stdio.h>

namespace synth {

static const char* const kWaveNames[]  = {"SAW", "SQR", "PULSE", "TRI", "SINE", "NOISE", nullptr};
static const char* const kSubNames[]   = {"SQR", "SINE", nullptr};
static const char* const kFiltNames[]  = {"OFF", "LPF", "HPF", "BPF", nullptr};
static const char* const kLfoNames[]   = {"SINE", "TRI", "SQR", "S+H", "SAW", nullptr};
static const char* const kDestNames[]  = {"PITCH", "FILTER", "AMP", "PW", nullptr};
static const char* const kVoiceNames[] = {"POLY", "MONO", "LEGATO", nullptr};
static const char* const kEngNames[]   = {"ANALOG", "FM", "WTABLE", "CHIP", nullptr};
static const char* const kSyncNames[]  = {"FREE", "RETRIG", nullptr};

#define PI_(nm, mx, df, ds, ls) { nm, mx, df, ds, ls }
const ParamInfo kSynthParamInfo[P_COUNT] = {
    PI_("ENGINE",  3, 0,   D_LIST,    kEngNames),
    PI_("OSC1",    5, 0,   D_LIST,    kWaveNames),
    PI_("O1 LVL",  127, 110, D_PCT,   nullptr),
    PI_("OSC2",    5, 0,   D_LIST,    kWaveNames),
    PI_("O2 LVL",  127, 0,   D_PCT,   nullptr),
    PI_("O2 SEMI", 48, 24,  D_SEMI,   nullptr),
    PI_("O2 TUNE", 127, 64, D_CENTS,  nullptr),
    PI_("SUB",     1, 0,   D_LIST,    kSubNames),
    PI_("SUB LVL", 127, 0,  D_PCT,    nullptr),
    PI_("NOISE",   127, 0,  D_PCT,    nullptr),
    PI_("PWIDTH",  127, 64, D_PCT,    nullptr),
    PI_("FINE",    127, 64, D_CENTS,  nullptr),
    PI_("GLIDE",   127, 0,  D_MS,     nullptr),

    PI_("AMP ATK", 127, 2,   D_MS,    nullptr),
    PI_("AMP DEC", 127, 60,  D_MS,    nullptr),
    PI_("AMP SUS", 127, 100, D_PCT,   nullptr),
    PI_("AMP REL", 127, 40,  D_MS,    nullptr),

    PI_("FILTER",  3, 1,   D_LIST,    kFiltNames),
    PI_("CUTOFF",  127, 96, D_PCT,    nullptr),
    PI_("RESO",    127, 20, D_PCT,    nullptr),
    PI_("ENV AMT", 127, 64, D_BIPOLAR, nullptr),
    PI_("KEYTRK",  127, 40, D_PCT,    nullptr),
    PI_("FEG ATK", 127, 2,  D_MS,     nullptr),
    PI_("FEG DEC", 127, 50, D_MS,     nullptr),
    PI_("FEG SUS", 127, 40, D_PCT,    nullptr),
    PI_("FEG REL", 127, 40, D_MS,     nullptr),

    PI_("LFO",     4, 0,   D_LIST,    kLfoNames),
    PI_("LFO RTE", 127, 45, D_NUM,    nullptr),
    PI_("LFO AMT", 127, 0,  D_PCT,    nullptr),
    PI_("LFO DST", 3, 0,   D_LIST,    kDestNames),
    PI_("LFO SYN", 1, 0,   D_LIST,    kSyncNames),

    PI_("VOICE",   2, 0,   D_LIST,    kVoiceNames),
    PI_("VELO",    127, 80, D_PCT,    nullptr),
    PI_("DRIVE",   127, 0,  D_PCT,    nullptr),
    PI_("LEVEL",   127, 90, D_PCT,    nullptr),
    PI_("DELAY",   127, 0,  D_PCT,    nullptr),
    PI_("REVERB",  127, 0,  D_PCT,    nullptr),
    PI_("BEND",    24, 2,   D_NUM,    nullptr),
};
#undef PI_

const ParamPage kSynthPages[] = {
    {"OSC",    6, {P_ENGINE, P_O1_WAVE, P_O1_LEVEL, P_O2_WAVE, P_O2_LEVEL, P_PW}},
    {"TUNE",   6, {P_O2_SEMI, P_O2_DETUNE, P_SUB_WAVE, P_SUB_LEVEL, P_NOISE, P_FINE}},
    {"AMP",    5, {P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R, P_VELO_AMT, 0}},
    {"FILTER", 6, {P_FIL_TYPE, P_CUTOFF, P_RESO, P_FEG_AMT, P_KEYTRK, P_FEG_D}},
    {"F.ENV",  4, {P_FEG_A, P_FEG_D, P_FEG_S, P_FEG_R, 0, 0}},
    {"LFO",    5, {P_LFO_WAVE, P_LFO_RATE, P_LFO_AMT, P_LFO_DEST, P_LFO_SYNC, 0}},
    {"VOICE",  6, {P_VOICE_MODE, P_GLIDE, P_DRIVE, P_LEVEL, P_SEND_DLY, P_SEND_REV}},
};
const uint8_t kSynthPageCount = sizeof(kSynthPages) / sizeof(kSynthPages[0]);

// ---------------------------------------------------------------- presets --
#define OV static const PresetOverride
OV pv_basic[]  = {{P_O1_WAVE,0},{P_O2_WAVE,0},{P_O2_LEVEL,80},{P_O2_DETUNE,71},{P_CUTOFF,100},{P_RESO,26},{P_FEG_AMT,80},{P_FEG_D,55},{P_AMP_R,45},{P_SEND_DLY,28}};
OV pv_bright[] = {{P_O1_WAVE,0},{P_O2_WAVE,2},{P_O2_LEVEL,90},{P_O2_DETUNE,78},{P_PW,40},{P_CUTOFF,118},{P_RESO,34},{P_FEG_AMT,92},{P_FEG_D,70},{P_AMP_R,50},{P_SEND_REV,34},{P_SEND_DLY,30}};
OV pv_acidl[]  = {{P_O1_WAVE,0},{P_CUTOFF,52},{P_RESO,112},{P_FEG_AMT,110},{P_FEG_D,38},{P_FEG_S,0},{P_AMP_D,80},{P_AMP_S,90},{P_AMP_R,26},{P_VOICE_MODE,2},{P_GLIDE,34},{P_DRIVE,50}};
OV pv_sqlead[] = {{P_O1_WAVE,1},{P_O2_WAVE,1},{P_O2_LEVEL,70},{P_O2_SEMI,36},{P_CUTOFF,108},{P_RESO,18},{P_AMP_R,34},{P_SEND_DLY,44}};
OV pv_soft[]   = {{P_O1_WAVE,3},{P_O2_WAVE,4},{P_O2_LEVEL,74},{P_O2_DETUNE,72},{P_CUTOFF,84},{P_AMP_A,20},{P_AMP_R,60},{P_SEND_REV,50}};

OV pv_sub[]    = {{P_O1_WAVE,4},{P_O1_LEVEL,120},{P_SUB_WAVE,1},{P_SUB_LEVEL,90},{P_CUTOFF,64},{P_AMP_D,70},{P_AMP_S,80},{P_AMP_R,20},{P_VOICE_MODE,1},{P_LEVEL,110}};
OV pv_acidb[]  = {{P_O1_WAVE,0},{P_CUTOFF,40},{P_RESO,118},{P_FEG_AMT,104},{P_FEG_D,32},{P_FEG_S,0},{P_AMP_S,70},{P_AMP_R,18},{P_VOICE_MODE,2},{P_GLIDE,28},{P_DRIVE,64},{P_SUB_LEVEL,40}};
OV pv_fmb[]    = {{P_ENGINE,1},{P_O2_LEVEL,72},{P_O2_SEMI,24},{P_CUTOFF,86},{P_AMP_D,58},{P_AMP_S,40},{P_AMP_R,20},{P_VOICE_MODE,1},{P_FEG_AMT,74},{P_FEG_D,34}};
OV pv_pluckb[] = {{P_O1_WAVE,1},{P_SUB_LEVEL,70},{P_CUTOFF,70},{P_RESO,50},{P_FEG_AMT,96},{P_FEG_D,26},{P_FEG_S,0},{P_AMP_D,44},{P_AMP_S,20},{P_AMP_R,16},{P_VOICE_MODE,1}};
OV pv_dirtyb[] = {{P_O1_WAVE,0},{P_O2_WAVE,1},{P_O2_LEVEL,96},{P_O2_DETUNE,58},{P_SUB_LEVEL,64},{P_CUTOFF,58},{P_RESO,60},{P_DRIVE,100},{P_AMP_R,20},{P_VOICE_MODE,1},{P_FEG_AMT,80},{P_FEG_D,40}};

OV pv_warm[]   = {{P_O1_WAVE,0},{P_O2_WAVE,0},{P_O2_LEVEL,105},{P_O2_DETUNE,76},{P_SUB_LEVEL,40},{P_CUTOFF,74},{P_AMP_A,52},{P_AMP_D,90},{P_AMP_S,110},{P_AMP_R,96},{P_LFO_RATE,20},{P_LFO_AMT,18},{P_LFO_DEST,1},{P_SEND_REV,88},{P_LEVEL,78}};
OV pv_space[]  = {{P_O1_WAVE,2},{P_PW,44},{P_O2_WAVE,0},{P_O2_LEVEL,90},{P_O2_SEMI,31},{P_CUTOFF,80},{P_RESO,30},{P_AMP_A,70},{P_AMP_D,100},{P_AMP_S,100},{P_AMP_R,116},{P_LFO_RATE,14},{P_LFO_AMT,40},{P_LFO_DEST,3},{P_SEND_REV,104},{P_SEND_DLY,44},{P_LEVEL,74}};
OV pv_apad[]   = {{P_O1_WAVE,3},{P_O2_WAVE,0},{P_O2_LEVEL,88},{P_O2_DETUNE,80},{P_CUTOFF,68},{P_RESO,24},{P_FEG_AMT,78},{P_FEG_A,60},{P_FEG_D,110},{P_AMP_A,60},{P_AMP_D,110},{P_AMP_S,105},{P_AMP_R,104},{P_SEND_REV,84},{P_LEVEL,76}};

OV pv_piano[]  = {{P_ENGINE,1},{P_O2_LEVEL,54},{P_O2_SEMI,24},{P_AMP_A,0},{P_AMP_D,72},{P_AMP_S,30},{P_AMP_R,40},{P_CUTOFF,104},{P_FEG_AMT,84},{P_FEG_D,50},{P_FEG_S,10},{P_SEND_REV,40}};
OV pv_organ[]  = {{P_O1_WAVE,4},{P_O2_WAVE,4},{P_O2_LEVEL,86},{P_O2_SEMI,36},{P_SUB_WAVE,1},{P_SUB_LEVEL,72},{P_AMP_A,0},{P_AMP_S,127},{P_AMP_R,8},{P_CUTOFF,112},{P_SEND_REV,44},{P_LFO_RATE,58},{P_LFO_AMT,10},{P_LFO_DEST,0}};
OV pv_ekeys[]  = {{P_ENGINE,1},{P_O2_LEVEL,66},{P_O2_SEMI,36},{P_AMP_A,0},{P_AMP_D,84},{P_AMP_S,44},{P_AMP_R,48},{P_CUTOFF,96},{P_SEND_REV,54},{P_SEND_DLY,24},{P_LFO_RATE,30},{P_LFO_AMT,14},{P_LFO_DEST,2}};

OV pv_pluck[]  = {{P_O1_WAVE,0},{P_O2_WAVE,1},{P_O2_LEVEL,64},{P_O2_DETUNE,74},{P_CUTOFF,90},{P_RESO,56},{P_FEG_AMT,100},{P_FEG_D,24},{P_FEG_S,0},{P_AMP_D,40},{P_AMP_S,0},{P_AMP_R,30},{P_SEND_DLY,54},{P_SEND_REV,40}};
OV pv_bell[]   = {{P_ENGINE,1},{P_O2_LEVEL,90},{P_O2_SEMI,41},{P_AMP_A,0},{P_AMP_D,96},{P_AMP_S,0},{P_AMP_R,80},{P_FIL_TYPE,0},{P_SEND_REV,90},{P_SEND_DLY,40},{P_LEVEL,74}};
OV pv_digit[]  = {{P_ENGINE,2},{P_PW,30},{P_O2_WAVE,1},{P_O2_LEVEL,50},{P_O2_SEMI,31},{P_AMP_D,34},{P_AMP_S,0},{P_AMP_R,26},{P_CUTOFF,110},{P_RESO,40},{P_FEG_AMT,88},{P_FEG_D,22},{P_FEG_S,0},{P_SEND_DLY,60}};

OV pv_chip[]   = {{P_ENGINE,3},{P_O1_WAVE,2},{P_PW,32},{P_FIL_TYPE,0},{P_AMP_A,0},{P_AMP_D,60},{P_AMP_S,80},{P_AMP_R,6},{P_VOICE_MODE,1},{P_LEVEL,84}};
OV pv_glitch[] = {{P_ENGINE,2},{P_O1_WAVE,1},{P_O2_WAVE,5},{P_O2_LEVEL,60},{P_NOISE,40},{P_LFO_WAVE,3},{P_LFO_RATE,110},{P_LFO_AMT,90},{P_LFO_DEST,0},{P_AMP_D,30},{P_AMP_S,20},{P_AMP_R,14},{P_CUTOFF,90},{P_RESO,80},{P_SEND_DLY,70}};
OV pv_drone[]  = {{P_O1_WAVE,0},{P_O2_WAVE,0},{P_O2_LEVEL,110},{P_O2_DETUNE,52},{P_SUB_LEVEL,80},{P_CUTOFF,50},{P_RESO,44},{P_AMP_A,90},{P_AMP_S,127},{P_AMP_R,120},{P_LFO_RATE,8},{P_LFO_AMT,52},{P_LFO_DEST,1},{P_SEND_REV,110},{P_LEVEL,70}};
OV pv_noise[]  = {{P_O1_LEVEL,0},{P_NOISE,120},{P_CUTOFF,70},{P_RESO,90},{P_FIL_TYPE,3},{P_FEG_AMT,96},{P_FEG_D,60},{P_FEG_S,0},{P_AMP_D,70},{P_AMP_S,30},{P_AMP_R,50},{P_SEND_REV,70}};
OV pv_laser[]  = {{P_O1_WAVE,0},{P_FEG_AMT,127},{P_FEG_D,18},{P_FEG_S,0},{P_CUTOFF,30},{P_RESO,100},{P_AMP_D,26},{P_AMP_S,0},{P_AMP_R,10},{P_LFO_WAVE,4},{P_LFO_RATE,100},{P_LFO_AMT,60},{P_LFO_DEST,0},{P_VOICE_MODE,1}};
OV pv_weird[]  = {{P_ENGINE,1},{P_O2_LEVEL,110},{P_O2_SEMI,31},{P_O2_DETUNE,90},{P_LFO_WAVE,3},{P_LFO_RATE,72},{P_LFO_AMT,70},{P_LFO_DEST,3},{P_CUTOFF,76},{P_RESO,70},{P_AMP_A,24},{P_AMP_R,70},{P_SEND_DLY,66},{P_SEND_REV,60}};
#undef OV

#define PE(n, g, v) { n, g, v, (uint8_t)(sizeof(v) / sizeof(v[0])) }
const PresetEntry kPresets[] = {
    PE("BASIC LEAD",  "LEAD",  pv_basic),
    PE("BRIGHT LEAD", "LEAD",  pv_bright),
    PE("ACID LEAD",   "LEAD",  pv_acidl),
    PE("SQUARE LEAD", "LEAD",  pv_sqlead),
    PE("SOFT LEAD",   "LEAD",  pv_soft),
    PE("SUB BASS",    "BASS",  pv_sub),
    PE("ACID BASS",   "BASS",  pv_acidb),
    PE("FM BASS",     "BASS",  pv_fmb),
    PE("PLUCK BASS",  "BASS",  pv_pluckb),
    PE("DIRTY BASS",  "BASS",  pv_dirtyb),
    PE("WARM PAD",    "PAD",   pv_warm),
    PE("SPACE PAD",   "PAD",   pv_space),
    PE("ANALOG PAD",  "PAD",   pv_apad),
    PE("PIANO",       "KEYS",  pv_piano),
    PE("ORGAN",       "KEYS",  pv_organ),
    PE("E.KEYS",      "KEYS",  pv_ekeys),
    PE("SYNTH PLUCK", "PLUCK", pv_pluck),
    PE("BELL",        "PLUCK", pv_bell),
    PE("DIGI PLUCK",  "PLUCK", pv_digit),
    PE("CHIP SQUARE", "CHIP",  pv_chip),
    PE("GLITCH",      "EXP",   pv_glitch),
    PE("DRONE",       "EXP",   pv_drone),
    PE("NOISE",       "EXP",   pv_noise),
    PE("LASER",       "EXP",   pv_laser),
    PE("WEIRD",       "EXP",   pv_weird),
};
#undef PE
const uint16_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

void loadPreset(Patch& dst, uint16_t index) {
    dst.reset();
    if (index >= kPresetCount) return;
    const PresetEntry& e = kPresets[index];
    for (uint8_t i = 0; i < e.n; ++i) dst.set(e.ov[i].id, e.ov[i].val);
    strncpy(dst.name, e.name, sizeof(dst.name) - 1);
    dst.name[sizeof(dst.name) - 1] = 0;
}

void formatParam(const Patch& pt, uint8_t id, char* buf, int bufLen) {
    if (id >= P_COUNT || bufLen < 2) { if (bufLen) buf[0] = 0; return; }
    const ParamInfo& in = kSynthParamInfo[id];
    int v = pt.p[id];
    switch (in.disp) {
        case D_LIST:    snprintf(buf, bufLen, "%s", in.list[v]); break;
        case D_PCT:     snprintf(buf, bufLen, "%d%%", (v * 100 + in.max / 2) / in.max); break;
        case D_BIPOLAR: snprintf(buf, bufLen, "%+d", (v - 64) * 100 / 64); break;
        case D_CENTS:   snprintf(buf, bufLen, "%+dc", (v - 64) * 50 / 64); break;
        case D_SEMI:    snprintf(buf, bufLen, "%+d", v - 24); break;
        case D_MS: {
            // Envelope times are exponential: 1 ms .. ~8 s.
            int ms = (int)(1.0f + 7999.0f * ((float)v / 127.0f) * ((float)v / 127.0f) * ((float)v / 127.0f));
            if (ms >= 1000) snprintf(buf, bufLen, "%d.%ds", ms / 1000, (ms % 1000) / 100);
            else            snprintf(buf, bufLen, "%dms", ms);
            break;
        }
        default:        snprintf(buf, bufLen, "%d", v); break;
    }
}

}  // namespace synth
