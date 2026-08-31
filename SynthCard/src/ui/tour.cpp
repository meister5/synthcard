// SynthCard - the first-run walkthrough.
//
// Five steps, each waiting for the real key rather than for "next". By the end
// a beat is playing and the player has used the transport, the pads, the mode
// key, the recorder and the legend - which is the difference between a machine
// that is confusing and one you have already made something on.
//
// Runs once (flagged in NVS), can be replayed from SETUP, and BKSP skips out
// at any point.
#include "../app.h"
#include "ui.h"

namespace synth {

using namespace ui;

namespace {

struct TourStep {
    const char* title;
    const char* body;
    const char* prompt;
    uint8_t     keyLo, keyHi;      // inclusive range of keys that satisfy it
    bool        onModifier;        // satisfied by holding a modifier instead
};

// Key ids, spelled out so the table reads as the keyboard.
constexpr uint8_t kKeySpace = KID(3, 13);
constexpr uint8_t kKeyTab   = KID(1, 0);
constexpr uint8_t kKeyRec   = KID(1, 13);
constexpr uint8_t kPad1     = KID(0, 1);
constexpr uint8_t kPad8     = KID(0, 8);

const TourStep kSteps[] = {
    {"WELCOME",
     "There is a beat loaded and ready.",
     "Press SPACE to start it",
     kKeySpace, kKeySpace, false},

    {"PADS",
     "The number row plays the drums.",
     "Hit any key from 1 to 8",
     kPad1, kPad8, false},

    {"SCREENS",
     "Each screen does one job.",
     "Press TAB to see the next one",
     kKeyTab, kKeyTab, false},

    {"RECORDING",
     "Arm the recorder and play along;",
     "press  \\  to arm it",
     kKeyRec, kKeyRec, false},

    {"EVERY KEY",
     "Forgotten what a key does? Hold a",
     "modifier. Hold FN now",
     0, 0, true},
};
constexpr int kStepCount = (int)(sizeof(kSteps) / sizeof(kSteps[0]));

}  // namespace

int tourStepCount() { return kStepCount; }

void tourStart(App& a) {
    a.tourStep = 0;
    a.tourOpen = true;
    // Something to hear from the first keypress. A tour that starts on an
    // empty machine teaches the keys but not what they are for.
    snapshotUndo(a);
    Pattern& p = a.proj.pat[0];
    p.clear();
    p.length = 16;
    for (int s = 0; s < 16; s += 4)  p.drum[DL_KICK][s]  = 110;
    for (int s = 4; s < 16; s += 8)  p.drum[DL_SNARE][s] = 105;
    for (int s = 2; s < 16; s += 2)  p.drum[DL_CHH][s]   = 70;
    a.engine.seq().selectPattern(0, true);
}

void tourFinish(App& a) {
    a.tourOpen = false;
    a.settings.tourDone = 1;
    settingsSave(a.settings);
    showToast(a, "READY - press ` for the manual");
}

// Returns true when the key was consumed by the tour.
bool tourKey(App& a, uint8_t id, bool pressed) {
    if (!a.tourOpen || !pressed) return false;

    // BKSP always leaves, at any step.
    if (id == KID(0, 13)) { tourFinish(a); return true; }

    const TourStep& st = kSteps[a.tourStep % kStepCount];
    if (st.onModifier) {
        if (!isModifierKey(id)) return false;
    } else if (id < st.keyLo || id > st.keyHi) {
        return false;
    }

    // The key is let through to the app as well, so the step's action really
    // happens - SPACE starts the transport, a pad makes a sound. Being told
    // "press SPACE" and having nothing happen would teach the wrong thing.
    if (++a.tourStep >= kStepCount) tourFinish(a);
    return false;
}

void uiDrawTour(App& a) {
    M5Canvas& g = uiCanvas();
    const int w = 210, h = 82, x = (W - w) / 2, y = 26;
    const TourStep& st = kSteps[a.tourStep % kStepCount];

    g.fillRect(0, 0, W, H, C_BG);
    panel(g, x, y, w, h, C_PANEL, C_ACCENT);

    char head[32];
    snprintf(head, sizeof(head), "%d of %d", a.tourStep + 1, kStepCount);
    textAt(g, x + 8, y + 6, head, C_FAINT, &fonts::Font0);
    textAt(g, x + w - 8, y + 6, "BKSP=skip", C_FAINT, &fonts::Font0, textdatum_t::top_right);

    textAt(g, x + 8, y + 20, st.title, C_ACCENT, &fonts::Font2);
    textAt(g, x + 8, y + 40, st.body, C_DIM, &fonts::Font0);
    textAt(g, x + 8, y + 54, st.prompt, C_TEXT, &fonts::Font0);

    // Progress pips.
    for (int i = 0; i < kStepCount; ++i) {
        const int px = x + 8 + i * 9;
        if (i < a.tourStep) g.fillRect(px, y + h - 12, 6, 4, C_OK);
        else if (i == a.tourStep) g.fillRect(px, y + h - 12, 6, 4, C_ACCENT);
        else g.fillRect(px, y + h - 12, 6, 4, C_FAINT);
    }
}

}  // namespace synth
