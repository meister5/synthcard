#include "ui.h"
#include "../app.h"
#include "../music/music.h"
#include <M5Cardputer.h>

namespace synth {

const char* const kModeNames[M_COUNT] =
    {"PLAY", "DRUM", "SEQ", "SOUND", "FX", "SONG", "FILE", "SYS"};
const char* const kChordNames[CH_COUNT] = {"OFF", "POWER", "TRIAD", "7TH"};

// Constructed inside uiBegin() rather than at static-init time: the sprite
// reads settings off M5.Display, which is not brought up until M5.begin().
static M5Canvas* s_canvasPtr = nullptr;
M5Canvas& uiCanvas() { return *s_canvasPtr; }

namespace ui {

void bar(M5Canvas& g, int x, int y, int w, int h, float frac, uint16_t fg, uint16_t bg) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    g.fillRect(x, y, w, h, bg);
    int fw = (int)(w * frac + 0.5f);
    if (fw > 0) g.fillRect(x, y, fw, h, fg);
}

void barBipolar(M5Canvas& g, int x, int y, int w, int h, float v, uint16_t fg, uint16_t bg) {
    g.fillRect(x, y, w, h, bg);
    int mid = x + w / 2;
    int len = (int)((w / 2) * (v < 0 ? -v : v));
    if (len < 1 && (v > 0.02f || v < -0.02f)) len = 1;
    if (v >= 0) g.fillRect(mid, y, len, h, fg);
    else        g.fillRect(mid - len, y, len, h, fg);
    g.drawFastVLine(mid, y - 1, h + 2, C_DIM);
}

void panel(M5Canvas& g, int x, int y, int w, int h, uint16_t fill, uint16_t border) {
    g.fillRoundRect(x, y, w, h, 3, fill);
    if (border != fill) g.drawRoundRect(x, y, w, h, 3, border);
}

void textAt(M5Canvas& g, int x, int y, const char* s, uint16_t col, const void* font, uint8_t datum) {
    g.setFont(static_cast<const lgfx::IFont*>(font));
    g.setTextColor(col);
    g.setTextDatum(datum);
    g.drawString(s, x, y);
    g.setTextDatum(textdatum_t::top_left);
}

}  // namespace ui

using namespace ui;

static bool s_canvasOk = false;

void uiBegin() {
    static M5Canvas canvas(&M5.Display);
    s_canvasPtr = &canvas;
    M5.Display.setRotation(1);
    M5.Display.fillScreen(C_BG);
    canvas.setColorDepth(16);
    s_canvasOk = canvas.createSprite(W, H) != nullptr;
    canvas.setTextWrap(false);
}

// The 65 KB canvas should always fit, but a failed allocation must degrade to
// a readable screen rather than a null-pointer write.
static bool canvasReady() {
    if (s_canvasOk) return true;
    M5.Display.fillScreen(C_BG);
    M5.Display.setTextColor(C_REC);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setCursor(8, 50);
    M5.Display.print("DISPLAY MEMORY ERROR");
    return false;
}

// ------------------------------------------------------------------ chrome --
static void drawTopBar(App& a) {
    M5Canvas& g = uiCanvas();
    g.fillRect(0, 0, W, TOP_H, C_PANEL);
    g.drawFastHLine(0, TOP_H, W, C_GRID);

    // mode name
    textAt(g, 3, 3, kModeNames[a.mode], C_ACCENT, &fonts::Font0);

    // transport
    Sequencer& s = a.engine.seq();
    const bool playing = s.playing();
    const bool rec = s.recording();
    int x = 42;
    if (playing) g.fillTriangle(x, 3, x, 10, x + 6, 6, rec ? C_REC : C_OK);
    else         g.fillRect(x, 3, 7, 7, C_DIM);
    if (rec) g.fillCircle(x + 14, 6, 3, C_REC);

    char buf[40];
    snprintf(buf, sizeof(buf), "%3d BPM", a.proj.bpm);
    textAt(g, 66, 3, buf, C_TEXT, &fonts::Font0);

    snprintf(buf, sizeof(buf), "%s%d.%02d",
             s.songMode() ? "S" : "P", s.currentPattern() + 1, (s.step() % 16) + 1);
    textAt(g, 118, 3, buf, s.songMode() ? C_ACC2 : C_DIM, &fonts::Font0);

    // step blinker: 16 ticks across the right edge
    int len = patternLength(a.proj, s.currentPattern());
    int barIdx = (len > 16) ? (s.step() / 16) : 0;
    for (int i = 0; i < 16; ++i) {
        int gx = 166 + i * 4;
        bool cur = playing && (s.step() % 16) == i;
        g.fillRect(gx, 5, 3, 3, cur ? C_ACCENT : C_FAINT);
    }
    if (len > 16) {
        snprintf(buf, sizeof(buf), "%d", barIdx + 1);
        textAt(g, 232, 3, buf, C_DIM, &fonts::Font0);
    }
    // voice activity dot
    if (a.engine.activeVoices() > 0) g.fillCircle(236, 6, 2, C_ACC2);
}

