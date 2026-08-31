// Explicit, versioned project serialisation. Deliberately field-by-field
// rather than a struct dump so the on-card format survives a recompile.
//
// v3 added the engine-overlay patch model, three drum lanes, four drum
// parameters and the per-lane macros. v2 files still load: the drum lanes are
// remapped (v2's lane 4 was CLAP, which is lane 6 now, so without a remap a
// saved beat would come back with its claps playing as rides), and patches are
// converted parameter by parameter.
#include "storage.h"
#include "../music/music.h"
#include <string.h>

namespace synth {

namespace {
struct Writer {
    uint8_t* b; int cap, n = 0; bool ok = true;
    void u8(uint8_t v)  { if (n + 1 > cap) { ok = false; return; } b[n++] = v; }
    void u16(uint16_t v){ u8((uint8_t)(v & 0xFF)); u8((uint8_t)(v >> 8)); }
    void u32(uint32_t v){ u16((uint16_t)(v & 0xFFFF)); u16((uint16_t)(v >> 16)); }
    void bytes(const void* p, int len) {
        if (n + len > cap) { ok = false; return; }
        memcpy(b + n, p, len); n += len;
    }
};
struct Reader {
    const uint8_t* b; int len, n = 0; bool ok = true;
    uint8_t u8()  { if (n + 1 > len) { ok = false; return 0; } return b[n++]; }
    uint16_t u16(){ uint16_t lo = u8(); return (uint16_t)(lo | ((uint16_t)u8() << 8)); }
    uint32_t u32(){ uint32_t lo = u16(); return lo | ((uint32_t)u16() << 16); }
    void bytes(void* p, int l) {
        if (n + l > len) { ok = false; memset(p, 0, l); return; }
        memcpy(p, b + n, l); n += l;
    }
    void skip(int l) { if (n + l > len) { ok = false; return; } n += l; }
};
uint32_t checksum(const uint8_t* b, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
    return h;
}

// ---- v2 compatibility ----------------------------------------------------
constexpr int kV2Drums      = 9;
constexpr int kV2DrumParams = 4;

// v2 lane order was KICK SNARE CHH OHH CLAP TOM RIM CRASH PERC.
const uint8_t kV2LaneMap[kV2Drums] = {
    DL_KICK, DL_SNARE, DL_CHH, DL_OHH, DL_CLAP, DL_TOM, DL_RIM, DL_CRASH, DL_PERC
};

// v2 engine ids were 0 subtractive, 1 FM, 2 wavetable, 3 chip.
const uint8_t kV2EngineMap[4] = { ENG_ANALOG, ENG_FM, ENG_WT, ENG_CHIP };

// 0xFF means "no equivalent in v3"; the engine's default is kept instead.
constexpr uint8_t kNoMap = 0xFF;

// v2 parameter index -> v3 parameter id, for the parameters whose meaning did
// not change. The oscillator block is engine-dependent and handled separately.
const uint8_t kV2Common[38] = {
    kNoMap,                                     //  0 ENGINE, handled first
    kNoMap, kNoMap, kNoMap, kNoMap, kNoMap,     //  1-5  oscillator block
    kNoMap, kNoMap, kNoMap, kNoMap, kNoMap,     //  6-10 oscillator block
    P_FINE, P_GLIDE,                            // 11-12
    P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,         // 13-16
    P_FIL_TYPE, P_CUTOFF, P_RESO, P_FEG_AMT, P_KEYTRK,   // 17-21
    P_FEG_A, P_FEG_D, P_FEG_S, P_FEG_R,         // 22-25
    P_LFO_WAVE, P_LFO_RATE, P_LFO_AMT, P_LFO_DEST, P_LFO_SYNC,  // 26-30
    P_VOICE_MODE, P_VELO_AMT, P_DRIVE, P_LEVEL, // 31-34
    P_SEND_DLY, P_SEND_REV, P_BEND,             // 35-37
};

// The oscillator block, whose meaning depended on the v2 engine. ANALOG maps
// one-for-one; the others only keep what still means the same thing, because
// v2's FM was a single sine pair and v2's wavetable was a four-table morph -
// neither has a faithful equivalent in the new engines.
void convertV2Osc(Patch& dst, uint8_t v2Engine, const uint8_t* v2) {
    switch (kV2EngineMap[v2Engine & 3]) {
        case ENG_ANALOG:
            dst.set(PA_O1_WAVE,   v2[1]);
            dst.set(PA_O1_LEVEL,  v2[2]);
            dst.set(PA_O2_WAVE,   v2[3]);
            dst.set(PA_O2_LEVEL,  v2[4]);
            dst.set(PA_O2_SEMI,   v2[5]);
            dst.set(PA_O2_DETUNE, v2[6]);
            dst.set(PA_SUB_WAVE,  v2[7]);
            dst.set(PA_SUB_LEVEL, v2[8]);
            dst.set(PA_NOISE,     v2[9]);
            dst.set(PA_PW,        v2[10]);
            break;
        case ENG_FM:
            // v2 drove the modulation index from O2 LEVEL and the ratio from
            // O2 SEMI; those become operator 2's level and ratio.
            dst.set(PF_L2, v2[4]);
            dst.set(PF_R2, (uint8_t)clampi((v2[5] - 24) + 1, 0, 31));
            break;
        case ENG_WT:
            dst.set(PW_MORPH,    v2[10]);       // v2's PW was the table morph
            dst.set(PW_O2_LEVEL, v2[4]);
            dst.set(PW_O2_SEMI,  v2[5]);
            dst.set(PW_NOISE,    v2[9]);
            break;
        default:                                 // ENG_CHIP
            dst.set(PC_O2_LEVEL, v2[4]);
            dst.set(PC_O2_SEMI,  v2[5]);
            break;
    }
}
}  // namespace

int projectSerialize(const Project& p, uint8_t* buf, int cap) {
    Writer w{buf, cap};
    w.u32(kProjectMagic);
    w.u16(kProjectVersion);
    w.bytes(p.name, kNameLen);
    w.u16(p.bpm);
    w.u8(p.swing); w.u8(p.scale); w.u8(p.root); w.u8(p.octave);
    w.u8(p.arpOn); w.u8(p.arpMode); w.u8(p.arpRate); w.u8(p.arpOct); w.u8(p.arpGate);

    w.u8(kPatternCount); w.u8(kMelTracks); w.u8(DL_COUNT); w.u8(kMaxSteps);
    for (int i = 0; i < kPatternCount; ++i) {
        const Pattern& pa = p.pat[i];
        w.bytes(pa.name, 9);
        w.u8(pa.length); w.u8(pa.muteMel); w.u16(pa.muteDrum);
        for (int t = 0; t < kMelTracks; ++t)
            for (int s = 0; s < kMaxSteps; ++s) {
                w.u8(pa.mel[t][s].note); w.u8(pa.mel[t][s].vel);
                // Gate needs 5 bits and chord 2, so the file carries them in
                // one byte. That is 1 KB off a project, which is what keeps
                // the whole format inside a borrowed Project - see
                // kProjectBufSize.
                w.u8((uint8_t)((pa.mel[t][s].gate & 0x1F) | ((pa.mel[t][s].chord & 0x03) << 5)));
                w.u8(pa.mel[t][s].flags);
            }
        for (int l = 0; l < DL_COUNT; ++l) w.bytes(pa.drum[l], kMaxSteps);
    }

    w.u8(p.song.length);
    for (int i = 0; i < kSongSlots; ++i) { w.u8(p.song.slot[i].pattern); w.u8(p.song.slot[i].repeat); }

    w.u8(P_COUNT);
    for (int t = 0; t < kMelTracks; ++t) { w.bytes(p.patch[t].name, 13); w.bytes(p.patch[t].p, P_COUNT); }

    w.bytes(p.kit.name, 13);
    w.u8(DP_COUNT); w.u8(DM_COUNT);
    for (int l = 0; l < DL_COUNT; ++l) w.bytes(p.kit.p[l], DP_COUNT);
    for (int l = 0; l < DL_COUNT; ++l) w.bytes(p.kit.m[l], DM_COUNT);

    w.u8(FX_COUNT);
    w.bytes(p.fx.p, FX_COUNT);

    if (!w.ok) return -1;
    uint32_t sum = checksum(buf, w.n);
    w.u32(sum);
    return w.ok ? w.n : -1;
}

bool projectDeserialize(Project& p, const uint8_t* buf, int len) {
    if (len < 16) return false;
    Reader r{buf, len - 4};
    if (r.u32() != kProjectMagic) return false;
    uint16_t ver = r.u16();
    if (ver == 0 || ver > kProjectVersion) return false;
    if (checksum(buf, len - 4) != (uint32_t)(buf[len - 4] | (buf[len - 3] << 8) |
                                             ((uint32_t)buf[len - 2] << 16) | ((uint32_t)buf[len - 1] << 24)))
        return false;

    p.reset();
    r.bytes(p.name, kNameLen); p.name[kNameLen - 1] = 0;
    p.bpm = r.u16();
    p.swing = r.u8(); p.scale = r.u8(); p.root = r.u8(); p.octave = r.u8();
    p.arpOn = r.u8(); p.arpMode = r.u8(); p.arpRate = r.u8(); p.arpOct = r.u8(); p.arpGate = r.u8();

    int nPat = r.u8(), nMel = r.u8(), nDrum = r.u8(), nSteps = r.u8();
    if (nPat > kPatternCount || nMel > kMelTracks || nSteps > kMaxSteps) return false;
    if (nDrum > DL_COUNT) return false;
    for (int i = 0; i < nPat; ++i) {
        Pattern& pa = p.pat[i];
        r.bytes(pa.name, 9); pa.name[8] = 0;
        pa.length = (uint8_t)clampi(r.u8(), 1, kMaxSteps);
        pa.muteMel = r.u8(); pa.muteDrum = r.u16();
        for (int t = 0; t < nMel; ++t)
            for (int s = 0; s < nSteps; ++s) {
                Step& st = pa.mel[t][s];
                st.note = r.u8(); st.vel = r.u8();
                if (ver >= 3) {
                    const uint8_t gc = r.u8();
                    st.gate  = (uint8_t)(gc & 0x1F);
                    st.chord = (uint8_t)((gc >> 5) & 0x03);
                    st.flags = r.u8();
                } else {
                    st.gate  = r.u8();
                    st.flags = r.u8();
                    st.chord = (ver >= 2) ? r.u8() : (uint8_t)CHORD_OFF;
                }
            }
        for (int l = 0; l < nDrum; ++l) {
            // v2 stored nine lanes in a different order; without the remap a
            // saved beat's claps would come back as rides.
            const uint8_t lane = (ver < 3 && l < kV2Drums) ? kV2LaneMap[l] : (uint8_t)l;
            r.bytes(pa.drum[lane], nSteps);
        }
        // A v2 pattern's mute bits refer to the old lane order too.
        if (ver < 3) {
            uint16_t old = pa.muteDrum, remapped = 0;
            for (int l = 0; l < kV2Drums; ++l)
                if (old & (1u << l)) remapped |= (uint16_t)(1u << kV2LaneMap[l]);
            pa.muteDrum = remapped;
        }
    }

    p.song.length = (uint8_t)clampi(r.u8(), 1, kSongSlots);
    for (int i = 0; i < kSongSlots; ++i) { p.song.slot[i].pattern = r.u8(); p.song.slot[i].repeat = r.u8(); }

    int nParams = r.u8();
    for (int t = 0; t < kMelTracks; ++t) {
        r.bytes(p.patch[t].name, 13); p.patch[t].name[12] = 0;
        if (ver >= 3) {
            for (int i = 0; i < nParams; ++i) { uint8_t v = r.u8(); if (i < P_COUNT) p.patch[t].p[i] = v; }
        } else {
            uint8_t v2[64] = {0};
            for (int i = 0; i < nParams; ++i) { uint8_t v = r.u8(); if (i < 64) v2[i] = v; }
            Patch& dst = p.patch[t];
            dst.reset();
            dst.setEngine(kV2EngineMap[v2[0] & 3]);
            const int nCommon = (int)(sizeof(kV2Common) / sizeof(kV2Common[0]));
            for (int i = 0; i < nParams && i < nCommon; ++i)
                if (kV2Common[i] != kNoMap) dst.set(kV2Common[i], v2[i]);
            convertV2Osc(dst, v2[0], v2);
        }
    }

    r.bytes(p.kit.name, 13); p.kit.name[12] = 0;
    if (ver >= 3) {
        int nDp = r.u8(), nDm = r.u8();
        for (int l = 0; l < DL_COUNT; ++l)
            for (int d = 0; d < nDp; ++d) { uint8_t v = r.u8(); if (d < DP_COUNT) p.kit.p[l][d] = v; }
        for (int l = 0; l < DL_COUNT; ++l)
            for (int d = 0; d < nDm; ++d) { uint8_t v = r.u8(); if (d < DM_COUNT) p.kit.m[l][d] = v; }
    } else {
        // v2: nine lanes of four parameters, then two kit-wide send levels.
        // The sends become per-lane, so the kit-wide value seeds every lane.
        loadKit(p.kit, 0);
        for (int l = 0; l < kV2Drums; ++l) {
            const uint8_t lane = kV2LaneMap[l];
            for (int d = 0; d < kV2DrumParams; ++d) p.kit.p[lane][d] = r.u8();
        }
        const uint8_t sendDly = r.u8(), sendRev = r.u8();
        for (int l = 0; l < DL_COUNT; ++l) {
            p.kit.p[l][DP_DLY] = sendDly;
            p.kit.p[l][DP_REV] = sendRev;
        }
        strncpy(p.kit.name, "IMPORTED", sizeof(p.kit.name) - 1);
        p.kit.name[sizeof(p.kit.name) - 1] = 0;
    }

    int nFx = r.u8();
    for (int i = 0; i < nFx; ++i) { uint8_t v = r.u8(); if (i < FX_COUNT) p.fx.p[i] = v; }

    if (!r.ok) return false;
    // Repair anything a corrupted file could have left out of range.
    p.bpm     = (uint16_t)clampi(p.bpm, 40, 300);
    p.swing   = (uint8_t)clampi(p.swing, 0, 100);
    p.octave  = (uint8_t)clampi(p.octave, 0, 8);
    p.scale   = (uint8_t)(p.scale % kScaleCount);
    p.root    = (uint8_t)(p.root % 12);
    p.arpOn   = p.arpOn ? 1 : 0;
    p.arpMode = (uint8_t)(p.arpMode % ARP_MODE_COUNT);
    p.arpRate = (uint8_t)(p.arpRate % 6);
    p.arpOct  = (uint8_t)clampi(p.arpOct, 1, 4);
    p.arpGate = (uint8_t)clampi(p.arpGate, 1, 15);
    for (int t = 0; t < kMelTracks; ++t) {
        Patch& pt = p.patch[t];
        if (pt.p[P_ENGINE] >= ENG_COUNT) pt.p[P_ENGINE] = 0;
        for (uint8_t i = 0; i < P_COUNT; ++i) {
            const ParamInfo& in = paramInfo(pt.engine(), i);
            if (pt.p[i] > in.max) pt.p[i] = in.def;
        }
    }
    for (int l = 0; l < DL_COUNT; ++l) {
        for (int d = 0; d < DP_COUNT; ++d)
            if (p.kit.p[l][d] > 127) p.kit.p[l][d] = 64;
        for (int d = 0; d < DM_COUNT; ++d)
            if (p.kit.m[l][d] > 127) p.kit.m[l][d] = kMacroNeutral;
    }
    for (int i = 0; i < kPatternCount; ++i)
        for (int t = 0; t < kMelTracks; ++t)
            for (int s2 = 0; s2 < kMaxSteps; ++s2) {
                Step& st = p.pat[i].mel[t][s2];
                if (st.note > 127) st.note = 0;
                if (st.vel > 127) st.vel = 100;
                if (st.gate > kGateMax) st.gate = 8;
                if (st.chord >= CHORD_COUNT) st.chord = CHORD_OFF;
            }
    for (int i = 0; i < kPatternCount; ++i)
        for (int j = 0; j < kSongSlots; ++j)
            if (p.song.slot[j].pattern >= kPatternCount) p.song.slot[j].pattern = 0;
    return true;
}

}  // namespace synth
