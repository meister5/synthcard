// Explicit, versioned project serialisation. Deliberately field-by-field
// rather than a struct dump so the on-card format survives a recompile.
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
};
uint32_t checksum(const uint8_t* b, int n) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
    return h;
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
                w.u8(pa.mel[t][s].gate); w.u8(pa.mel[t][s].flags);
            }
        for (int l = 0; l < DL_COUNT; ++l) w.bytes(pa.drum[l], kMaxSteps);
    }

    w.u8(p.song.length);
    for (int i = 0; i < kSongSlots; ++i) { w.u8(p.song.slot[i].pattern); w.u8(p.song.slot[i].repeat); }

    w.u8(P_COUNT);
    for (int t = 0; t < kMelTracks; ++t) { w.bytes(p.patch[t].name, 13); w.bytes(p.patch[t].p, P_COUNT); }

    w.bytes(p.kit.name, 13);
    for (int l = 0; l < DL_COUNT; ++l) w.bytes(p.kit.p[l], DP_COUNT);
    w.u8(p.kit.sendDly); w.u8(p.kit.sendRev);

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
    if (nPat > kPatternCount || nMel > kMelTracks || nDrum > DL_COUNT || nSteps > kMaxSteps) return false;
    for (int i = 0; i < nPat; ++i) {
        Pattern& pa = p.pat[i];
        r.bytes(pa.name, 9); pa.name[8] = 0;
        pa.length = (uint8_t)clampi(r.u8(), 1, kMaxSteps);
        pa.muteMel = r.u8(); pa.muteDrum = r.u16();
        for (int t = 0; t < nMel; ++t)
            for (int s = 0; s < nSteps; ++s) {
                pa.mel[t][s].note = r.u8(); pa.mel[t][s].vel = r.u8();
                pa.mel[t][s].gate = r.u8(); pa.mel[t][s].flags = r.u8();
            }
        for (int l = 0; l < nDrum; ++l) r.bytes(pa.drum[l], nSteps);
    }

    p.song.length = (uint8_t)clampi(r.u8(), 1, kSongSlots);
    for (int i = 0; i < kSongSlots; ++i) { p.song.slot[i].pattern = r.u8(); p.song.slot[i].repeat = r.u8(); }

    int nParams = r.u8();
    for (int t = 0; t < kMelTracks; ++t) {
        r.bytes(p.patch[t].name, 13); p.patch[t].name[12] = 0;
        for (int i = 0; i < nParams; ++i) { uint8_t v = r.u8(); if (i < P_COUNT) p.patch[t].p[i] = v; }
    }

    r.bytes(p.kit.name, 13); p.kit.name[12] = 0;
    for (int l = 0; l < DL_COUNT; ++l) r.bytes(p.kit.p[l], DP_COUNT);
    p.kit.sendDly = r.u8(); p.kit.sendRev = r.u8();

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
    for (int t = 0; t < kMelTracks; ++t)
        for (uint8_t i = 0; i < P_COUNT; ++i)
            if (p.patch[t].p[i] > kSynthParamInfo[i].max) p.patch[t].p[i] = kSynthParamInfo[i].def;
    for (int l = 0; l < DL_COUNT; ++l)
        for (int d = 0; d < DP_COUNT; ++d)
            if (p.kit.p[l][d] > 127) p.kit.p[l][d] = 64;
    for (int i = 0; i < kPatternCount; ++i)
        for (int t = 0; t < kMelTracks; ++t)
            for (int s2 = 0; s2 < kMaxSteps; ++s2) {
                Step& st = p.pat[i].mel[t][s2];
                if (st.note > 127) st.note = 0;
                if (st.vel > 127) st.vel = 100;
                if (st.gate > kGateMax) st.gate = 8;
            }
    for (int i = 0; i < kPatternCount; ++i)
        for (int j = 0; j < kSongSlots; ++j)
            if (p.song.slot[j].pattern >= kPatternCount) p.song.slot[j].pattern = 0;
    return true;
}

}  // namespace synth
