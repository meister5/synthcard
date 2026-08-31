// SynthCard - application state and top-level wiring.
#pragma once
#include <stdint.h>
#include "audio/engine.h"
#include "input/keys.h"
#include "storage/storage.h"
#include "sequencer/sequencer.h"
#include "music/music.h"

namespace synth {

enum Mode : uint8_t {
    // FILE and SYS are one screen with two pages: both are administration,
    // and one fewer thing to walk past while looking for something else.
    M_PLAY = 0, M_DRUM, M_SEQ, M_SOUND, M_FX, M_SONG, M_SETUP, M_COUNT
};
extern const char* const kModeNames[M_COUNT];
// The input layer binds this many mode-jump keys; keeping the two in step is
// what stops FN+8 advertising an eighth mode.
static_assert(kModeJumpCount == M_COUNT, "kModeJumpCount must match M_COUNT");

enum BootChoice : uint8_t { BOOT_JAM = 0, BOOT_NEW, BOOT_LOAD, BOOT_COUNT };
struct App {
    Project     proj;
    AudioEngine engine;
    Keys        keys;
    Settings    settings;
    Rng         rng;

    bool    booted   = false;
    uint8_t bootSel  = BOOT_JAM;
    Mode    mode     = M_PLAY;

    // per-screen cursors
    uint8_t soundPage = 0, soundCursor = 0;
    uint8_t fxCursor = 0;
    uint8_t drumLane = 0, drumPage = 0, drumParam = 0;
    uint8_t seqTrack = 0, seqStep = 0, seqPage = 0, seqField = 0;
    uint8_t songCursor = 0, songField = 0;
    uint8_t fileCursor = 0, fileAction = 0;
    uint8_t setupPage = 0;      // 0 = FILE, 1 = SYSTEM
    uint8_t sysCursor = 0;

    uint16_t presetIndex[kMelTracks] = {0, 5};
    uint8_t  kitIndex = 0;
    uint8_t  chordMode = CHORD_OFF;   // live keyboard; steps carry their own
    uint8_t  euclidHits = 4, euclidRot = 0;

    // live keyboard bookkeeping: which notes each physical key started
    uint8_t keyNotes[kKeyCount][4] = {{0}};

    // overlays
    char     ovName[14] = {0}, ovValue[14] = {0};
    float    ovFrac = 0.0f;
    uint32_t ovUntil = 0;
    char     toast[48] = {0};
    uint32_t toastUntil = 0;
    bool     toastError = false;
    bool     menuOpen = false, helpOpen = false;

    // Live legend: how long the current modifier set has been held. A quick
    // chord never reaches the threshold, so playing is never interrupted.
    Mods     lastMods;
    uint32_t modsSince = 0;

    // First-run walkthrough.
    bool     tourOpen = false;
    uint8_t  tourStep = 0;
    uint8_t  menuCursor = 0;
    uint8_t  helpPage = 0;

    // file browser
    char fileNames[kMaxProjectFiles][kNameLen] = {{0}};
    int  fileCount = 0;
    bool fileListValid = false;

    Pattern clipboard;
    bool    clipboardValid = false;

    // Single-level undo. Holds a whole Project so it covers patterns, patches,
    // the kit and the FX in one shot; CTRL+Z swaps it with the live song, so a
    // second press is a redo.
    Project undoBuf;
    bool    undoValid = false;

    uint32_t tapTimes[4] = {0};
    uint8_t  tapCount = 0;

    uint32_t lastDrawMs = 0;
    float    fps = 0.0f;
};

void appSetup();
void appLoop();

// --- shared helpers used by the screens ------------------------------------
void showParam(App& a, const char* name, const char* value, float frac);
void showToast(App& a, const char* msg, bool error = false);
void applyPreset(App& a, uint8_t track, int index);
void applyKit(App& a, int index);
uint8_t liveBaseNote(const App& a);
void refreshFileList(App& a);
void snapshotUndo(App& a);

// --- UI entry points --------------------------------------------------------
void uiBegin();
void uiDrawBoot(App& a);
void uiDraw(App& a);

// --- live legend (ui/legend.cpp) -------------------------------------------
// A modifier has to be held this long before the legend appears, so a fast
// chord - how the keys are actually played - never triggers it.
constexpr uint32_t kLegendHoldMs = 180;
bool legendActive(const App& a, uint32_t now);
void uiDrawLegend(App& a);

// --- first-run tour (ui/tour.cpp) ------------------------------------------
int  tourStepCount();
void tourStart(App& a);
void tourFinish(App& a);
bool tourKey(App& a, uint8_t id, bool pressed);
void uiDrawTour(App& a);

// Per-screen draw + input, implemented in screens.cpp.
int  uiMenuCount();
const char* uiMenuLabel(int i);
int  uiHelpPageCount();

void screenDraw(App& a, int x, int y, int w, int h);
bool screenAction(App& a, const Action& act);   // true when handled
const char* screenHint(const App& a);

}  // namespace synth
