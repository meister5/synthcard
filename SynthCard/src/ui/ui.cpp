#include "ui.h"
#include "../app.h"
#include "../music/music.h"
#include <M5Cardputer.h>

namespace synth {

const char* const kModeNames[M_COUNT] =
    {"PLAY", "DRUM", "SEQ", "SOUND", "FX", "SONG", "SETUP"};

// Constructed inside uiBegin() rather than at static-init time: the sprite
// reads settings off M5.Display, which is not brought up until M5.begin().
static M5Canvas* s_canvasPtr = nullptr;
M5Canvas& uiCanvas() { return *s_canvasPtr; }

namespace ui {

void bar(M5Canvas& g, int x, int y, int w, int h, float frac, Color fg, Color bg) {
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    g.fillRect(x, y, w, h, bg);
    int fw = (int)(w * frac + 0.5f);
    if (fw > 0) g.fillRect(x, y, fw, h, fg);
}

void barBipolar(M5Canvas& g, int x, int y, int w, int h, float v, Color fg, Color bg) {
    g.fillRect(x, y, w, h, bg);
    int mid = x + w / 2;
    int len = (int)((w / 2) * (v < 0 ? -v : v));
    if (len < 1 && (v > 0.02f || v < -0.02f)) len = 1;
    if (v >= 0) g.fillRect(mid, y, len, h, fg);
    else        g.fillRect(mid - len, y, len, h, fg);
    g.drawFastVLine(mid, y - 1, h + 2, C_DIM);
}

void panel(M5Canvas& g, int x, int y, int w, int h, Color fill, Color border) {
    g.fillRoundRect(x, y, w, h, 3, fill);
    if (border != fill) g.drawRoundRect(x, y, w, h, 3, border);
}

void textAt(M5Canvas& g, int x, int y, const char* s, Color col, const void* font, uint8_t datum) {
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
    M5.Display.fillScreen(displayRgb(0));
#if SC_CANVAS_4BIT
    // 4 bits per pixel with a 16-entry palette: 16.2 KB instead of 64.8 KB.
    // Every drawing call then passes an index, which is what the C_* constants
    // are in this mode.
    canvas.setColorDepth(4);
    s_canvasOk = canvas.createSprite(W, H) != nullptr;
    if (s_canvasOk)
        for (int i = 0; i < kPaletteSize; ++i)
            canvas.setPaletteColor(i, kPalette[i].r, kPalette[i].g, kPalette[i].b);
#else
    canvas.setColorDepth(16);
    s_canvasOk = canvas.createSprite(W, H) != nullptr;
#endif
    canvas.setTextWrap(false);
}

// The canvas should always fit, but a failed allocation must degrade to a
// readable screen rather than a null-pointer write.
static bool canvasReady() {
    if (s_canvasOk) return true;
    M5.Display.fillScreen(displayRgb(0));
    M5.Display.setTextColor(displayRgb(8));
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setCursor(8, 50);
    M5.Display.print("DISPLAY MEMORY ERROR");
    return false;
}

// ------------------------------------------------------------------ chrome --
static void drawTopBar(App& a) {
    M5Canvas& g = uiCanvas();
    const bool recOn = a.engine.seq().recording();
    g.fillRect(0, 0, W, TOP_H, recOn ? C_RECBAR : C_PANEL);
    g.drawFastHLine(0, TOP_H, W, recOn ? C_REC : C_GRID);

    // mode name
    textAt(g, 3, 3, kModeNames[a.mode], C_ACCENT, &fonts::Font0);

    // transport
    Sequencer& s = a.engine.seq();
    const bool playing = s.playing();
    const bool rec = recOn;
    int x = 42;
    if (playing) g.fillTriangle(x, 3, x, 10, x + 6, 6, rec ? C_REC : C_OK);
    else         g.fillRect(x, 3, 7, 7, C_DIM);
    // Blink the record light so an armed transport reads at a glance.
    if (rec && ((millis() >> 8) & 1)) g.fillCircle(x + 14, 6, 3, C_REC);
    else if (rec) g.drawCircle(x + 14, 6, 3, C_REC);

    char buf[40];
    snprintf(buf, sizeof(buf), "%3d BPM", a.proj.bpm);
    textAt(g, 66, 3, buf, C_TEXT, &fonts::Font0);

    if (s.queuedPattern() != 0xFF)
        snprintf(buf, sizeof(buf), "PTN %d>%d", s.currentPattern() + 1, s.queuedPattern() + 1);
    else
        snprintf(buf, sizeof(buf), "%s %d", s.songMode() ? "SONG" : "PTN", s.currentPattern() + 1);
    textAt(g, 114, 3, buf, s.songMode() ? C_ACC2
                          : (s.queuedPattern() != 0xFF ? C_ACC2 : C_DIM), &fonts::Font0);

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

    // One fixed status strip. These used to be scattered across screens, so
    // whether the arp was on depended on which screen you happened to be on.
    char right[40];
    if (a.mode == M_DRUM) {
        snprintf(right, sizeof(right), "KIT %s", a.proj.kit.name);
    } else {
        char extra[24] = {0};
        int k = 0;
        if (a.proj.arpOn) k += snprintf(extra + k, sizeof(extra) - k, " ARP");
        if (a.chordMode)  k += snprintf(extra + k, sizeof(extra) - k, " %s",
                                        kChordNames[a.chordMode % CHORD_COUNT]);
        snprintf(right, sizeof(right), "%s OCT%d%s",
                 a.engine.liveTrack() == 0 ? "LEAD" : "BASS", a.proj.octave, extra);
    }
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
            case 9:  snprintf(line, sizeof(line), "SCALE  %s", kScales[a.proj.scale % kScaleCount].name); break;
            case 10: snprintf(line, sizeof(line), "ROOT   %s", kNoteNames[a.proj.root % 12]); break;
            case 11: snprintf(line, sizeof(line), "SWING  %d%%", a.proj.swing); break;
            default: snprintf(line, sizeof(line), "%s", kMenu[idx].label); break;
        }
        textAt(g, x + 8, ry, line, sel ? C_ACCENT : C_TEXT, &fonts::Font0);
    }
    textAt(g, x + w / 2, y + h - 11, "FN+;/. MOVE  ENTER OK", C_FAINT, &fonts::Font0, textdatum_t::top_center);
}

