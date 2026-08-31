// SynthCard - one draw + input handler per screen.
#include "ui.h"
#include "../app.h"
#include "../music/music.h"
#include <M5Cardputer.h>

namespace synth {
using namespace ui;

// ===========================================================================
// helpers
// ===========================================================================
static void paramRow(M5Canvas& g, int x, int y, int w, const char* name, const char* value,
                     float frac, bool bipolar, bool selected) {
    if (selected) panel(g, x - 2, y - 2, w + 4, 22, C_PANEL2, C_ACCENT);
    textAt(g, x, y, name, selected ? C_ACCENT : C_DIM, &fonts::Font0);
    textAt(g, x + w, y, value, C_TEXT, &fonts::Font0, textdatum_t::top_right);
    if (bipolar) barBipolar(g, x, y + 11, w, 5, frac * 2.0f - 1.0f, selected ? C_ACCENT : C_ACC2, C_FAINT);
    else         bar(g, x, y + 11, w, 5, frac, selected ? C_ACCENT : C_ACC2, C_FAINT);
}

static void trackBadge(M5Canvas& g, int x, int y, uint8_t track, bool muted) {
    const char* n = track == 0 ? "LEAD" : "BASS";
    uint16_t c = muted ? C_DIM : (track == 0 ? C_ACCENT : C_ACC2);
    panel(g, x, y, 34, 11, C_PANEL2, c);
    textAt(g, x + 17, y + 2, n, c, &fonts::Font0, textdatum_t::top_center);
}

static Patch& curPatch(App& a) { return a.proj.patch[a.engine.liveTrack()]; }
static Pattern& curPattern(App& a) { return a.proj.pat[a.engine.seq().currentPattern()]; }

// ===========================================================================
// PLAY
// ===========================================================================
static void drawPlay(App& a, int, int y0, int w, int h) {
    M5Canvas& g = uiCanvas();
    const uint8_t tr = a.engine.liveTrack();
    Patch& pt = a.proj.patch[tr];
    Pattern& pat = curPattern(a);

    trackBadge(g, 4, y0 + 2, tr, (pat.muteMel >> tr) & 1);
    char buf[48];
    snprintf(buf, sizeof(buf), "%02d/%02d", (int)(a.presetIndex[tr] % kPresetCount) + 1, (int)kPresetCount);
    textAt(g, 42, y0 + 4, buf, C_FAINT, &fonts::Font0);

    textAt(g, 4, y0 + 15, pt.name, C_TEXT, &fonts::Font2);

    // The engine, its own first parameter (whatever that means for this
    // engine) and the filter - a one-line summary that stays true for all six.
    char oscTxt[16], filTxt[16];
    formatParam(pt, P_ENG_BASE, oscTxt, sizeof(oscTxt));
    formatParam(pt, P_FIL_TYPE, filTxt, sizeof(filTxt));
    snprintf(buf, sizeof(buf), "%s  %s  %s", kEngineNames[pt.engine()], oscTxt, filTxt);
    textAt(g, 4, y0 + 34, buf, C_DIM, &fonts::Font0);

    // two live macros: cutoff and resonance
    formatParam(pt, P_CUTOFF, buf, sizeof(buf));
    paramRow(g, 4, y0 + 46, 108, "CUTOFF", buf, pt.norm(P_CUTOFF), false, false);
    formatParam(pt, P_RESO, buf, sizeof(buf));
    paramRow(g, 4, y0 + 68, 108, "RESO", buf, pt.norm(P_RESO), false, false);

    // scope
    const int sx = 128, sy = y0 + 2, sw = 108, sh = 46;
    panel(g, sx, sy, sw, sh, C_PANEL, C_GRID);
    const int8_t* sc = a.engine.scope();
    const int mid = sy + sh / 2;
    int prev = mid;
    for (int i = 0; i < sw - 4; ++i) {
        int v = sc[(i * kScopeSize) / (sw - 4)];
        int py = mid - (v * (sh / 2 - 2)) / 127;
        if (i) g.drawLine(sx + 1 + i, prev, sx + 2 + i, py, C_ACC2);
        prev = py;
    }

    // performance state
    snprintf(buf, sizeof(buf), "OCT %d", a.proj.octave);
    textAt(g, sx, sy + sh + 4, buf, C_DIM, &fonts::Font0);
    snprintf(buf, sizeof(buf), "%s", a.proj.scale ? kScales[a.proj.scale].name : "CHROM");
    textAt(g, sx + 44, sy + sh + 4, buf, C_DIM, &fonts::Font0);
    if (a.chordMode) {
        snprintf(buf, sizeof(buf), "CHORD %s", kChordNames[a.chordMode]);
        textAt(g, sx, sy + sh + 15, buf, C_ACC2, &fonts::Font0);
    }
    if (a.proj.arpOn) {
        snprintf(buf, sizeof(buf), "ARP %s %s", kArpModeNames[a.proj.arpMode % ARP_MODE_COUNT],
                 kArpRateNames[a.proj.arpRate % 6]);
        textAt(g, sx, sy + sh + 26, buf, C_ACCENT, &fonts::Font0);
    }

    // voice meter
    for (int i = 0; i < a.engine.maxVoices(); ++i)
        g.fillRect(sx + i * 7, sy + sh + 38, 5, 4, i < a.engine.activeVoices() ? C_ACC2 : C_FAINT);

    // Step strip: the top band is this track's notes, the thin band under it is
    // the kick. Without it you cannot see a recorded note land.
    Sequencer& s = a.engine.seq();
    const int len = patternLength(a.proj, s.currentPattern());
    const int pages = (len + 15) / 16;
    const int page = (s.playing() && pages > 1) ? (s.step() / 16) % pages : 0;
    const int base = page * 16;
    const int py = y0 + h - 13, cw = (w - 8) / 16;
    for (int i = 0; i < 16; ++i) {
        const int st = base + i;
        const int cx = 4 + i * cw;
        if (st >= len) { g.fillRect(cx, py, cw - 2, 10, C_BG); continue; }
        g.fillRect(cx, py, cw - 2, 10, (st % 4 == 0) ? C_GRID : C_PANEL);
        if (pat.mel[tr][st].on())
            g.fillRect(cx, py, cw - 2, 6, s.recording() ? C_REC : C_ACCENT);
        if (pat.drum[DL_KICK][st]) g.fillRect(cx, py + 7, cw - 2, 3, C_ACC2);
        if (s.playing() && st == s.step()) g.drawRect(cx - 1, py - 1, cw, 12, C_TEXT);
    }
    if (pages > 1) {
        snprintf(buf, sizeof(buf), "%d/%d", page + 1, pages);
        textAt(g, w - 2, py - 10, buf, C_FAINT, &fonts::Font0, textdatum_t::top_right);
    }
}

static bool actPlay(App& a, const Action& act) {
    Patch& pt = curPatch(a);
    switch (act.act) {
        case A_CURSOR_PREV: case A_CURSOR_NEXT: {
            int d = (act.act == A_CURSOR_NEXT) ? 4 : -4;
            pt.set(P_RESO, pt.get(P_RESO) + d);
            char v[16]; formatParam(pt, P_RESO, v, sizeof(v));
            showParam(a, "RESO", v, pt.norm(P_RESO));
            return true;
        }
        case A_VALUE_DOWN: case A_VALUE_UP: {
            int d = (act.act == A_VALUE_UP ? 1 : -1) * (act.arg > 1 ? 8 : 3);
            pt.set(P_CUTOFF, pt.get(P_CUTOFF) + d);
            char v[16]; formatParam(pt, P_CUTOFF, v, sizeof(v));
            showParam(a, "CUTOFF", v, pt.norm(P_CUTOFF));
            return true;
        }
        case A_UP:   a.engine.setLiveTrack(0); showToast(a, "TRACK LEAD"); return true;
        case A_DOWN: a.engine.setLiveTrack(1); showToast(a, "TRACK BASS"); return true;
        // Number row doubles as eight finger-drum pads while jamming.
        case A_STEP: a.engine.drumHit((uint8_t)act.arg, 110); return true;
        default: return false;
    }
}

// ===========================================================================
// DRUM
// ===========================================================================
// The lane grid scrolls: twelve lanes will not fit the body at a readable row
// height, so eight are shown in a window that follows the cursor.
static constexpr int kDrumVisible = 8;

// One cursor spans the macros and the detail parameters: 0..2 are PUNCH /
// TONE / SPACE, 3..10 are the eight per-lane parameters. Walking off the end
// of the macros lands on the detail page, which is the same wrap the SOUND
// screen uses, so paging is never something you have to think about.
static inline bool drumCursorIsMacro(uint8_t c) { return c < DM_COUNT; }
static inline uint8_t drumCursorParam(uint8_t c) { return (uint8_t)(c - DM_COUNT); }

static void drawDrum(App& a, int, int y0, int w, int h) {
    M5Canvas& g = uiCanvas();
    Pattern& p = curPattern(a);
    Sequencer& s = a.engine.seq();
    const int len = patternLength(a.proj, s.currentPattern());
    const int pages = (len + 15) / 16;
    const int page = a.drumPage % (pages ? pages : 1);
    const int base = page * 16;

    const int laneH = 10, gx = 26, cellW = 13;
    const int playStep = s.playing() ? s.step() : -1;

    // Keep the selected lane inside the window without letting the window
    // run off either end of the list.
    int top = (int)a.drumLane - kDrumVisible / 2;
    if (top > DL_COUNT - kDrumVisible) top = DL_COUNT - kDrumVisible;
    if (top < 0) top = 0;

    for (int row = 0; row < kDrumVisible; ++row) {
        const int l = top + row;
        int ly = y0 + row * laneH;
        bool sel = l == a.drumLane;
        bool mute = (p.muteDrum >> l) & 1;
        if (sel) g.fillRect(0, ly, w, laneH - 1, C_PANEL);
        textAt(g, 2, ly + 1, kDrumShort[l], mute ? C_FAINT : (sel ? C_ACCENT : C_DIM), &fonts::Font0);
        // A lane that chokes others is marked, so the behaviour is visible
        // rather than a surprise when the open hat cuts off.
        if (kChokeGroup[l]) textAt(g, 20, ly + 1, "\xB7", C_FAINT, &fonts::Font0);
        for (int i = 0; i < 16; ++i) {
            int st = base + i;
            int cx = gx + i * cellW;
            if (st >= len) { g.drawRect(cx, ly, cellW - 2, laneH - 2, C_BG); continue; }
            uint8_t v = p.drum[l][st];
            uint16_t col;
            if (v) col = mute ? C_FAINT : (v > 110 ? C_ACCENT : C_ACC2);
            else   col = (st % 4 == 0) ? C_GRID : C_PANEL;
            g.fillRect(cx, ly, cellW - 2, laneH - 2, col);
            if (st == playStep) g.drawRect(cx - 1, ly - 1, cellW, laneH, C_TEXT);
        }
    }
    // Scroll markers, so it is obvious there are lanes off-screen.
    if (top > 0) textAt(g, 18, y0, "\x18", C_FAINT, &fonts::Font0);
    if (top + kDrumVisible < DL_COUNT)
        textAt(g, 18, y0 + (kDrumVisible - 1) * laneH + 1, "\x19", C_FAINT, &fonts::Font0);

    const int py = y0 + kDrumVisible * laneH + 1;
    char buf[40];
    const DrumKit& kit = a.proj.kit;
    textAt(g, 2, py, kDrumNames[a.drumLane], C_TEXT, &fonts::Font0);

    if (drumCursorIsMacro(a.drumParam)) {
        textAt(g, 2, py + 11, kit.name, C_FAINT, &fonts::Font0);
        for (int i = 0; i < DM_COUNT; ++i) {
            const int px = 62 + i * 60;
            const bool sel = i == a.drumParam;
            const int v = (int)kit.macro(a.drumLane, (uint8_t)i) - kMacroNeutral;
            textAt(g, px, py, kDrumMacroNames[i], sel ? C_ACCENT : C_FAINT, &fonts::Font0);
            snprintf(buf, sizeof(buf), "%+d", v * 100 / 64);
            textAt(g, px + 54, py, buf, sel ? C_TEXT : C_FAINT, &fonts::Font0,
                   textdatum_t::top_right);
            barBipolar(g, px, py + 10, 54, 4, (float)v / 64.0f,
                       sel ? C_ACCENT : C_ACC2, C_FAINT);
        }
    } else {
        // Eight parameters as two rows of four.
        for (int i = 0; i < DP_COUNT; ++i) {
            const int col = i % 4, row = i / 4;
            const int px = 62 + col * 45, pyy = py + row * 12;
            const bool sel = (uint8_t)i == drumCursorParam(a.drumParam);
            const uint8_t v = kit.get(a.drumLane, (uint8_t)i);
            textAt(g, px, pyy, kDrumParamNames[i], sel ? C_ACCENT : C_FAINT, &fonts::Font0);
            bar(g, px, pyy + 8, 40, 3, v * (1.0f / 127.0f), sel ? C_ACCENT : C_ACC2, C_FAINT);
        }
    }
}

static bool actDrum(App& a, const Action& act) {
    Pattern& p = curPattern(a);
    const int len = patternLength(a.proj, a.engine.seq().currentPattern());
    const int pages = (len + 15) / 16;
    DrumKit& kit = a.proj.kit;
    switch (act.act) {
        case A_STEP: {
            const int st = (a.drumPage % (pages ? pages : 1)) * 16 + act.arg;
            if (st < 0 || st >= len) return true;
            uint8_t& c = p.drum[a.drumLane][st];
            c = c ? 0 : 100;
            if (c) a.engine.drumHit(a.drumLane, c);
            return true;
        }
        case A_UP:   a.drumLane = (uint8_t)((a.drumLane + DL_COUNT - 1) % DL_COUNT); return true;
        case A_DOWN: a.drumLane = (uint8_t)((a.drumLane + 1) % DL_COUNT); return true;
        case A_LEFT: if (pages > 1) a.drumPage = (uint8_t)((a.drumPage + pages - 1) % pages); return true;
        case A_RIGHT:if (pages > 1) a.drumPage = (uint8_t)((a.drumPage + 1) % pages); return true;
        case A_CURSOR_PREV:
            a.drumParam = (uint8_t)((a.drumParam + kDrumCursorCount - 1) % kDrumCursorCount);
            return true;
        case A_CURSOR_NEXT:
            a.drumParam = (uint8_t)((a.drumParam + 1) % kDrumCursorCount);
            return true;
        case A_VALUE_DOWN: case A_VALUE_UP: {
            const int d = (act.act == A_VALUE_UP ? 1 : -1) * (act.arg > 1 ? 10 : 2);
            char v[16];
            if (drumCursorIsMacro(a.drumParam)) {
                const uint8_t mi = a.drumParam;
                kit.setMacro(a.drumLane, mi, kit.macro(a.drumLane, mi) + d);
                const int off = (int)kit.macro(a.drumLane, mi) - kMacroNeutral;
                snprintf(v, sizeof(v), "%+d", off * 100 / 64);
                showParam(a, kDrumMacroNames[mi], v, kit.macro(a.drumLane, mi) / 127.0f);
            } else {
                const uint8_t par = drumCursorParam(a.drumParam);
                kit.set(a.drumLane, par, kit.get(a.drumLane, par) + d);
                snprintf(v, sizeof(v), "%d%%", kit.get(a.drumLane, par) * 100 / 127);
                showParam(a, kDrumParamNames[par], v, kit.get(a.drumLane, par) / 127.0f);
            }
            a.engine.drumHit(a.drumLane, 100);
            return true;
        }
        case A_MUTE:
            p.muteDrum ^= (uint16_t)(1u << a.drumLane);
            showToast(a, ((p.muteDrum >> a.drumLane) & 1) ? "LANE MUTED" : "LANE ON");
            return true;
        case A_PRESET_PREV: applyKit(a, a.kitIndex - 1); return true;
        case A_PRESET_NEXT: applyKit(a, a.kitIndex + 1); return true;
        case A_CLEAR_TRACK:
            snapshotUndo(a);
            p.clearTrack((uint8_t)(kMelTracks + a.drumLane));
            showToast(a, "LANE CLEARED");
            return true;
        case A_EUCLID: {
            snapshotUndo(a);
            a.euclidHits = (uint8_t)(a.euclidHits % (len ? len : 16) + 1);
            applyEuclid(p, a.drumLane, a.euclidHits, a.euclidRot, 100);
            char m[32]; snprintf(m, sizeof(m), "EUCLID %d/%d", a.euclidHits, len);
            showToast(a, m);
            return true;
        }
        default: return false;
    }
}

// ===========================================================================
// SEQ
// ===========================================================================
static const char* const kSeqFields[] = {"NOTE", "VEL", "GATE", "CHRD", "PROB"};
constexpr int kSeqFieldCount = 5;

static void drawSeq(App& a, int, int y0, int w, int h) {
    M5Canvas& g = uiCanvas();
    Pattern& p = curPattern(a);
    Sequencer& s = a.engine.seq();
    const int len = patternLength(a.proj, s.currentPattern());
    const int pages = (len + 15) / 16;
    const int page = a.seqPage % (pages ? pages : 1);
    const int base = page * 16;
    const uint8_t tr = a.seqTrack % kMelTracks;

    char buf[48];
    trackBadge(g, 2, y0 + 1, tr, (p.muteMel >> tr) & 1);
    snprintf(buf, sizeof(buf), "LEN %d  PG %d/%d", len, page + 1, pages);
    textAt(g, 40, y0 + 3, buf, C_DIM, &fonts::Font0);
    // Pattern bank, so "which pattern am I editing" is never a guess.
    textAt(g, 116, y0 + 3, "PTN", C_FAINT, &fonts::Font0);
    for (int i = 0; i < kPatternCount; ++i) {
        int px = 138 + i * 12;
        bool cur = i == s.currentPattern();
        bool q   = i == s.queuedPattern();
        g.fillRect(px, y0 + 2, 10, 9, cur ? C_ACCENT : (q ? C_ACC2 : C_PANEL));
        snprintf(buf, sizeof(buf), "%d", i + 1);
        textAt(g, px + 5, y0 + 3, buf, cur ? C_BG : C_DIM, &fonts::Font0, textdatum_t::top_center);
    }

    // auto-ranged piano roll
    int lo = 127, hi = 0;
    uint8_t voicing[4];
    for (int i = 0; i < len; ++i) {
        const Step& st0 = p.mel[tr][i];
        if (!st0.note) continue;
        int vn = buildChord(st0.note, st0.chord, a.proj.root, a.proj.scale, voicing);
        for (int k = 0; k < vn; ++k) {
            if (voicing[k] < lo) lo = voicing[k];
            if (voicing[k] > hi) hi = voicing[k];
        }
    }
    if (lo > hi) { lo = 48; hi = 72; }
    if (hi - lo < 12) { int c = (lo + hi) / 2; lo = c - 6; hi = c + 6; }
    lo -= 1; hi += 1;

    const int rollY = y0 + 15, rollH = 50, gx = 4, cellW = 14;
    g.fillRect(gx, rollY, 16 * cellW, rollH, C_PANEL);
    const int playStep = s.playing() ? s.step() : -1;
    for (int i = 0; i < 16; ++i) {
        int st = base + i;
        int cx = gx + i * cellW;
        if (st % 4 == 0) g.drawFastVLine(cx, rollY, rollH, C_GRID);
        if (st >= len) { g.fillRect(cx, rollY, cellW - 1, rollH, C_BG); continue; }
        if (st == playStep) g.fillRect(cx, rollY, cellW - 1, rollH, C_PANEL2);
        const Step& stp = p.mel[tr][st];
        if (stp.note) {
            // A note longer than one step draws across the steps it covers, and
            // a chord draws every tone it will actually voice.
            const int gw = clampi((cellW * gateSixteenths(stp.gate)) / 16 - 1, 2, 16 * cellW - (cx - gx));
            const int span = (hi - lo) ? (hi - lo) : 1;
            const uint16_t root = (stp.flags & SF_MUTE) ? C_FAINT : (stp.vel > 110 ? C_ACCENT : C_ACC2);
            const int vn = buildChord(stp.note, stp.chord, a.proj.root, a.proj.scale, voicing);
            for (int k = vn - 1; k >= 0; --k) {
                int ny = rollY + rollH - 3 - ((voicing[k] - lo) * (rollH - 6)) / span;
                g.fillRect(cx + 1, ny, gw, 3, k == 0 ? root : C_FAINT);
            }
            int ny0 = rollY + rollH - 3 - ((stp.note - lo) * (rollH - 6)) / span;
            if (stp.flags & SF_SLIDE) g.drawFastHLine(cx + 1, ny0 + 4, cellW - 3, C_ACCENT);
            if (stp.prob()) g.drawPixel(cx + cellW - 3, rollY + 2, C_REC);
        }
        if (st == (base + (a.seqStep % 16))) g.drawRect(cx, rollY, cellW - 1, rollH, C_ACCENT);
    }

    // selected-step detail
    const int st = base + (a.seqStep % 16);
    const Step& sel = p.mel[tr][clampi(st, 0, kMaxSteps - 1)];
    const int dy = rollY + rollH + 5;
    char nn[8] = "---";
    if (sel.note) noteName(sel.note, nn, sizeof(nn));
    snprintf(buf, sizeof(buf), "STEP %02d", st + 1);
    textAt(g, 4, dy, buf, C_DIM, &fonts::Font0);

    // One entry per kSeqFieldCount, all of them assigned: this array feeds
    // drawString directly, so a missing slot is a null-pointer crash.
    char v0[8], v1[8], v2[8], v3[8], v4[8];
    snprintf(v0, sizeof(v0), "%s", nn);
    snprintf(v1, sizeof(v1), "%d", sel.vel);
    formatGate(sel.gate, v2, sizeof(v2));
    snprintf(v3, sizeof(v3), "%s", kChordNames[sel.chord % CHORD_COUNT]);
    snprintf(v4, sizeof(v4), "%s", sel.prob() ? (sel.prob() >= 8 ? "ALW" : "RND") : "ALW");
    const char* vals[kSeqFieldCount] = {v0, v1, v2, v3, v4};
    static_assert(sizeof(vals) / sizeof(vals[0]) == kSeqFieldCount,
                  "every SEQ field needs a value string");
    for (int i = 0; i < kSeqFieldCount; ++i) {
        const int px = 46 + i * 39;
        const bool s2 = i == a.seqField;
        if (s2) panel(g, px - 3, dy - 2, 37, 22, C_PANEL2, C_ACCENT);
        textAt(g, px, dy, kSeqFields[i], s2 ? C_ACCENT : C_FAINT, &fonts::Font0);
        textAt(g, px, dy + 10, vals[i], C_TEXT, &fonts::Font0);
    }
    if (sel.flags & SF_MUTE) textAt(g, 4, dy + 11, "MUTE", C_REC, &fonts::Font0);
}

static bool actSeq(App& a, const Action& act) {
    Pattern& p = curPattern(a);
    const int len = patternLength(a.proj, a.engine.seq().currentPattern());
    const int pages = (len + 15) / 16;
    const uint8_t tr = a.seqTrack % kMelTracks;
    const int st = clampi((a.seqPage % (pages ? pages : 1)) * 16 + (a.seqStep % 16), 0, kMaxSteps - 1);
    Step& s = p.mel[tr][st];

    switch (act.act) {
        case A_STEP: a.seqStep = (uint8_t)act.arg; return true;
        case A_LEFT:  a.seqStep = (uint8_t)((a.seqStep + 15) % 16); return true;
        case A_RIGHT: a.seqStep = (uint8_t)((a.seqStep + 1) % 16); return true;
        case A_UP:   a.seqTrack = (uint8_t)((a.seqTrack + kMelTracks - 1) % kMelTracks); return true;
        case A_DOWN: a.seqTrack = (uint8_t)((a.seqTrack + 1) % kMelTracks); return true;
        case A_CURSOR_PREV: a.seqField = (uint8_t)((a.seqField + kSeqFieldCount - 1) % kSeqFieldCount); return true;
        case A_CURSOR_NEXT: a.seqField = (uint8_t)((a.seqField + 1) % kSeqFieldCount); return true;
        case A_CONFIRM:
            if (s.note) s.flags ^= SF_MUTE;
            else { s.note = liveBaseNote(a); s.vel = 100; s.gate = 8; }
            return true;
        case A_BACK: s.note = 0; s.flags = 0; return true;
        case A_MUTE: p.muteMel ^= (uint8_t)(1u << tr); return true;
        case A_CLEAR_TRACK: snapshotUndo(a); p.clearTrack(tr); showToast(a, "TRACK CLEARED"); return true;
        case A_VALUE_DOWN: case A_VALUE_UP: {
            int dir = (act.act == A_VALUE_UP) ? 1 : -1;
            int big = act.arg > 1 ? 12 : 1;
            char nm[12], vl[12];
            switch (a.seqField) {
                case 0:
                    if (!s.note) s.note = liveBaseNote(a);
                    else s.note = (uint8_t)clampi(s.note + dir * big, 1, 127);
                    noteName(s.note, vl, sizeof(vl));
                    snprintf(nm, sizeof(nm), "NOTE");
                    showParam(a, nm, vl, (s.note - 24) / 84.0f);
                    a.engine.noteOn(tr, s.note, s.vel);
                    a.engine.post(EV_NOTE_OFF, tr, s.note);
                    break;
                case 1:
                    s.vel = (uint8_t)clampi(s.vel + dir * (big > 1 ? 10 : 4), 1, 127);
                    snprintf(vl, sizeof(vl), "%d", s.vel);
                    showParam(a, "VELOCITY", vl, s.vel / 127.0f);
                    break;
                case 2:
                    // 0..15 is a fraction of one step, 16..31 is 2..17 whole
                    // steps, so a note can be held across the bar.
                    s.gate = (uint8_t)clampi(s.gate + dir * (big > 1 ? 4 : 1), 0, kGateMax);
                    formatGate(s.gate, vl, sizeof(vl));
                    showParam(a, "GATE", vl, s.gate / (float)kGateMax);
                    break;
                case 3: {
                    s.chord = (uint8_t)((s.chord + CHORD_COUNT + dir) % CHORD_COUNT);
                    showParam(a, "CHORD", kChordNames[s.chord], -1.0f);
                    // Audition the whole voicing, not just the root.
                    if (s.note) {
                        uint8_t ch[4];
                        int cn = buildChord(s.note, s.chord, a.proj.root, a.proj.scale, ch);
                        for (int k = 0; k < cn; ++k) {
                            a.engine.noteOn(tr, ch[k], s.vel);
                            a.engine.post(EV_NOTE_OFF, tr, ch[k]);
                        }
                    }
                    break;
                }
                default: {
                    int pr = clampi(s.prob() + dir, 0, 8);
                    s.setProb((uint8_t)pr);
                    if (pr == 0 || pr >= 8) snprintf(vl, sizeof(vl), "ALWAYS");
                    else snprintf(vl, sizeof(vl), "%d/8", pr);
                    showParam(a, "PROBABILITY", vl, pr / 8.0f);
                    break;
                }
            }
            return true;
        }
        default: return false;
    }
}

// ===========================================================================
// SOUND
// ===========================================================================
static void drawSound(App& a, int, int y0, int w, int h) {
    M5Canvas& g = uiCanvas();
    const uint8_t tr = a.engine.liveTrack();
    Patch& pt = a.proj.patch[tr];
    const uint8_t eng = pt.engine();
    const uint8_t nPages = synthPageCount(eng);
    const ParamPage& pg = synthPages(eng)[a.soundPage % nPages];

    trackBadge(g, 2, y0 + 1, tr, false);
    textAt(g, 40, y0 + 3, pt.name, C_TEXT, &fonts::Font0);
    char buf[40];
    snprintf(buf, sizeof(buf), "%s %d/%d", pg.name, (a.soundPage % nPages) + 1, nPages);
    textAt(g, w - 3, y0 + 3, buf, C_ACCENT, &fonts::Font0, textdatum_t::top_right);
    g.drawFastHLine(0, y0 + 13, w, C_GRID);

    for (int i = 0; i < 6; ++i) {
        int col = i % 2, row = i / 2;
        int x = 6 + col * 118, y = y0 + 19 + row * 27;
        if (i >= pg.n) continue;
        uint8_t id = pg.p[i];
        const ParamInfo& in = paramInfo(eng, id);
        formatParam(pt, id, buf, sizeof(buf));
        paramRow(g, x, y, 104, in.name, buf, pt.norm(id), in.disp == D_BIPOLAR || in.disp == D_CENTS,
                 i == a.soundCursor);
    }
}

static bool actSound(App& a, const Action& act) {
    const uint8_t tr = a.engine.liveTrack();
    Patch& pt = a.proj.patch[tr];
    const uint8_t eng = pt.engine();
    const uint8_t nPages = synthPageCount(eng);
    const ParamPage& pg = synthPages(eng)[a.soundPage % nPages];
    switch (act.act) {
        // The cursor wraps across page boundaries: past the last parameter of
        // a page you land on the first of the next one. That is what removes
        // the need to interleave two different navigation gestures while
        // editing a sound. The page keys still work as a shortcut.
        case A_CURSOR_PREV:
            if (a.soundCursor == 0) {
                a.soundPage = (uint8_t)((a.soundPage + nPages - 1) % nPages);
                const uint8_t pn = synthPages(eng)[a.soundPage].n;
                a.soundCursor = (uint8_t)(pn ? pn - 1 : 0);
            } else --a.soundCursor;
            return true;
        case A_CURSOR_NEXT:
            if (a.soundCursor + 1 >= pg.n) {
                a.soundPage = (uint8_t)((a.soundPage + 1) % nPages);
                a.soundCursor = 0;
            } else ++a.soundCursor;
            return true;
        case A_LEFT:
            a.soundPage = (uint8_t)((a.soundPage + nPages - 1) % nPages);
            a.soundCursor = 0; return true;
        case A_RIGHT:
            a.soundPage = (uint8_t)((a.soundPage + 1) % nPages);
            a.soundCursor = 0; return true;
        case A_UP:   a.engine.setLiveTrack(0); return true;
        case A_DOWN: a.engine.setLiveTrack(1); return true;
        case A_STEP:
            if (act.arg < nPages) { a.soundPage = (uint8_t)act.arg; a.soundCursor = 0; }
            return true;
        case A_VALUE_DOWN: case A_VALUE_UP: {
            uint8_t id = pg.p[a.soundCursor % pg.n];
            const ParamInfo& in = paramInfo(eng, id);
            if (in.max == 0) return true;
            int step = (in.disp == D_LIST) ? 1 : (act.arg > 1 ? 10 : 2);
            const int next = pt.get(id) + (act.act == A_VALUE_UP ? step : -step);
            // Changing the engine reshapes the overlay block, so it goes
            // through setEngine() and the cursor returns to page one.
            if (id == P_ENGINE) {
                pt.setEngine((uint8_t)clampi(next, 0, ENG_COUNT - 1));
                a.soundPage = 0; a.soundCursor = 0;
                showParam(a, "ENGINE", kEngineNames[pt.engine()],
                          (float)pt.engine() / (float)(ENG_COUNT - 1));
                return true;
            }
            pt.set(id, next);
            char v[20];
            formatParam(pt, id, v, sizeof(v));
            showParam(a, in.name, v, pt.norm(id));
            return true;
        }
        default: return false;
    }
}

// ===========================================================================
// FX
// ===========================================================================
static void drawFx(App& a, int, int y0, int w, int h) {
    M5Canvas& g = uiCanvas();
    char buf[24];
    textAt(g, 4, y0 + 1, "MASTER FX", C_ACCENT, &fonts::Font0);
    snprintf(buf, sizeof(buf), "CPU %d%%  V %d/%d", (int)(a.engine.cpuLoad() * 100),
             a.engine.activeVoices(), a.engine.maxVoices());
    textAt(g, w - 3, y0 + 1, buf, C_FAINT, &fonts::Font0, textdatum_t::top_right);
    g.drawFastHLine(0, y0 + 11, w, C_GRID);

    for (int i = 0; i < FX_COUNT; ++i) {
        int col = i / 8, row = i % 8;
        int x = 6 + col * 118, y = y0 + 13 + row * 12;
        formatFx(a.proj.fx, (uint8_t)i, buf, sizeof(buf));
        bool sel = i == a.fxCursor;
        if (sel) g.fillRect(x - 4, y - 1, 112, 11, C_PANEL2);
        textAt(g, x, y, kFxInfo[i].name, sel ? C_ACCENT : C_DIM, &fonts::Font0);
        textAt(g, x + 104, y, buf, C_TEXT, &fonts::Font0, textdatum_t::top_right);
        bar(g, x, y + 8, 104, 2, a.proj.fx.norm((uint8_t)i), sel ? C_ACCENT : C_ACC2, C_FAINT);
    }
}

static bool actFx(App& a, const Action& act) {
    switch (act.act) {
        case A_CURSOR_PREV: case A_UP:
            a.fxCursor = (uint8_t)((a.fxCursor + FX_COUNT - 1) % FX_COUNT); return true;
        case A_CURSOR_NEXT: case A_DOWN:
            a.fxCursor = (uint8_t)((a.fxCursor + 1) % FX_COUNT); return true;
        case A_LEFT:  a.fxCursor = (uint8_t)((a.fxCursor + FX_COUNT - 8) % FX_COUNT); return true;
        case A_RIGHT: a.fxCursor = (uint8_t)((a.fxCursor + 8) % FX_COUNT); return true;
        case A_VALUE_DOWN: case A_VALUE_UP: {
            int d = (act.act == A_VALUE_UP ? 1 : -1) * (act.arg > 1 ? 12 : 3);
            a.proj.fx.set(a.fxCursor, a.proj.fx.p[a.fxCursor] + d);
            char v[20];
            formatFx(a.proj.fx, a.fxCursor, v, sizeof(v));
            showParam(a, kFxInfo[a.fxCursor].name, v, a.proj.fx.norm(a.fxCursor));
            return true;
        }
        default: return false;
    }
}

// ===========================================================================
// SONG
// ===========================================================================
static void drawSong(App& a, int, int y0, int w, int h) {
    M5Canvas& g = uiCanvas();
    Sequencer& s = a.engine.seq();
    char buf[40];
    snprintf(buf, sizeof(buf), "SONG  %d SLOTS  %s", a.proj.song.length, s.songMode() ? "ON" : "OFF");
    textAt(g, 4, y0 + 1, buf, s.songMode() ? C_ACCENT : C_DIM, &fonts::Font0);
    textAt(g, w - 3, y0 + 1, "FN+\\ TOGGLE SONG", C_FAINT, &fonts::Font0, textdatum_t::top_right);
    g.drawFastHLine(0, y0 + 11, w, C_GRID);

    const int rows = 6;
    int first = a.songCursor >= rows ? a.songCursor - rows + 1 : 0;
    for (int i = 0; i < rows; ++i) {
        int idx = first + i;
        if (idx >= a.proj.song.length) break;
        int y = y0 + 15 + i * 14;
        bool sel = idx == a.songCursor;
        bool cur = s.songMode() && s.playing() && idx == s.songPos();
        if (sel) g.fillRect(2, y - 2, 150, 13, C_PANEL2);
        if (cur) g.fillTriangle(154, y, 154, y + 8, 160, y + 4, C_ACCENT);
        snprintf(buf, sizeof(buf), "%02d", idx + 1);
        textAt(g, 6, y, buf, C_FAINT, &fonts::Font0);
        snprintf(buf, sizeof(buf), "PAT %d", a.proj.song.slot[idx].pattern + 1);
        textAt(g, 34, y, buf, (sel && a.songField == 0) ? C_ACCENT : C_TEXT, &fonts::Font0);
        snprintf(buf, sizeof(buf), "x%d", a.proj.song.slot[idx].repeat);
        textAt(g, 92, y, buf, (sel && a.songField == 1) ? C_ACCENT : C_TEXT, &fonts::Font0);
        textAt(g, 120, y, a.proj.pat[a.proj.song.slot[idx].pattern % kPatternCount].name, C_DIM, &fonts::Font0);
    }

    // pattern bank overview on the right
    textAt(g, 172, y0 + 15, "PATTERNS", C_FAINT, &fonts::Font0);
    for (int i = 0; i < kPatternCount; ++i) {
        int px = 172 + (i % 4) * 17, py = y0 + 27 + (i / 4) * 17;
        bool playing = i == s.currentPattern();
        panel(g, px, py, 15, 15, playing ? C_ACCENT : C_PANEL, playing ? C_ACCENT : C_GRID);
        snprintf(buf, sizeof(buf), "%d", i + 1);
        textAt(g, px + 7, py + 4, buf, playing ? C_BG : C_DIM, &fonts::Font0, textdatum_t::top_center);
    }
    textAt(g, 172, y0 + 64, "SHIFT+1..8", C_FAINT, &fonts::Font0);
    textAt(g, 172, y0 + 74, "PICK PATTERN", C_FAINT, &fonts::Font0);
}

static bool actSong(App& a, const Action& act) {
    Song& sg = a.proj.song;
    switch (act.act) {
        case A_UP:   a.songCursor = (uint8_t)((a.songCursor + sg.length - 1) % sg.length); return true;
        case A_DOWN: a.songCursor = (uint8_t)((a.songCursor + 1) % sg.length); return true;
        case A_LEFT: case A_CURSOR_PREV: a.songField = 0; return true;
        case A_RIGHT: case A_CURSOR_NEXT: a.songField = 1; return true;
        case A_VALUE_DOWN: case A_VALUE_UP: {
            int d = (act.act == A_VALUE_UP) ? 1 : -1;
            SongSlot& sl = sg.slot[a.songCursor % kSongSlots];
            if (a.songField == 0) sl.pattern = (uint8_t)((sl.pattern + kPatternCount + d) % kPatternCount);
            else sl.repeat = (uint8_t)clampi(sl.repeat + d, 1, 16);
            return true;
        }
        case A_CONFIRM:
            sg.length = (uint8_t)clampi(sg.length + 1, 1, kSongSlots);
            showToast(a, "SLOT ADDED");
            return true;
        case A_BACK:
            if (sg.length > 1) --sg.length;
            if (a.songCursor >= sg.length) a.songCursor = (uint8_t)(sg.length - 1);
            return true;
        case A_STEP:
            sg.slot[a.songCursor % kSongSlots].pattern = (uint8_t)(act.arg % kPatternCount);
            return true;
        default: return false;
    }
}

// ===========================================================================
// FILE
// ===========================================================================
static void drawFile(App& a, int, int y0, int w, int h) {
    M5Canvas& g = uiCanvas();
    char buf[64];
    textAt(g, 4, y0 + 1, "PROJECT", C_ACCENT, &fonts::Font0);
    textAt(g, w - 3, y0 + 1, "SD /synthcard", C_FAINT, &fonts::Font0, textdatum_t::top_right);
    g.drawFastHLine(0, y0 + 11, w, C_GRID);

    panel(g, 4, y0 + 15, 150, 18, C_PANEL2, a.fileAction == 0 ? C_ACCENT : C_GRID);
    snprintf(buf, sizeof(buf), "%s", a.proj.name);
    textAt(g, 9, y0 + 20, buf, C_TEXT, &fonts::Font0);
    textAt(g, 158, y0 + 20, "TYPE TO RENAME", C_FAINT, &fonts::Font0);

    static const char* const kActs[] = {"NAME", "SAVE", "LOAD", "DELETE", "NEW"};
    for (int i = 1; i < 5; ++i) {
        int x = 4 + (i - 1) * 58;
        bool sel = i == a.fileAction;
        panel(g, x, y0 + 36, 54, 15, sel ? C_PANEL2 : C_PANEL, sel ? C_ACCENT : C_GRID);
        textAt(g, x + 27, y0 + 40, kActs[i], sel ? C_ACCENT : C_DIM, &fonts::Font0, textdatum_t::top_center);
    }

    textAt(g, 4, y0 + 55, a.fileListValid ? "FILES" : "PRESS FN+; TO SCAN CARD", C_FAINT, &fonts::Font0);
    const int rows = 4;
    int first = a.fileCursor >= rows ? a.fileCursor - rows + 1 : 0;
    for (int i = 0; i < rows && first + i < a.fileCount; ++i) {
        int idx = first + i;
        int y = y0 + 66 + i * 11;
        bool sel = idx == a.fileCursor;
        if (sel) g.fillRect(2, y - 1, 150, 11, C_PANEL2);
        textAt(g, 8, y, a.fileNames[idx], sel ? C_ACCENT : C_TEXT, &fonts::Font0);
    }
    if (a.fileListValid && a.fileCount == 0)
        textAt(g, 8, y0 + 66, "(no projects on card)", C_FAINT, &fonts::Font0);
}

static bool actFile(App& a, const Action& act) {
    char err[48];
    switch (act.act) {
        case A_LEFT:  a.fileAction = (uint8_t)((a.fileAction + 4) % 5); return true;
        case A_RIGHT: a.fileAction = (uint8_t)((a.fileAction + 1) % 5); return true;
        case A_UP:
            if (!a.fileListValid) { refreshFileList(a); return true; }
            if (a.fileCount) a.fileCursor = (uint8_t)((a.fileCursor + a.fileCount - 1) % a.fileCount);
            return true;
        case A_DOWN:
            if (!a.fileListValid) { refreshFileList(a); return true; }
            if (a.fileCount) a.fileCursor = (uint8_t)((a.fileCursor + 1) % a.fileCount);
            return true;
        case A_BACK: {
            int n = (int)strlen(a.proj.name);
            if (n > 0) a.proj.name[n - 1] = 0;
            return true;
        }
        case A_CONFIRM: {
            switch (a.fileAction) {
                case 1: {   // SAVE
                    a.engine.suspendAudio();
                    bool ok = projectSave(a.proj, a.proj.name, &a.undoBuf, err, sizeof(err));
                    a.undoValid = false;
                    a.engine.resumeAudio();
                    if (ok) {
                        strncpy(a.settings.lastProject, a.proj.name, kNameLen - 1);
                        settingsSave(a.settings);
                        a.fileListValid = false;
                        showToast(a, "SAVED");
                    } else showToast(a, err, true);
                    return true;
                }
                case 2: {   // LOAD
                    if (!a.fileListValid) { refreshFileList(a); return true; }
                    if (a.fileCount == 0) { showToast(a, "NO PROJECTS", true); return true; }
                    // Static: a Project is ~9 KB and the Arduino loop task
                    // only has an 8 KB stack.
                    static Project tmp;
                    a.engine.suspendAudio();
                    bool ok = projectLoad(tmp, a.fileNames[a.fileCursor], &a.undoBuf, err, sizeof(err));
                    a.undoValid = false;
                    if (ok) a.proj = tmp;
                    a.engine.resumeAudio();
                    if (ok) {
                        a.engine.post(EV_PANIC);
                        strncpy(a.settings.lastProject, a.proj.name, kNameLen - 1);
                        settingsSave(a.settings);
                        showToast(a, "LOADED");
                    } else showToast(a, err, true);
                    return true;
                }
                case 3: {   // DELETE
                    if (a.fileCount == 0) { showToast(a, "NOTHING TO DELETE", true); return true; }
                    a.engine.suspendAudio();
                    bool ok = projectDelete(a.fileNames[a.fileCursor], err, sizeof(err));
                    a.engine.resumeAudio();
                    if (ok) { a.fileListValid = false; showToast(a, "DELETED"); }
                    else showToast(a, err, true);
                    return true;
                }
                case 4:     // NEW
                    a.engine.post(EV_PANIC);
                    a.proj.reset();
                    applyPreset(a, 0, 0);
                    applyPreset(a, 1, 5);
                    showToast(a, "NEW PROJECT");
                    return true;
                default:
                    return true;
            }
        }
        default: return false;
    }
}

// ===========================================================================
// SYS
// ===========================================================================
enum SysRow : uint8_t {
    SR_VOLUME = 0, SR_BRIGHT, SR_OUTPUT, SR_METRO, SR_SWING, SR_SCALE, SR_ROOT, SR_CHORD,
    SR_ARP_MODE, SR_ARP_RATE, SR_ARP_OCT, SR_ARP_GATE, SR_PAT_LEN, SR_COUNT
};
static const char* const kSysRows[SR_COUNT] = {
    "VOLUME", "BRIGHTNESS", "OUTPUT", "METRONOME", "SWING", "SCALE", "ROOT", "CHORD",
    "ARP MODE", "ARP RATE", "ARP OCT", "ARP GATE", "PATTERN LEN"
};
constexpr int kSysVisible = 7;

// value text + optional 0..1 bar fraction (negative = no bar)
static float sysValue(App& a, int row, char* buf, int len) {
    switch (row) {
        case SR_VOLUME:   snprintf(buf, len, "%d", audioGetVolume());   return audioGetVolume() / 255.0f;
        case SR_BRIGHT:   snprintf(buf, len, "%d", a.settings.brightness); return a.settings.brightness / 255.0f;
        case SR_OUTPUT:   snprintf(buf, len, "%s", kOutModeNames[a.settings.outMode % OUT_MODE_COUNT]); return -1.0f;
        case SR_METRO:    snprintf(buf, len, "%s", kMetroNames[a.settings.metronome % METRO_COUNT]); return -1.0f;
        case SR_SWING:    snprintf(buf, len, "%d%%", a.proj.swing);     return a.proj.swing / 100.0f;
        case SR_SCALE:    snprintf(buf, len, "%s", kScales[a.proj.scale % kScaleCount].name); return -1.0f;
        case SR_ROOT:     snprintf(buf, len, "%s", kNoteNames[a.proj.root % 12]); return -1.0f;
        case SR_CHORD:    snprintf(buf, len, "%s", kChordNames[a.chordMode % CHORD_COUNT]); return -1.0f;
        case SR_ARP_MODE: snprintf(buf, len, "%s", kArpModeNames[a.proj.arpMode % ARP_MODE_COUNT]); return -1.0f;
        case SR_ARP_RATE: snprintf(buf, len, "%s", kArpRateNames[a.proj.arpRate % 6]); return -1.0f;
        case SR_ARP_OCT:  snprintf(buf, len, "%d", a.proj.arpOct);      return a.proj.arpOct / 4.0f;
        case SR_ARP_GATE: snprintf(buf, len, "%d/16", a.proj.arpGate);  return a.proj.arpGate / 15.0f;
        default: {
            int L = patternLength(a.proj, a.engine.seq().currentPattern());
            snprintf(buf, len, "%d", L);
            return L / (float)kMaxSteps;
        }
    }
}

static void drawSys(App& a, int, int y0, int w, int h) {
    M5Canvas& g = uiCanvas();
    char buf[48];
    textAt(g, 4, y0 + 1, "SETTINGS", C_ACCENT, &fonts::Font0);
    textAt(g, w - 3, y0 + 1, "SYNTHCARD v1.1", C_FAINT, &fonts::Font0, textdatum_t::top_right);
    g.drawFastHLine(0, y0 + 11, w, C_GRID);

    int first = 0;
    if (a.sysCursor >= kSysVisible) first = a.sysCursor - kSysVisible + 1;
    if (first > SR_COUNT - kSysVisible) first = SR_COUNT - kSysVisible;

    for (int i = 0; i < kSysVisible; ++i) {
        const int row = first + i;
        if (row >= SR_COUNT) break;
        const int y = y0 + 15 + i * 13;
        const bool sel = row == a.sysCursor;
        if (sel) g.fillRect(2, y - 2, 140, 12, C_PANEL2);
        textAt(g, 6, y, kSysRows[row], sel ? C_ACCENT : C_DIM, &fonts::Font0);
        float frac = sysValue(a, row, buf, sizeof(buf));
        textAt(g, 138, y, buf, C_TEXT, &fonts::Font0, textdatum_t::top_right);
        if (frac >= 0.0f) bar(g, 6, y + 9, 132, 2, frac, sel ? C_ACCENT : C_ACC2, C_FAINT);
    }
    // scroll indicator
    if (SR_COUNT > kSysVisible) {
        const int trackH = kSysVisible * 13;
        const int thumb = trackH * kSysVisible / SR_COUNT;
        const int off = trackH * first / SR_COUNT;
        g.fillRect(144, y0 + 13, 2, trackH, C_FAINT);
        g.fillRect(144, y0 + 13 + off, 2, thumb, C_ACCENT);
    }

    const int ix = 152;
    textAt(g, ix, y0 + 15, "STATUS", C_FAINT, &fonts::Font0);
    snprintf(buf, sizeof(buf), "CPU  %d%%", (int)(a.engine.cpuLoad() * 100));
    textAt(g, ix, y0 + 27, buf, C_TEXT, &fonts::Font0);
    snprintf(buf, sizeof(buf), "VOX  %d/%d", a.engine.activeVoices(), a.engine.maxVoices());
    textAt(g, ix, y0 + 38, buf, C_TEXT, &fonts::Font0);
    snprintf(buf, sizeof(buf), "RAM  %dk", (int)(ESP.getFreeHeap() / 1024));
    textAt(g, ix, y0 + 49, buf, C_TEXT, &fonts::Font0);
    snprintf(buf, sizeof(buf), "FPS  %d", (int)a.fps);
    textAt(g, ix, y0 + 60, buf, C_TEXT, &fonts::Font0);
    snprintf(buf, sizeof(buf), "BAT  %d%%", M5.Power.getBatteryLevel());
    textAt(g, ix, y0 + 71, buf, C_TEXT, &fonts::Font0);
}

static bool actSys(App& a, const Action& act) {
    switch (act.act) {
        case A_UP: case A_CURSOR_PREV:
            a.sysCursor = (uint8_t)((a.sysCursor + SR_COUNT - 1) % SR_COUNT); return true;
        case A_DOWN: case A_CURSOR_NEXT:
            a.sysCursor = (uint8_t)((a.sysCursor + 1) % SR_COUNT); return true;
        case A_VALUE_DOWN: case A_VALUE_UP: {
            const int d = (act.act == A_VALUE_UP) ? 1 : -1;
            const int big = act.arg > 1 ? 10 : 1;
            char v[24];
            switch (a.sysCursor) {
                case SR_VOLUME: {
                    int nv = clampi(audioGetVolume() + d * big * 8, 0, 255);
                    audioSetVolume((uint8_t)nv);
                    a.settings.volume = (uint8_t)nv;
                    settingsSave(a.settings);
                    break;
                }
                case SR_BRIGHT: {
                    int nv = clampi(a.settings.brightness + d * big * 8, 10, 255);
                    a.settings.brightness = (uint8_t)nv;
                    M5.Display.setBrightness((uint8_t)nv);
                    settingsSave(a.settings);
                    break;
                }
                case SR_OUTPUT:
                    // SPEAKER high-passes the master so a kick's inaudible
                    // bottom octave stops eating the limiter's headroom;
                    // LINE leaves the signal alone for headphones.
                    a.settings.outMode = (uint8_t)((a.settings.outMode + OUT_MODE_COUNT + d) % OUT_MODE_COUNT);
                    a.engine.setOutMode(a.settings.outMode);
                    settingsSave(a.settings);
                    break;
                case SR_METRO:
                    a.settings.metronome = (uint8_t)((a.settings.metronome + METRO_COUNT + d) % METRO_COUNT);
                    a.engine.setMetronome(a.settings.metronome);
                    settingsSave(a.settings);
                    break;
                case SR_SWING:
                    a.proj.swing = (uint8_t)clampi(a.proj.swing + d * big * 2, 0, 100); break;
                case SR_SCALE:
                    a.proj.scale = (uint8_t)((a.proj.scale + kScaleCount + d) % kScaleCount); break;
                case SR_ROOT:
                    a.proj.root = (uint8_t)((a.proj.root + 12 + d) % 12); break;
                case SR_CHORD:
                    a.chordMode = (uint8_t)((a.chordMode + CHORD_COUNT + d) % CHORD_COUNT);
                    a.engine.setRecordChord(a.chordMode); break;
                case SR_ARP_MODE:
                    a.proj.arpMode = (uint8_t)((a.proj.arpMode + ARP_MODE_COUNT + d) % ARP_MODE_COUNT); break;
                case SR_ARP_RATE:
                    a.proj.arpRate = (uint8_t)((a.proj.arpRate + 6 + d) % 6); break;
                case SR_ARP_OCT:
                    a.proj.arpOct = (uint8_t)clampi(a.proj.arpOct + d, 1, 4); break;
                case SR_ARP_GATE:
                    a.proj.arpGate = (uint8_t)clampi(a.proj.arpGate + d, 1, 15); break;
                default: {
                    Pattern& p = a.proj.pat[a.engine.seq().currentPattern()];
                    p.length = (uint8_t)clampi(p.length + d * big, 1, kMaxSteps);
                    break;
                }
            }
            float frac = sysValue(a, a.sysCursor, v, sizeof(v));
            showParam(a, kSysRows[a.sysCursor], v, frac);
            return true;
        }
        default: return false;
    }
}

// ===========================================================================
// dispatch
// ===========================================================================
// Every screen cursor is normalised here, once, before any drawing or editing.
// Individual screens then index arrays freely; nothing downstream has to trust
// that a pattern did not shrink, a project did not load, or a cursor was left
// over from another screen.
static void clampCursors(App& a) {
    a.drumLane    %= DL_COUNT;
    a.drumParam   %= kDrumCursorCount;
    a.seqTrack    %= kMelTracks;
    a.seqField    %= kSeqFieldCount;
    a.seqStep     %= 16;
    const uint8_t eng = a.proj.patch[a.engine.liveTrack() % kMelTracks].engine();
    a.soundPage   %= synthPageCount(eng);
    const uint8_t pn = synthPages(eng)[a.soundPage].n;
    a.soundCursor = pn ? (uint8_t)(a.soundCursor % pn) : 0;
    a.fxCursor    %= FX_COUNT;
    a.sysCursor   %= SR_COUNT;
    a.songField   %= 2;
    a.fileAction  %= 5;
    a.chordMode   %= CHORD_COUNT;
    a.menuCursor  %= (uint8_t)uiMenuCount();
    a.helpPage    %= (uint8_t)uiHelpPageCount();
    a.bootSel     %= BOOT_COUNT;
    a.proj.scale  %= kScaleCount;
    a.proj.root   %= 12;
    a.proj.arpMode %= ARP_MODE_COUNT;
    a.proj.arpRate %= 6;
    if (a.proj.song.length < 1) a.proj.song.length = 1;
    a.songCursor  %= a.proj.song.length;
    a.fileCursor  = (a.fileCount > 0) ? (uint8_t)(a.fileCursor % a.fileCount) : 0;
    const int pages = (patternLength(a.proj, a.engine.seq().currentPattern()) + 15) / 16;
    a.drumPage    %= (uint8_t)pages;
    a.seqPage     %= (uint8_t)pages;
}

void screenDraw(App& a, int x, int y, int w, int h) {
    clampCursors(a);
    switch (a.mode) {
        case M_PLAY:  drawPlay(a, x, y, w, h); break;
        case M_DRUM:  drawDrum(a, x, y, w, h); break;
        case M_SEQ:   drawSeq(a, x, y, w, h); break;
        case M_SOUND: drawSound(a, x, y, w, h); break;
        case M_FX:    drawFx(a, x, y, w, h); break;
        case M_SONG:  drawSong(a, x, y, w, h); break;
        case M_FILE:  drawFile(a, x, y, w, h); break;
        default:      drawSys(a, x, y, w, h); break;
    }
}

bool screenAction(App& a, const Action& act) {
    clampCursors(a);
    // A step action always names one of the sixteen step keys. Normalising it
    // here means no screen has to defend its own array indexing.
    Action e = act;
    if (e.act == A_STEP) e.arg = (int8_t)clampi(e.arg, 0, 15);
    switch (a.mode) {
        case M_PLAY:  return actPlay(a, e);
        case M_DRUM:  return actDrum(a, e);
        case M_SEQ:   return actSeq(a, e);
        case M_SOUND: return actSound(a, e);
        case M_FX:    return actFx(a, e);
        case M_SONG:  return actSong(a, e);
        case M_FILE:  return actFile(a, e);
        default:      return actSys(a, e);
    }
}

const char* screenHint(const App& a) {
    switch (a.mode) {
        case M_PLAY:  return "Z..? PLAY   F/K SOUND   ` HELP";
        case M_DRUM:  return "1-8 Q-I STEPS   Z.. PADS   ` HELP";
        case M_SEQ:   return "Z..? NOTE   OP FIELD   [] VALUE   ` HELP";
        case M_SOUND: return "OP PARAM   [] VALUE   F/K PRESET   ` HELP";
        case M_FX:    return "OP PARAM   [] VALUE   ` HELP";
        case M_SONG:  return "FN+;. SLOT   [] VALUE   ` HELP";
        case M_FILE:  return "FN+,/ ACTION   TYPE TO RENAME   ` HELP";
        default:      return "FN+;. ROW   [] VALUE   ` HELP";
    }
}

}  // namespace synth