static void drawHintBar(App& a) {
    M5Canvas& g = uiCanvas();
    const int y = H - HINT_H;
    g.fillRect(0, y, W, HINT_H, C_PANEL);
    g.drawFastHLine(0, y, W, C_GRID);
    textAt(g, 3, y + 4, screenHint(a), C_DIM, &fonts::Font0);

    char right[24];
    if (a.mode == M_DRUM) snprintf(right, sizeof(right), "KIT %s", a.proj.kit.name);
    else snprintf(right, sizeof(right), "OCT %d %s", a.proj.octave,
                  a.proj.arpOn ? "ARP" : (a.proj.scale ? kScales[a.proj.scale].name : ""));
    textAt(g, W - 3, y + 4, right, C_FAINT, &fonts::Font0, textdatum_t::top_right);
}

// ---------------------------------------------------------------- overlays --
static void drawParamOverlay(App& a, uint32_t now) {
    if (now >= a.ovUntil) return;
    M5Canvas& g = uiCanvas();
    const int w = 150, h = 56, x = (W - w) / 2, y = 32;
    panel(g, x, y, w, h, C_PANEL2, C_ACCENT);
    textAt(g, x + w / 2, y + 5, a.ovName, C_DIM, &fonts::Font0, textdatum_t::top_center);
    textAt(g, x + w / 2, y + 15, a.ovValue, C_TEXT, &fonts::Font4, textdatum_t::top_center);
    if (a.ovFrac >= 0.0f) bar(g, x + 10, y + h - 12, w - 20, 6, a.ovFrac, C_ACCENT, C_FAINT);
}

static void drawToast(App& a, uint32_t now) {
    if (now >= a.toastUntil) return;
    M5Canvas& g = uiCanvas();
    const int h = 22, y = H - HINT_H - h - 3, x = 8, w = W - 16;
    panel(g, x, y, w, h, C_PANEL2, a.toastError ? C_REC : C_OK);
    textAt(g, x + w / 2, y + 7, a.toast, a.toastError ? C_REC : C_TEXT,
           &fonts::Font0, textdatum_t::top_center);
}

struct MenuItem { const char* label; };
static const MenuItem kMenu[] = {
    {"QUICK JAM"}, {"NEW PROJECT"}, {"SAVE PROJECT"}, {"LOAD PROJECT"},
    {"RANDOM BEAT"}, {"RANDOM BASS"}, {"RANDOM LEAD"}, {"RANDOM SOUND"},
    {"CHORD MODE"}, {"SCALE"}, {"ROOT"}, {"SWING"}, {"HELP"}, {"CLOSE"},
};
constexpr int kMenuCount = sizeof(kMenu) / sizeof(kMenu[0]);
int uiMenuCount() { return kMenuCount; }
const char* uiMenuLabel(int i) { return kMenu[i % kMenuCount].label; }

static void drawMenu(App& a) {
    M5Canvas& g = uiCanvas();
    const int w = 150, x = (W - w) / 2, y = 8, h = H - 16;
    panel(g, x, y, w, h, C_PANEL, C_ACCENT);
    textAt(g, x + w / 2, y + 4, "MENU", C_ACCENT, &fonts::Font0, textdatum_t::top_center);
    const int rows = 8;
    int first = a.menuCursor >= rows ? a.menuCursor - rows + 1 : 0;
    for (int i = 0; i < rows && first + i < kMenuCount; ++i) {
        int idx = first + i;
        int ry = y + 16 + i * 12;
        bool sel = idx == a.menuCursor;
        if (sel) g.fillRect(x + 3, ry - 1, w - 6, 11, C_PANEL2);
        char line[40];
        switch (idx) {
            case 8:  snprintf(line, sizeof(line), "CHORD  %s", kChordNames[a.chordMode]); break;
            case 9:  snprintf(line, sizeof(line), "SCALE  %s", kScales[a.proj.scale].name); break;
            case 10: snprintf(line, sizeof(line), "ROOT   %s", kNoteNames[a.proj.root % 12]); break;
            case 11: snprintf(line, sizeof(line), "SWING  %d%%", a.proj.swing); break;
            default: snprintf(line, sizeof(line), "%s", kMenu[idx].label); break;
        }
        textAt(g, x + 8, ry, line, sel ? C_ACCENT : C_TEXT, &fonts::Font0);
    }
    textAt(g, x + w / 2, y + h - 11, "FN+;/. MOVE  ENTER OK", C_FAINT, &fonts::Font0, textdatum_t::top_center);
}