// Per-tab manual. The hint bar can only carry three shortcuts, so ` opens the
// full page for whichever tab you are on; FN+, / FN+/ flips to the global keys.
struct HelpPage { const char* title; const char* line[10]; };

static const HelpPage kModeHelp[M_COUNT] = {
    {"PLAY", {
        "Z..? and S..;   play notes",
        "- / =           octave down / up",
        "[ / ]           filter cutoff",
        "O / P           resonance",
        "F / K           previous / next sound",
        "1..8            drum pads",
        "FN+; / FN+.     LEAD <-> BASS track",
        "A               arpeggiator on / off",
        "hold BKSP       erase while playing"}},
    {"DRUM", {
        "1..8  Q..I      toggle steps 1-16",
        "Z X C V B N M , .   drum pads",
        "FN+; / FN+.     choose lane (12 of them)",
        "O / P           PUNCH TONE SPACE, then",
        "                the 8 detail knobs",
        "[ / ]           change it (auditions)",
        "F / K           previous / next kit",
        "hats and ride cut each other off",
        "'  mute lane    FN+BKSP clear lane"}},
    {"SEQ", {
        "1..8  Q..I      pick step 1-16",
        "Z..? keys    set note, cursor moves",
        "O / P    field (CHRD = chord on a step)",
        "GATE past 16/16 = 2STP..17STP holds",
        "FN+, / FN+/     page (steps 17+)",
        "FN+; / FN+.     LEAD <-> BASS track",
        "SHIFT+1..8      go to pattern 1-8",
        "SHIFT+[/] pattern  SHIFT+O/P length",
        "ENTER mute step   BKSP clear step"}},
    {"SOUND", {
        "O / P           pick a parameter",
        "[ / ]           change it (FN = x10)",
        "FN+, / FN+/     turn the page",
        "1..7            jump straight to a page",
        "FN+; / FN+.     LEAD <-> BASS track",
        "F / K           previous / next preset",
        "FN+R            randomise, musically",
        "O / P wraps past the end of a page,",
        "so you never need the page keys."}},
    {"FX", {
        "O / P           pick a parameter",
        "[ / ]           change it (FN = x10)",
        "FN+; / FN+.     move up / down",
        "FN+, / FN+/     jump column",
        "",
        "MIX is how much you hear.",
        "Each track feeds the delay and reverb",
        "through its own send, set on the",
        "SOUND page (VOICE) or the kit."}},
    {"SONG", {
        "FN+; / FN+.     choose a slot",
        "O / P           pattern / repeat field",
        "[ / ]           change the value",
        "1..8            set this slot's pattern",
        "ENTER           add a slot",
        "BKSP            remove the last slot",
        "FN+\\            song mode on / off",
        "",
        "Song mode plays the list top to bottom."}},
    {"SETUP", {
        "FN+7            switch FILES / SYSTEM",
        "FILES: FN+,//   NAME SAVE LOAD DEL NEW",
        "  on NAME type A-Z 0-9 - _ ; ENTER",
        "  saves to SD:/synthcard/NAME.SCP.",
        "  Nothing is written until you save.",
        "SYSTEM: FN+;/. row, [ / ] change.",
        "  OUTPUT picks SPEAKER or LINE -",
        "  SPEAKER trims what the speaker",
        "  cannot reproduce. Right col is live."}},
};

