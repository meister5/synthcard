#include "patch.h"
#include <stdio.h>

namespace synth {

const char* const kEngineNames[ENG_COUNT] =
    {"ANALOG", "FM", "WAVETBL", "PLUCK", "ORGAN", "CHIP"};

static const char* const kWaveNames[]  = {"SAW", "SQR", "PULSE", "TRI", "SINE", "NOISE", nullptr};
static const char* const kSubNames[]   = {"SQR", "SINE", nullptr};
static const char* const kFiltNames[]  = {"OFF", "LPF", "HPF", "BPF", "NOTCH", nullptr};
static const char* const kPoleNames[]  = {"12DB", "24DB", nullptr};
static const char* const kLfoNames[]   = {"SINE", "TRI", "SQR", "S+H", "SAW", nullptr};
static const char* const kDestNames[]  = {"PITCH", "FILTER", "AMP", "SHAPE", nullptr};
static const char* const kVoiceNames[] = {"POLY", "MONO", "LEGATO", nullptr};
static const char* const kSyncNames[]  = {"FREE", "RETRIG", nullptr};
static const char* const kEngList[]    = {"ANALOG", "FM", "WAVETBL", "PLUCK", "ORGAN", "CHIP", nullptr};

// Wavetable names, in the order eng_wt.cpp stores them.
static const char* const kTableNames[] = {
    "SAW", "SQUARE", "TRI", "SINE", "PULSE25", "PULSE12", "VOWEL A", "VOWEL E",
    "VOWEL O", "ORGAN", "BELL", "REED", "FOLD 1", "FOLD 2", "GLASS", "GRIT", nullptr
};
// Named for what they do, in the same order as kAlgos[] in eng_fm.cpp.
static const char* const kFmAlgoNames[] = {
    "CHAIN", "STACK", "TRIMOD", "TWIN", "3+1", "FAN", "1+3+4", "ADD", nullptr
};
static const char* const kChipArpNames[]   = {"OFF", "MAJOR", "MINOR", "OCT", "FIFTH", "DOWN", nullptr};
static const char* const kChipNoiseNames[] = {"LONG", "SHORT", "PERIODIC", nullptr};
static const char* const kExciteNames[]    = {"NOISE", "PLUCK", "MALLET", "BOW", nullptr};

// FM operator ratios: half-integer steps from 0.5 to 16. Musical, predictable,
// and the whole range fits one uint8_t index.
float fmRatio(uint8_t v) { return 0.5f * (float)((v > 31 ? 31 : v) + 1); }

static const ParamInfo kInert = { "", 0, 0, D_NUM, nullptr };

#define PI_(nm, mx, df, ds, ls) { nm, mx, df, ds, ls }

// ------------------------------------------------------------ common block --
static const ParamInfo kCommonInfo[P_COMMON_COUNT] = {
    PI_("ENGINE",  ENG_COUNT - 1, 0, D_LIST, kEngList),
    PI_("FINE",    127, 64, D_CENTS,  nullptr),
    PI_("GLIDE",   127, 0,  D_MS,     nullptr),
    PI_("BEND",    24,  2,  D_NUM,    nullptr),

    PI_("AMP ATK", 127, 2,   D_MS,    nullptr),
    PI_("AMP DEC", 127, 60,  D_MS,    nullptr),
    PI_("AMP SUS", 127, 100, D_PCT,   nullptr),
    PI_("AMP REL", 127, 40,  D_MS,    nullptr),

    PI_("FILTER",  4, 1,   D_LIST,    kFiltNames),
    PI_("SLOPE",   1, 0,   D_LIST,    kPoleNames),
    PI_("CUTOFF",  127, 96, D_PCT,    nullptr),
    PI_("RESO",    127, 20, D_PCT,    nullptr),
    PI_("ENV AMT", 127, 64, D_BIPOLAR, nullptr),
    PI_("KEYTRK",  127, 40, D_PCT,    nullptr),
    PI_("VEL>CUT", 127, 0,  D_PCT,    nullptr),
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
};

// ------------------------------------------------------------ overlay sets --
// Unused slots are inert: named "", max 0, so the generic editor and the
// randomiser both skip them and nothing can index off the end.
#define NONE PI_("", 0, 0, D_NUM, nullptr)

static const ParamInfo kAnalogInfo[P_ENG_SLOTS] = {
    PI_("OSC1",    5, 0,    D_LIST,  kWaveNames),
    PI_("O1 LVL",  127, 110, D_PCT,  nullptr),
    PI_("OSC2",    5, 0,    D_LIST,  kWaveNames),
    PI_("O2 LVL",  127, 0,   D_PCT,  nullptr),
    PI_("O2 SEMI", 48, 24,   D_SEMI, nullptr),
    PI_("O2 TUNE", 127, 64,  D_CENTS, nullptr),
    PI_("SUB",     1, 0,     D_LIST, kSubNames),
    PI_("SUB LVL", 127, 0,   D_PCT,  nullptr),
    PI_("NOISE",   127, 0,   D_PCT,  nullptr),
    PI_("PWIDTH",  127, 64,  D_PCT,  nullptr),
    PI_("UNISON",  3, 0,     D_NUM,  nullptr),
    PI_("SPREAD",  127, 40,  D_PCT,  nullptr),
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
};

static const ParamInfo kFmInfo[P_ENG_SLOTS] = {
    PI_("ALGO",    7, 0,    D_LIST,  kFmAlgoNames),
    PI_("FEEDBK",  127, 0,   D_PCT,  nullptr),
    PI_("OP1 RAT", 31, 1,    D_RATIO, nullptr),
    PI_("OP1 LVL", 127, 127, D_PCT,  nullptr),
    PI_("OP1 ATK", 127, 0,   D_MS,   nullptr),
    PI_("OP1 DEC", 127, 90,  D_MS,   nullptr),
    PI_("OP2 RAT", 31, 3,    D_RATIO, nullptr),
    PI_("OP2 LVL", 127, 70,  D_PCT,  nullptr),
    PI_("OP2 ATK", 127, 0,   D_MS,   nullptr),
    PI_("OP2 DEC", 127, 70,  D_MS,   nullptr),
    PI_("OP3 RAT", 31, 1,    D_RATIO, nullptr),
    PI_("OP3 LVL", 127, 0,   D_PCT,  nullptr),
    PI_("OP3 ATK", 127, 0,   D_MS,   nullptr),
    PI_("OP3 DEC", 127, 60,  D_MS,   nullptr),
    PI_("OP4 RAT", 31, 7,    D_RATIO, nullptr),
    PI_("OP4 LVL", 127, 0,   D_PCT,  nullptr),
    PI_("OP4 ATK", 127, 0,   D_MS,   nullptr),
    PI_("OP4 DEC", 127, 50,  D_MS,   nullptr),
    NONE, NONE,
};

static const ParamInfo kWtInfo[P_ENG_SLOTS] = {
    PI_("TABLE",   15, 0,    D_LIST,  kTableNames),
    PI_("MORPH",   127, 0,   D_PCT,  nullptr),
    PI_("WARP",    127, 64,  D_BIPOLAR, nullptr),
    PI_("O2 LVL",  127, 0,   D_PCT,  nullptr),
    PI_("O2 SEMI", 48, 24,   D_SEMI, nullptr),
    PI_("O2 TUNE", 127, 64,  D_CENTS, nullptr),
    PI_("SUB LVL", 127, 0,   D_PCT,  nullptr),
    PI_("NOISE",   127, 0,   D_PCT,  nullptr),
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
};

static const ParamInfo kPluckInfo[P_ENG_SLOTS] = {
    PI_("EXCITE",  3, 1,     D_LIST, kExciteNames),
    PI_("BRIGHT",  127, 80,  D_PCT,  nullptr),
    PI_("DAMPING", 127, 40,  D_PCT,  nullptr),
    PI_("PICKPOS", 127, 30,  D_PCT,  nullptr),
    PI_("BODY",    127, 50,  D_PCT,  nullptr),
    PI_("SPREAD",  127, 0,   D_PCT,  nullptr),
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
    NONE, NONE,
};

static const ParamInfo kOrganInfo[P_ENG_SLOTS] = {
    PI_("16'",     8, 8,     D_NUM,  nullptr),
    PI_("5 1/3'",  8, 0,     D_NUM,  nullptr),
    PI_("8'",      8, 8,     D_NUM,  nullptr),
    PI_("4'",      8, 5,     D_NUM,  nullptr),
    PI_("2 2/3'",  8, 0,     D_NUM,  nullptr),
    PI_("2'",      8, 3,     D_NUM,  nullptr),
    PI_("1 3/5'",  8, 0,     D_NUM,  nullptr),
    PI_("1 1/3'",  8, 0,     D_NUM,  nullptr),
    PI_("1'",      8, 2,     D_NUM,  nullptr),
    PI_("CLICK",   127, 40,  D_PCT,  nullptr),
    PI_("ROTARY",  127, 30,  D_PCT,  nullptr),
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
};

static const ParamInfo kChipInfo[P_ENG_SLOTS] = {
    PI_("DUTY",    3, 2,     D_NUM,  nullptr),
    PI_("ARP",     5, 0,     D_LIST, kChipArpNames),
    PI_("ARP SPD", 127, 70,  D_PCT,  nullptr),
    PI_("VIBRATO", 127, 0,   D_PCT,  nullptr),
    PI_("VIB DLY", 127, 40,  D_PCT,  nullptr),
    PI_("NOISE",   2, 0,     D_LIST, kChipNoiseNames),
    PI_("O2 LVL",  127, 0,   D_PCT,  nullptr),
    PI_("O2 SEMI", 48, 24,   D_SEMI, nullptr),
    NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE,
};
#undef NONE
#undef PI_

static const ParamInfo* const kOverlay[ENG_COUNT] = {
    kAnalogInfo, kFmInfo, kWtInfo, kPluckInfo, kOrganInfo, kChipInfo
};

const ParamInfo& paramInfo(uint8_t engine, uint8_t id) {
    if (engine >= ENG_COUNT) engine = 0;
    if (id < P_COMMON_COUNT) return kCommonInfo[id];
    if (id >= P_ENG_BASE && id < P_COUNT) return kOverlay[engine][id - P_ENG_BASE];
    return kInert;                       // the two pad slots, and anything else
}

// ------------------------------------------------------------------ pages --
// The common pages are repeated in each engine's table so synthPages() stays a
// plain pointer return and the SOUND screen needs no two-part iteration. Six
// duplicated entries per engine is well under a kilobyte of flash.
#define COMMON_PAGES \
    {"AMP",    5, {P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R, P_VELO_AMT, 0}},             \
    {"FILTER", 6, {P_FIL_TYPE, P_FIL_POLES, P_CUTOFF, P_RESO, P_FEG_AMT, P_KEYTRK}},\
    {"F.ENV",  5, {P_FEG_A, P_FEG_D, P_FEG_S, P_FEG_R, P_VEL_CUT, 0}},              \
    {"LFO",    5, {P_LFO_WAVE, P_LFO_RATE, P_LFO_AMT, P_LFO_DEST, P_LFO_SYNC, 0}},  \
    {"VOICE",  6, {P_VOICE_MODE, P_GLIDE, P_FINE, P_BEND, P_DRIVE, P_LEVEL}},       \
    {"MIX",    3, {P_SEND_DLY, P_SEND_REV, P_ENGINE, 0, 0, 0}}

static const ParamPage kPagesAnalog[] = {
    {"OSC",  6, {PA_O1_WAVE, PA_O1_LEVEL, PA_O2_WAVE, PA_O2_LEVEL, PA_PW, PA_NOISE}},
    {"TUNE", 6, {PA_O2_SEMI, PA_O2_DETUNE, PA_SUB_WAVE, PA_SUB_LEVEL, PA_UNISON, PA_UNI_DET}},
    COMMON_PAGES,
};
static const ParamPage kPagesFm[] = {
    {"ALGO", 2, {PF_ALGO, PF_FB, 0, 0, 0, 0}},
    {"OP1",  4, {PF_R1, PF_L1, PF_A1, PF_D1, 0, 0}},
    {"OP2",  4, {PF_R2, PF_L2, PF_A2, PF_D2, 0, 0}},
    {"OP3",  4, {PF_R3, PF_L3, PF_A3, PF_D3, 0, 0}},
    {"OP4",  4, {PF_R4, PF_L4, PF_A4, PF_D4, 0, 0}},
    COMMON_PAGES,
};
static const ParamPage kPagesWt[] = {
    {"WAVE", 6, {PW_TABLE, PW_MORPH, PW_WARP, PW_O2_LEVEL, PW_SUB_LEVEL, PW_NOISE}},
    {"TUNE", 2, {PW_O2_SEMI, PW_O2_DETUNE, 0, 0, 0, 0}},
    COMMON_PAGES,
};
static const ParamPage kPagesPluck[] = {
    {"STRING", 6, {PP_EXCITE, PP_BRIGHT, PP_DAMP, PP_PICK, PP_BODY, PP_SPREAD}},
    COMMON_PAGES,
};
static const ParamPage kPagesOrgan[] = {
    {"DRAWBAR", 6, {PO_D1, PO_D2, PO_D3, PO_D4, PO_D5, PO_D6}},
    {"DRAWBR2", 5, {PO_D7, PO_D8, PO_D9, PO_CLICK, PO_ROTARY, 0}},
    COMMON_PAGES,
};
static const ParamPage kPagesChip[] = {
    {"CHIP",  4, {PC_DUTY, PC_NOISE_MODE, PC_O2_LEVEL, PC_O2_SEMI, 0, 0}},
    {"ARP",   4, {PC_ARP, PC_ARP_SPD, PC_VIB_DEP, PC_VIB_DLY, 0, 0}},
    COMMON_PAGES,
};
#undef COMMON_PAGES

static const ParamPage* const kPageSets[ENG_COUNT] = {
    kPagesAnalog, kPagesFm, kPagesWt, kPagesPluck, kPagesOrgan, kPagesChip
};
static const uint8_t kPageCounts[ENG_COUNT] = {
    (uint8_t)(sizeof(kPagesAnalog) / sizeof(ParamPage)),
    (uint8_t)(sizeof(kPagesFm)     / sizeof(ParamPage)),
    (uint8_t)(sizeof(kPagesWt)     / sizeof(ParamPage)),
    (uint8_t)(sizeof(kPagesPluck)  / sizeof(ParamPage)),
    (uint8_t)(sizeof(kPagesOrgan)  / sizeof(ParamPage)),
    (uint8_t)(sizeof(kPagesChip)   / sizeof(ParamPage)),
};

// Randomisation targets per engine. Levels that can mute the patch outright
// (operator 1, oscillator 1, the 8' drawbar) are deliberately absent, so
// rolling the dice always leaves something audible.
static const uint8_t kRndAnalog[] = {
    PA_O1_WAVE, PA_O2_WAVE, PA_O2_LEVEL, PA_O2_DETUNE, PA_SUB_LEVEL,
    PA_NOISE, PA_PW, PA_UNISON, PA_UNI_DET, 0xFF
};
static const uint8_t kRndFm[] = {
    PF_ALGO, PF_FB, PF_R2, PF_L2, PF_D2, PF_R3, PF_L3, PF_D3,
    PF_R4, PF_L4, PF_D4, 0xFF
};
static const uint8_t kRndWt[] = {
    PW_TABLE, PW_MORPH, PW_WARP, PW_O2_LEVEL, PW_SUB_LEVEL, PW_NOISE, 0xFF
};
static const uint8_t kRndPluck[] = {
    PP_EXCITE, PP_BRIGHT, PP_DAMP, PP_PICK, PP_BODY, PP_SPREAD, 0xFF
};
static const uint8_t kRndOrgan[] = {
    PO_D1, PO_D2, PO_D4, PO_D5, PO_D6, PO_D7, PO_D8, PO_D9,
    PO_CLICK, PO_ROTARY, 0xFF
};
static const uint8_t kRndChip[] = {
    PC_DUTY, PC_ARP, PC_ARP_SPD, PC_VIB_DEP, PC_VIB_DLY, PC_O2_LEVEL, 0xFF
};
static const uint8_t* const kRndSets[ENG_COUNT] = {
    kRndAnalog, kRndFm, kRndWt, kRndPluck, kRndOrgan, kRndChip
};

const uint8_t* engineRandomSlots(uint8_t engine) {
    return kRndSets[engine < ENG_COUNT ? engine : 0];
}

const ParamPage* synthPages(uint8_t engine) {
    return kPageSets[engine < ENG_COUNT ? engine : 0];
}
uint8_t synthPageCount(uint8_t engine) {
    return kPageCounts[engine < ENG_COUNT ? engine : 0];
}

// ------------------------------------------------------------------ Patch --
void Patch::reset() {
    memset(name, 0, sizeof(name));
    strncpy(name, "INIT", sizeof(name) - 1);
    for (uint8_t i = 0; i < P_COMMON_COUNT; ++i) p[i] = kCommonInfo[i].def;
    for (uint8_t i = P_COMMON_COUNT; i < P_ENG_BASE; ++i) p[i] = 0;
    setEngine(ENG_ANALOG);
}

void Patch::setEngine(uint8_t eng) {
    if (eng >= ENG_COUNT) eng = 0;
    p[P_ENGINE] = eng;
    const ParamInfo* ov = kOverlay[eng];
    for (uint8_t i = 0; i < P_ENG_SLOTS; ++i) p[P_ENG_BASE + i] = ov[i].def;
}

// ---------------------------------------------------------------- presets --
#define OV static const PresetOverride

// --- ANALOG -----------------------------------------------------------------
OV pv_basic[]  = {{PA_O1_WAVE,0},{PA_O2_WAVE,0},{PA_O2_LEVEL,80},{PA_O2_DETUNE,71},{P_CUTOFF,100},{P_RESO,26},{P_FEG_AMT,80},{P_FEG_D,55},{P_AMP_R,45},{P_SEND_DLY,28}};
OV pv_super[]  = {{PA_O1_WAVE,0},{PA_UNISON,3},{PA_UNI_DET,58},{PA_O2_LEVEL,0},{P_CUTOFF,110},{P_RESO,20},{P_FEG_AMT,74},{P_FEG_D,64},{P_AMP_A,10},{P_AMP_R,60},{P_LEVEL,72},{P_SEND_REV,44}};
OV pv_bright[] = {{PA_O1_WAVE,0},{PA_O2_WAVE,2},{PA_O2_LEVEL,90},{PA_O2_DETUNE,78},{PA_PW,40},{P_CUTOFF,118},{P_RESO,34},{P_FEG_AMT,92},{P_FEG_D,70},{P_AMP_R,50},{P_SEND_REV,34},{P_SEND_DLY,30}};
OV pv_acidl[]  = {{PA_O1_WAVE,0},{P_CUTOFF,52},{P_RESO,112},{P_FIL_POLES,1},{P_FEG_AMT,110},{P_FEG_D,38},{P_FEG_S,0},{P_AMP_D,80},{P_AMP_S,90},{P_AMP_R,26},{P_VOICE_MODE,2},{P_GLIDE,34},{P_DRIVE,50}};
OV pv_sqlead[] = {{PA_O1_WAVE,1},{PA_O2_WAVE,1},{PA_O2_LEVEL,70},{PA_O2_SEMI,36},{P_CUTOFF,108},{P_RESO,18},{P_AMP_R,34},{P_SEND_DLY,44}};
OV pv_soft[]   = {{PA_O1_WAVE,3},{PA_O2_WAVE,4},{PA_O2_LEVEL,74},{PA_O2_DETUNE,72},{P_CUTOFF,84},{P_AMP_A,20},{P_AMP_R,60},{P_SEND_REV,50}};
OV pv_sub[]    = {{PA_O1_WAVE,4},{PA_O1_LEVEL,120},{PA_SUB_WAVE,1},{PA_SUB_LEVEL,90},{P_CUTOFF,64},{P_AMP_D,70},{P_AMP_S,80},{P_AMP_R,20},{P_VOICE_MODE,1},{P_LEVEL,110}};
OV pv_acidb[]  = {{PA_O1_WAVE,0},{P_CUTOFF,40},{P_RESO,118},{P_FIL_POLES,1},{P_FEG_AMT,104},{P_FEG_D,32},{P_FEG_S,0},{P_AMP_S,70},{P_AMP_R,18},{P_VOICE_MODE,2},{P_GLIDE,28},{P_DRIVE,64},{PA_SUB_LEVEL,40}};
OV pv_reese[]  = {{PA_O1_WAVE,0},{PA_O2_WAVE,0},{PA_O2_LEVEL,120},{PA_O2_DETUNE,44},{PA_SUB_LEVEL,70},{P_CUTOFF,46},{P_RESO,40},{P_FIL_POLES,1},{P_DRIVE,80},{P_AMP_R,24},{P_VOICE_MODE,1},{P_LEVEL,104}};
OV pv_dirtyb[] = {{PA_O1_WAVE,0},{PA_O2_WAVE,1},{PA_O2_LEVEL,96},{PA_O2_DETUNE,58},{PA_SUB_LEVEL,64},{P_CUTOFF,58},{P_RESO,60},{P_DRIVE,100},{P_AMP_R,20},{P_VOICE_MODE,1},{P_FEG_AMT,80},{P_FEG_D,40}};
OV pv_warm[]   = {{PA_O1_WAVE,0},{PA_O2_WAVE,0},{PA_O2_LEVEL,105},{PA_O2_DETUNE,76},{PA_SUB_LEVEL,40},{P_CUTOFF,74},{P_AMP_A,52},{P_AMP_D,90},{P_AMP_S,110},{P_AMP_R,96},{P_LFO_RATE,20},{P_LFO_AMT,18},{P_LFO_DEST,1},{P_SEND_REV,88},{P_LEVEL,78}};
OV pv_space[]  = {{PA_O1_WAVE,2},{PA_PW,44},{PA_O2_WAVE,0},{PA_O2_LEVEL,90},{PA_O2_SEMI,31},{P_CUTOFF,80},{P_RESO,30},{P_AMP_A,70},{P_AMP_D,100},{P_AMP_S,100},{P_AMP_R,116},{P_LFO_RATE,14},{P_LFO_AMT,40},{P_LFO_DEST,3},{P_SEND_REV,104},{P_SEND_DLY,44},{P_LEVEL,74}};
OV pv_apad[]   = {{PA_O1_WAVE,3},{PA_O2_WAVE,0},{PA_O2_LEVEL,88},{PA_O2_DETUNE,80},{PA_UNISON,1},{P_CUTOFF,68},{P_RESO,24},{P_FEG_AMT,78},{P_FEG_A,60},{P_FEG_D,110},{P_AMP_A,60},{P_AMP_D,110},{P_AMP_S,105},{P_AMP_R,104},{P_SEND_REV,84},{P_LEVEL,76}};
OV pv_drone[]  = {{PA_O1_WAVE,0},{PA_O2_WAVE,0},{PA_O2_LEVEL,110},{PA_O2_DETUNE,52},{PA_SUB_LEVEL,80},{P_CUTOFF,50},{P_RESO,44},{P_AMP_A,90},{P_AMP_S,127},{P_AMP_R,120},{P_LFO_RATE,8},{P_LFO_AMT,52},{P_LFO_DEST,1},{P_SEND_REV,110},{P_LEVEL,70}};
OV pv_noise[]  = {{PA_O1_LEVEL,0},{PA_NOISE,120},{P_CUTOFF,70},{P_RESO,90},{P_FIL_TYPE,3},{P_FEG_AMT,96},{P_FEG_D,60},{P_FEG_S,0},{P_AMP_D,70},{P_AMP_S,30},{P_AMP_R,50},{P_SEND_REV,70}};
OV pv_laser[]  = {{PA_O1_WAVE,0},{P_FEG_AMT,127},{P_FEG_D,18},{P_FEG_S,0},{P_CUTOFF,30},{P_RESO,100},{P_AMP_D,26},{P_AMP_S,0},{P_AMP_R,10},{P_LFO_WAVE,4},{P_LFO_RATE,100},{P_LFO_AMT,60},{P_LFO_DEST,0},{P_VOICE_MODE,1}};

// --- FM ---------------------------------------------------------------------
OV pv_fmbass[] = {{P_ENGINE,ENG_FM},{PF_ALGO,0},{PF_R1,1},{PF_L1,127},{PF_R2,3},{PF_L2,84},{PF_D2,44},{PF_D1,70},{P_AMP_D,58},{P_AMP_S,40},{P_AMP_R,20},{P_VOICE_MODE,1},{P_CUTOFF,86}};
OV pv_epiano[] = {{P_ENGINE,ENG_FM},{PF_ALGO,3},{PF_R1,1},{PF_L1,120},{PF_R2,27},{PF_L2,46},{PF_D2,40},{PF_R3,1},{PF_L3,90},{PF_R4,3},{PF_L4,40},{PF_D4,54},{P_AMP_A,0},{P_AMP_D,88},{P_AMP_S,30},{P_AMP_R,52},{P_SEND_REV,52},{P_FIL_TYPE,0}};
OV pv_fmbell[] = {{P_ENGINE,ENG_FM},{PF_ALGO,1},{PF_R1,1},{PF_L1,120},{PF_R2,13},{PF_L2,70},{PF_D2,80},{PF_R3,27},{PF_L3,50},{PF_D3,70},{P_AMP_A,0},{P_AMP_D,100},{P_AMP_S,0},{P_AMP_R,90},{P_FIL_TYPE,0},{P_SEND_REV,96},{P_LEVEL,72}};
OV pv_fmbrass[]= {{P_ENGINE,ENG_FM},{PF_ALGO,4},{PF_R1,1},{PF_L1,124},{PF_R2,1},{PF_L2,76},{PF_A2,40},{PF_D2,90},{PF_R3,3},{PF_L3,60},{P_AMP_A,34},{P_AMP_S,110},{P_AMP_R,44},{P_CUTOFF,104},{P_SEND_REV,40}};
OV pv_fmorg[]  = {{P_ENGINE,ENG_FM},{PF_ALGO,7},{PF_R1,1},{PF_L1,110},{PF_R2,3},{PF_L2,80},{PF_R3,5},{PF_L3,54},{PF_R4,7},{PF_L4,30},{P_AMP_A,0},{P_AMP_S,127},{P_AMP_R,8},{P_FIL_TYPE,0},{P_SEND_REV,50}};
OV pv_fmwood[] = {{P_ENGINE,ENG_FM},{PF_ALGO,0},{PF_R1,1},{PF_L1,120},{PF_R2,5},{PF_L2,100},{PF_D2,26},{PF_FB,40},{P_AMP_A,0},{P_AMP_D,44},{P_AMP_S,0},{P_AMP_R,24},{P_FIL_TYPE,0},{P_SEND_DLY,36}};
OV pv_fmgrowl[]= {{P_ENGINE,ENG_FM},{PF_ALGO,2},{PF_R1,1},{PF_L1,127},{PF_R2,2},{PF_L2,110},{PF_R3,9},{PF_L3,70},{PF_FB,90},{P_AMP_S,100},{P_AMP_R,24},{P_VOICE_MODE,1},{P_CUTOFF,70},{P_DRIVE,44},{P_LEVEL,100}};
OV pv_fmglass[]= {{P_ENGINE,ENG_FM},{PF_ALGO,6},{PF_R1,1},{PF_L1,100},{PF_R2,15},{PF_L2,54},{PF_R3,23},{PF_L3,40},{PF_R4,31},{PF_L4,60},{PF_D4,84},{P_AMP_A,0},{P_AMP_D,104},{P_AMP_S,0},{P_AMP_R,96},{P_FIL_TYPE,0},{P_SEND_REV,104},{P_LEVEL,68}};

// --- WAVETABLE --------------------------------------------------------------
OV pv_wtsweep[]= {{P_ENGINE,ENG_WT},{PW_TABLE,0},{PW_MORPH,0},{P_LFO_RATE,18},{P_LFO_AMT,70},{P_LFO_DEST,3},{P_CUTOFF,104},{P_AMP_A,30},{P_AMP_R,70},{P_SEND_REV,60}};
OV pv_wtvowel[]= {{P_ENGINE,ENG_WT},{PW_TABLE,6},{PW_MORPH,60},{PW_WARP,80},{P_CUTOFF,100},{P_RESO,40},{P_AMP_A,16},{P_AMP_S,110},{P_AMP_R,50},{P_SEND_DLY,40},{P_SEND_REV,46}};
OV pv_wtbell[] = {{P_ENGINE,ENG_WT},{PW_TABLE,10},{PW_MORPH,30},{P_FIL_TYPE,0},{P_AMP_A,0},{P_AMP_D,96},{P_AMP_S,0},{P_AMP_R,84},{P_SEND_REV,92},{P_LEVEL,74}};
OV pv_wtdigi[] = {{P_ENGINE,ENG_WT},{PW_TABLE,12},{PW_MORPH,70},{PW_WARP,96},{PW_O2_LEVEL,50},{PW_O2_SEMI,31},{P_AMP_D,34},{P_AMP_S,0},{P_AMP_R,26},{P_CUTOFF,110},{P_RESO,40},{P_FEG_AMT,88},{P_FEG_D,22},{P_FEG_S,0},{P_SEND_DLY,60}};
OV pv_wtgrit[] = {{P_ENGINE,ENG_WT},{PW_TABLE,15},{PW_MORPH,50},{PW_NOISE,30},{P_CUTOFF,74},{P_RESO,64},{P_FIL_POLES,1},{P_DRIVE,70},{P_AMP_S,100},{P_AMP_R,30},{P_VOICE_MODE,1},{P_LEVEL,96}};
OV pv_wtpad[]  = {{P_ENGINE,ENG_WT},{PW_TABLE,9},{PW_MORPH,40},{PW_SUB_LEVEL,50},{P_CUTOFF,78},{P_AMP_A,66},{P_AMP_D,100},{P_AMP_S,108},{P_AMP_R,110},{P_LFO_RATE,12},{P_LFO_AMT,30},{P_LFO_DEST,3},{P_SEND_REV,100},{P_LEVEL,74}};

// --- PLUCK ------------------------------------------------------------------
OV pv_nylon[]  = {{P_ENGINE,ENG_PLUCK},{PP_EXCITE,1},{PP_BRIGHT,70},{PP_DAMP,44},{PP_PICK,30},{PP_BODY,80},{P_FIL_TYPE,0},{P_AMP_S,127},{P_AMP_R,60},{P_SEND_REV,44}};
OV pv_steel[]  = {{P_ENGINE,ENG_PLUCK},{PP_EXCITE,1},{PP_BRIGHT,110},{PP_DAMP,24},{PP_PICK,12},{PP_BODY,60},{P_FIL_TYPE,0},{P_AMP_S,127},{P_AMP_R,70},{P_SEND_REV,40},{P_SEND_DLY,30}};
OV pv_harp[]   = {{P_ENGINE,ENG_PLUCK},{PP_EXCITE,1},{PP_BRIGHT,96},{PP_DAMP,14},{PP_PICK,50},{PP_BODY,44},{P_FIL_TYPE,0},{P_AMP_S,127},{P_AMP_R,96},{P_SEND_REV,80},{P_LEVEL,80}};
OV pv_mallet[] = {{P_ENGINE,ENG_PLUCK},{PP_EXCITE,2},{PP_BRIGHT,60},{PP_DAMP,70},{PP_PICK,64},{PP_BODY,30},{P_FIL_TYPE,0},{P_AMP_S,127},{P_AMP_R,40},{P_SEND_REV,60}};
OV pv_koto[]   = {{P_ENGINE,ENG_PLUCK},{PP_EXCITE,1},{PP_BRIGHT,120},{PP_DAMP,34},{PP_PICK,8},{PP_BODY,100},{P_FIL_TYPE,0},{P_AMP_S,127},{P_AMP_R,54},{P_SEND_DLY,44},{P_SEND_REV,50}};
OV pv_bowed[]  = {{P_ENGINE,ENG_PLUCK},{PP_EXCITE,3},{PP_BRIGHT,80},{PP_DAMP,10},{PP_PICK,40},{PP_BODY,70},{P_FIL_TYPE,1},{P_CUTOFF,90},{P_AMP_A,50},{P_AMP_S,127},{P_AMP_R,60},{P_SEND_REV,76}};
OV pv_bassgt[] = {{P_ENGINE,ENG_PLUCK},{PP_EXCITE,1},{PP_BRIGHT,54},{PP_DAMP,54},{PP_PICK,20},{PP_BODY,90},{P_FIL_TYPE,1},{P_CUTOFF,70},{P_AMP_S,127},{P_AMP_R,34},{P_VOICE_MODE,1},{P_LEVEL,104}};

// --- ORGAN ------------------------------------------------------------------
OV pv_orgfull[]= {{P_ENGINE,ENG_ORGAN},{PO_D1,8},{PO_D2,8},{PO_D3,8},{PO_D4,8},{PO_D5,8},{PO_D6,8},{PO_D7,8},{PO_D8,8},{PO_D9,8},{PO_ROTARY,60},{P_FIL_TYPE,0},{P_AMP_A,0},{P_AMP_S,127},{P_AMP_R,10},{P_SEND_REV,50},{P_LEVEL,72}};
OV pv_orgjazz[]= {{P_ENGINE,ENG_ORGAN},{PO_D1,8},{PO_D2,0},{PO_D3,8},{PO_D4,6},{PO_D5,0},{PO_D6,0},{PO_D7,0},{PO_D8,0},{PO_D9,0},{PO_CLICK,70},{PO_ROTARY,40},{P_FIL_TYPE,0},{P_AMP_A,0},{P_AMP_S,127},{P_AMP_R,8},{P_SEND_REV,44}};
OV pv_orgrock[]= {{P_ENGINE,ENG_ORGAN},{PO_D1,8},{PO_D2,6},{PO_D3,8},{PO_D4,8},{PO_D5,4},{PO_D6,6},{PO_D7,2},{PO_D8,4},{PO_D9,8},{PO_CLICK,90},{PO_ROTARY,80},{P_DRIVE,54},{P_FIL_TYPE,0},{P_AMP_A,0},{P_AMP_S,127},{P_AMP_R,8},{P_LEVEL,88}};
OV pv_orgflute[]={{P_ENGINE,ENG_ORGAN},{PO_D1,0},{PO_D2,0},{PO_D3,8},{PO_D4,0},{PO_D5,0},{PO_D6,0},{PO_D7,0},{PO_D8,0},{PO_D9,0},{PO_CLICK,0},{PO_ROTARY,20},{P_FIL_TYPE,0},{P_AMP_A,20},{P_AMP_S,127},{P_AMP_R,30},{P_SEND_REV,70},{P_LEVEL,80}};
OV pv_orgbass[]= {{P_ENGINE,ENG_ORGAN},{PO_D1,8},{PO_D2,0},{PO_D3,5},{PO_D4,0},{PO_D5,0},{PO_D6,0},{PO_D7,0},{PO_D8,0},{PO_D9,0},{PO_CLICK,30},{PO_ROTARY,0},{P_FIL_TYPE,1},{P_CUTOFF,64},{P_AMP_A,0},{P_AMP_S,127},{P_AMP_R,8},{P_VOICE_MODE,1},{P_LEVEL,104}};

// --- CHIP -------------------------------------------------------------------
OV pv_chip[]   = {{P_ENGINE,ENG_CHIP},{PC_DUTY,2},{P_FIL_TYPE,0},{P_AMP_A,0},{P_AMP_D,60},{P_AMP_S,80},{P_AMP_R,6},{P_VOICE_MODE,1},{P_LEVEL,84}};
OV pv_chiparp[]= {{P_ENGINE,ENG_CHIP},{PC_DUTY,1},{PC_ARP,1},{PC_ARP_SPD,90},{P_FIL_TYPE,0},{P_AMP_A,0},{P_AMP_D,70},{P_AMP_S,90},{P_AMP_R,6},{P_VOICE_MODE,1},{P_LEVEL,84}};
OV pv_chiplead[]={{P_ENGINE,ENG_CHIP},{PC_DUTY,2},{PC_VIB_DEP,50},{PC_VIB_DLY,50},{P_FIL_TYPE,0},{P_AMP_A,0},{P_AMP_S,110},{P_AMP_R,8},{P_VOICE_MODE,1},{P_SEND_DLY,40},{P_LEVEL,84}};
OV pv_chipbass[]={{P_ENGINE,ENG_CHIP},{PC_DUTY,3},{PC_O2_LEVEL,60},{PC_O2_SEMI,12},{P_FIL_TYPE,1},{P_CUTOFF,70},{P_AMP_A,0},{P_AMP_S,120},{P_AMP_R,6},{P_VOICE_MODE,1},{P_LEVEL,104}};
OV pv_chipdrum[]={{P_ENGINE,ENG_CHIP},{PC_NOISE_MODE,1},{PC_DUTY,0},{P_FIL_TYPE,2},{P_CUTOFF,80},{P_AMP_A,0},{P_AMP_D,20},{P_AMP_S,0},{P_AMP_R,6},{P_VOICE_MODE,1}};
OV pv_chipglitch[]={{P_ENGINE,ENG_CHIP},{PC_ARP,3},{PC_ARP_SPD,127},{PC_DUTY,0},{P_LFO_WAVE,3},{P_LFO_RATE,110},{P_LFO_AMT,90},{P_LFO_DEST,0},{P_FIL_TYPE,0},{P_AMP_D,30},{P_AMP_S,20},{P_AMP_R,14},{P_VOICE_MODE,1}};
#undef OV

#define PE(n, g, v) { n, g, v, (uint8_t)(sizeof(v) / sizeof(v[0])) }
const PresetEntry kPresets[] = {
    PE("BASIC LEAD",   "LEAD",  pv_basic),
    PE("SUPERSAW",     "LEAD",  pv_super),
    PE("BRIGHT LEAD",  "LEAD",  pv_bright),
    PE("ACID LEAD",    "LEAD",  pv_acidl),
    PE("SQUARE LEAD",  "LEAD",  pv_sqlead),
    PE("SOFT LEAD",    "LEAD",  pv_soft),
    PE("CHIP LEAD",    "LEAD",  pv_chiplead),
    PE("FM BRASS",     "LEAD",  pv_fmbrass),

    PE("SUB BASS",     "BASS",  pv_sub),
    PE("ACID BASS",    "BASS",  pv_acidb),
    PE("REESE BASS",   "BASS",  pv_reese),
    PE("DIRTY BASS",   "BASS",  pv_dirtyb),
    PE("FM BASS",      "BASS",  pv_fmbass),
    PE("FM GROWL",     "BASS",  pv_fmgrowl),
    PE("BASS GUITAR",  "BASS",  pv_bassgt),
    PE("ORGAN BASS",   "BASS",  pv_orgbass),
    PE("CHIP BASS",    "BASS",  pv_chipbass),

    PE("WARM PAD",     "PAD",   pv_warm),
    PE("SPACE PAD",    "PAD",   pv_space),
    PE("ANALOG PAD",   "PAD",   pv_apad),
    PE("WAVE PAD",     "PAD",   pv_wtpad),
    PE("VOWEL PAD",    "PAD",   pv_wtvowel),
    PE("BOWED PAD",    "PAD",   pv_bowed),

    PE("E.PIANO",      "KEYS",  pv_epiano),
    PE("FULL ORGAN",   "KEYS",  pv_orgfull),
    PE("JAZZ ORGAN",   "KEYS",  pv_orgjazz),
    PE("ROCK ORGAN",   "KEYS",  pv_orgrock),
    PE("FLUTE ORGAN",  "KEYS",  pv_orgflute),
    PE("FM ORGAN",     "KEYS",  pv_fmorg),

    PE("NYLON GUITAR", "PLUCK", pv_nylon),
    PE("STEEL STRING", "PLUCK", pv_steel),
    PE("HARP",         "PLUCK", pv_harp),
    PE("MALLET",       "PLUCK", pv_mallet),
    PE("KOTO",         "PLUCK", pv_koto),
    PE("WOOD BLOCK",   "PLUCK", pv_fmwood),

    PE("FM BELL",      "BELL",  pv_fmbell),
    PE("GLASS BELL",   "BELL",  pv_fmglass),
    PE("WAVE BELL",    "BELL",  pv_wtbell),

    PE("CHIP SQUARE",  "CHIP",  pv_chip),
    PE("CHIP ARP",     "CHIP",  pv_chiparp),
    PE("CHIP DRUM",    "CHIP",  pv_chipdrum),
    PE("CHIP GLITCH",  "CHIP",  pv_chipglitch),

    PE("WAVE SWEEP",   "EXP",   pv_wtsweep),
    PE("DIGI PLUCK",   "EXP",   pv_wtdigi),
    PE("GRIT",         "EXP",   pv_wtgrit),
    PE("DRONE",        "EXP",   pv_drone),
    PE("NOISE",        "EXP",   pv_noise),
    PE("LASER",        "EXP",   pv_laser),
};
#undef PE
const uint16_t kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

void loadPreset(Patch& dst, uint16_t index) {
    dst.reset();
    if (index >= kPresetCount) return;
    const PresetEntry& e = kPresets[index];
    // P_ENGINE first: setEngine() rewrites the overlay block with that
    // engine's defaults, so any overlay override in the list has to land
    // afterwards or it would be wiped.
    for (uint8_t i = 0; i < e.n; ++i)
        if (e.ov[i].id == P_ENGINE) dst.setEngine(e.ov[i].val);
    for (uint8_t i = 0; i < e.n; ++i)
        if (e.ov[i].id != P_ENGINE) dst.set(e.ov[i].id, e.ov[i].val);
    strncpy(dst.name, e.name, sizeof(dst.name) - 1);
    dst.name[sizeof(dst.name) - 1] = 0;
}

void formatParam(const Patch& pt, uint8_t id, char* buf, int bufLen) {
    if (id >= P_COUNT || bufLen < 2) { if (bufLen) buf[0] = 0; return; }
    const ParamInfo& in = paramInfo(pt.engine(), id);
    int v = pt.p[id];
    if (in.max == 0) { snprintf(buf, bufLen, "-"); return; }
    switch (in.disp) {
        case D_LIST:    snprintf(buf, bufLen, "%s", in.list && in.list[v] ? in.list[v] : "-"); break;
        case D_PCT:     snprintf(buf, bufLen, "%d%%", (v * 100 + in.max / 2) / in.max); break;
        case D_BIPOLAR: snprintf(buf, bufLen, "%+d", (v - 64) * 100 / 64); break;
        case D_CENTS:   snprintf(buf, bufLen, "%+dc", (v - 64) * 50 / 64); break;
        case D_SEMI:    snprintf(buf, bufLen, "%+d", v - 24); break;
        case D_RATIO: {
            // Half-integer ratios print as "3x" or "3.5x", never "3.0x".
            int half = v + 1;
            if (half & 1) snprintf(buf, bufLen, "%d.5x", half / 2);
            else          snprintf(buf, bufLen, "%dx", half / 2);
            break;
        }
        case D_MS: {
            int ms = (int)envMsFor((uint8_t)v);
            if (ms >= 1000) snprintf(buf, bufLen, "%d.%ds", ms / 1000, (ms % 1000) / 100);
            else            snprintf(buf, bufLen, "%dms", ms);
            break;
        }
        default:        snprintf(buf, bufLen, "%d", v); break;
    }
}

}  // namespace synth