static const char* const kHelpPages[][9] = {
    {"TRANSPORT & MODES",
     "SPACE    play / stop",
     "\\        record arm",
     "TAB      next mode",
     "FN+1..8  jump to mode",
     "`        menu",
     "FN+SPACE tap tempo",
     "9 / 0    BPM  (FN = x10)",
     "- / =    octave down/up"},
    {"EDIT & PERFORM",
     "Z..? S..;  play notes",
     "1..8 Q..I  steps 1..16",
     "O / P      select param",
     "[ / ]      value -/+",
     "FN+;./,/   arrows",
     "SHIFT+1..8 pick pattern",
     "A          arp on/off",
     "F / K      preset -/+"},
    {"GENERATE & FILES",
     "FN+Q  random drums",
     "FN+W  random bass",
     "FN+E  random melody",
     "FN+R  random sound",
     "FN+T  euclid on lane",
     "FN+Y/U copy / paste pat",
     "FN+I  clear pattern",
     "FN+O / FN+P  save / load"},
};
constexpr int kHelpPageCount = sizeof(kHelpPages) / sizeof(kHelpPages[0]);
int uiHelpPageCount() { return kHelpPageCount; }

static void drawHelp(App& a) {
    M5Canvas& g = uiCanvas();
    g.fillRect(0, 0, W, H, C_BG);
    const int p = a.helpPage % kHelpPageCount;
    textAt(g, 4, 3, kHelpPages[p][0], C_ACCENT, &fonts::Font0);
    g.drawFastHLine(0, 13, W, C_GRID);
    for (int i = 1; i < 9; ++i)
        textAt(g, 6, 17 + (i - 1) * 13, kHelpPages[p][i], C_TEXT, &fonts::Font0);
    char f[32];
    snprintf(f, sizeof(f), "PAGE %d/%d   FN+,/ TURN   ESC=`", p + 1, kHelpPageCount);
    textAt(g, W / 2, H - 11, f, C_FAINT, &fonts::Font0, textdatum_t::top_center);
}

// -------------------------------------------------------------------- boot --
void uiDrawBoot(App& a) {
    if (!canvasReady()) return;
    M5Canvas& g = uiCanvas();
    g.fillSprite(C_BG);
    // wordmark
    textAt(g, W / 2, 14, "SYNTHCARD", C_ACCENT, &fonts::Font4, textdatum_t::top_center);
    textAt(g, W / 2, 40, "POCKET GROOVEBOX  /  CARDPUTER ADV", C_FAINT, &fonts::Font0, textdatum_t::top_center);
    g.drawFastHLine(30, 52, W - 60, C_GRID);

    static const char* const kOpts[BOOT_COUNT] = {"QUICK JAM", "NEW PROJECT", "LOAD PROJECT"};
    for (int i = 0; i < BOOT_COUNT; ++i) {
        int y = 58 + i * 18;
        bool sel = i == a.bootSel;
        panel(g, 46, y, W - 92, 16, sel ? C_PANEL2 : C_PANEL, sel ? C_ACCENT : C_PANEL);
        textAt(g, W / 2, y + 4, kOpts[i], sel ? C_ACCENT : C_DIM, &fonts::Font0, textdatum_t::top_center);
    }
    textAt(g, W / 2, H - 12, "FN+;/.  SELECT      ENTER  START",
           C_FAINT, &fonts::Font0, textdatum_t::top_center);
    g.pushSprite(0, 0);
}

// -------------------------------------------------------------------- draw --
void uiDraw(App& a) {
    if (!canvasReady()) return;
    M5Canvas& g = uiCanvas();
    const uint32_t now = millis();
    g.fillSprite(C_BG);

    if (a.helpOpen) { drawHelp(a); g.pushSprite(0, 0); return; }

    drawTopBar(a);
    screenDraw(a, 0, BODY_Y, W, BODY_H);
    drawHintBar(a);
    if (a.menuOpen) drawMenu(a);
    drawParamOverlay(a, now);
    drawToast(a, now);
    g.pushSprite(0, 0);
}

}  // namespace synth