static const HelpPage kGlobalHelp = {"GLOBAL KEYS", {
    "SPACE  play/stop    \\  record arm",
    "hold BKSP    erase under playhead",
    "CTRL+Z       undo  (again = redo)",
    "TAB next tab       FN+1..7  jump",
    "SHIFT+1..8     go to pattern 1-8",
    "SHIFT+[ / ]    prev / next pattern",
    "SHIFT+O / P    pattern length",
    "9/0 BPM  -/= octave  FN+SP tap",
    "FN+`  menu         `  this help",
    "hold FN or SHIFT to see every key"}};

int uiHelpPageCount() { return 2; }

static void drawHelp(App& a) {
    M5Canvas& g = uiCanvas();
    g.fillRect(0, 0, W, H, C_BG);
    const bool global = (a.helpPage % 2) != 0;
    const HelpPage& hp = global ? kGlobalHelp : kModeHelp[a.mode % M_COUNT];

    g.fillRect(0, 0, W, 13, C_PANEL);
    textAt(g, 4, 3, hp.title, C_ACCENT, &fonts::Font0);
    textAt(g, W - 4, 3, global ? "2/2" : "1/2", C_FAINT, &fonts::Font0, textdatum_t::top_right);
    g.drawFastHLine(0, 13, W, C_GRID);

    // The array is wider than the page fits; draw what there is room for.
    constexpr int kHelpLines = (int)(sizeof(hp.line) / sizeof(hp.line[0]));
    for (int i = 0; i < kHelpLines && 17 + i * 11 < H - 4; ++i)
        if (hp.line[i] && hp.line[i][0])
            textAt(g, 6, 17 + i * 11, hp.line[i], C_TEXT, &fonts::Font0);

    g.fillRect(0, H - 12, W, 12, C_PANEL);
    textAt(g, W / 2, H - 10, "FN+, / FN+/  TURN PAGE     ANY KEY  CLOSE",
           C_FAINT, &fonts::Font0, textdatum_t::top_center);
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

    if (a.tourOpen) { uiDrawTour(a); g.pushSprite(0, 0); return; }
    if (a.helpOpen) { drawHelp(a); g.pushSprite(0, 0); return; }

    drawTopBar(a);
    // Holding a modifier replaces the body with what that modifier does right
    // now. The top bar stays, so the transport never disappears mid-take.
    if (legendActive(a, now)) uiDrawLegend(a);
    else                      screenDraw(a, 0, BODY_Y, W, BODY_H);
    drawHintBar(a);
    if (a.menuOpen) drawMenu(a);
    drawParamOverlay(a, now);
    drawToast(a, now);
    g.pushSprite(0, 0);
}

}  // namespace synth
