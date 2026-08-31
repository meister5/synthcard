// Drives the real UI code off-device under AddressSanitizer.
//
// Every screen is drawn and every action dispatched across a wide spread of
// application states, because the failure mode we care about - an out-of-range
// index or a bad string pointer - reboots the device rather than misdrawing.
#include "app.h"
#include "ui/ui.h"
#include "music/music.h"
#include <cstdio>
#include <cstring>

void hostCheckString(const char* s);

using namespace synth;

static int g_fail = 0, g_run = 0;
#define CHECK(cond, ...) do { ++g_run; if (!(cond)) { ++g_fail; \
    printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static App app;

static void drawEveryScreen(const char* what) {
    for (int m = 0; m < M_COUNT; ++m) {
        app.mode = (Mode)m;
        ++g_run;
        uiDraw(app);                       // top bar + screen + hints + overlays
    }
    (void)what;
}

// Runs every action, with every argument it can carry, on every screen.
static void dispatchEverything() {
    for (int m = 0; m < M_COUNT; ++m) {
        app.mode = (Mode)m;
        for (int actId = 0; actId <= A_LOAD; ++actId) {
            for (int arg = -1; arg <= 16; ++arg) {
                Action act;
                act.act = (Act)actId;
                act.arg = (int8_t)arg;
                ++g_run;
                screenAction(app, act);
                uiDraw(app);               // redraw: the action may have moved a cursor
            }
        }
    }
}

// Walks the mode list the way TAB does, drawing at every stop.
static void tabThroughEverything(int laps) {
    for (int i = 0; i < laps * M_COUNT; ++i) {
        app.mode = (Mode)((app.mode + 1) % M_COUNT);
        ++g_run;
        uiDraw(app);
        Action next;
        next.act = A_MODE_NEXT;
        screenAction(app, next);
    }
}

static void withEveryHelpPage() {
    app.helpOpen = true;
    for (int m = 0; m < M_COUNT; ++m) {
        app.mode = (Mode)m;
        for (int p = 0; p < 4; ++p) { app.helpPage = (uint8_t)p; ++g_run; uiDraw(app); }
    }
    app.helpOpen = false;
    app.menuOpen = true;
    for (int c = 0; c < 20; ++c) { app.menuCursor = (uint8_t)c; ++g_run; uiDraw(app); }
    app.menuOpen = false;
}

// Every action must have a label. This is what makes the live legend
// trustworthy: a binding cannot be added without a name, so it cannot become
// one more invisible key.
static void everyActionIsLabelled() {
    for (int i = A_PLAY_STOP; i <= A_LOAD; ++i) {
        const char* l = actionLabel((Act)i);
        ++g_run;
        if (!l || l[0] == 0) {
            ++g_fail;
            printf("FAIL action %d has no label\n", i);
        } else {
            hostCheckString(l);
            CHECK(strlen(l) <= 16, "action %d label '%s' is too long for the legend", i, l);
        }
    }
    CHECK(actionLabel(A_NONE)[0] == 0, "A_NONE should have no label");

    // And every binding the input layer can produce must resolve to one.
    static const bool flags[2] = {false, true};
    for (bool fn : flags) for (bool sh : flags) for (bool ct : flags) for (bool al : flags) {
        Mods m; m.fn = fn; m.shift = sh; m.ctrl = ct; m.alt = al;
        for (uint8_t id = 0; id < kKeyCount; ++id) {
            const Action a = mapAction(id, m);
            if (a.act == A_NONE) continue;
            ++g_run;
            if (actionLabel(a.act)[0] == 0) {
                ++g_fail;
                printf("FAIL key %s binds action %d, which has no label\n", keyName(id), a.act);
            }
        }
    }
}

// The legend draws for every modifier on every screen, under ASan.
static void legendOnEveryScreen() {
    for (int m = 0; m < M_COUNT; ++m) {
        app.mode = (Mode)m;
        for (int which = 0; which < 4; ++which) {
            app.lastMods = Mods();
            app.lastMods.fn    = (which == 0);
            app.lastMods.shift = (which == 1);
            app.lastMods.ctrl  = (which == 2);
            app.lastMods.alt   = (which == 3);
            app.modsSince = 0;
            ++g_run;
            uiDrawLegend(app);
        }
    }
    app.lastMods = Mods();
}

// The tour runs end to end, and every wrong key leaves it where it was.
static void walkTheTour() {
    app.settings.tourDone = 0;
    tourStart(app);
    CHECK(app.tourOpen, "tour did not open");
    int guard = 0;
    while (app.tourOpen && guard++ < 200) {
        const uint8_t before = app.tourStep;
        ++g_run;
        uiDrawTour(app);
        // Every key on the board; only the one the step wants advances it.
        for (uint8_t id = 0; id < kKeyCount && app.tourOpen; ++id) {
            if (id == KID(0, 13)) continue;         // BKSP would skip out
            tourKey(app, id, true);
            tourKey(app, id, false);
            if (app.tourStep != before) break;
        }
        CHECK(app.tourStep != before || !app.tourOpen, "tour step %d accepted no key", before);
    }
    CHECK(!app.tourOpen, "tour never finished");
    CHECK(app.settings.tourDone == 1, "finishing the tour did not record it");

    // And BKSP leaves from any step.
    for (int step = 0; step < tourStepCount(); ++step) {
        app.settings.tourDone = 0;
        tourStart(app);
        app.tourStep = (uint8_t)step;
        tourKey(app, KID(0, 13), true);
        CHECK(!app.tourOpen, "BKSP did not skip the tour from step %d", step);
    }
}

int main() {
    printf("SynthCard UI tests\n");
    // Pure table checks, safe before there is a canvas.
    everyActionIsLabelled();

    app.proj.reset();
    app.engine.begin(&app.proj);
    app.engine.setUndoBuffer(&app.undoBuf);
    app.clipboard.clear();
    uiBegin();

    // Anything that draws has to come after uiBegin().
    legendOnEveryScreen();
    walkTheTour();

    // 1. a freshly reset project
    drawEveryScreen("fresh");
    tabThroughEverything(3);
    withEveryHelpPage();

    // 2. boot screen
    for (int i = 0; i < BOOT_COUNT + 2; ++i) { app.bootSel = (uint8_t)i; ++g_run; uiDrawBoot(app); }

    // 3. empty patterns, every pattern length, every cursor position
    Rng rng;
    rng.s = 0xC0FFEE;
    for (int len = 1; len <= kMaxSteps; ++len) {
        for (int p = 0; p < kPatternCount; ++p) app.proj.pat[p].length = (uint8_t)len;
        app.seqStep  = (uint8_t)rng.below(16);
        app.seqPage  = (uint8_t)rng.below(8);
        app.seqField = (uint8_t)rng.below(6);
        app.seqTrack = (uint8_t)rng.below(2);
        app.drumLane = (uint8_t)rng.below(DL_COUNT + 2);
        app.drumPage = (uint8_t)rng.below(8);
        app.drumParam = (uint8_t)rng.below(DP_COUNT + 1);
        app.soundPage = (uint8_t)rng.below(synthPageCount(app.proj.patch[0].engine()) + 2u);
        app.soundCursor = (uint8_t)rng.below(8);
        app.fxCursor = (uint8_t)rng.below(FX_COUNT + 2);
        app.songCursor = (uint8_t)rng.below(kSongSlots + 2);
        app.sysCursor = (uint8_t)rng.below(14);
        app.fileCursor = (uint8_t)rng.below(6);
        app.fileAction = (uint8_t)rng.below(6);
        drawEveryScreen("lengths");
    }

    // 4. a full song: notes everywhere, chords everywhere, every scale
    for (uint8_t scale = 0; scale < kScaleCount; ++scale) {
        app.proj.scale = scale;
        app.proj.root = (uint8_t)rng.below(12);
        app.proj.octave = (uint8_t)rng.below(9);
        app.chordMode = (uint8_t)rng.below(CHORD_COUNT);
        for (int p = 0; p < kPatternCount; ++p) {
            app.proj.pat[p].length = (uint8_t)(1 + rng.below(kMaxSteps));
            randomDrums(app.proj.pat[p], rng, 80);
            randomMelody(app.proj.pat[p], 0, rng, app.proj.root, scale, app.proj.octave);
            randomBass(app.proj.pat[p], 1, rng, app.proj.root, scale, app.proj.octave);
            for (int t = 0; t < kMelTracks; ++t)
                for (int s = 0; s < kMaxSteps; ++s) {
                    app.proj.pat[p].mel[t][s].chord = (uint8_t)rng.below(CHORD_COUNT);
                    app.proj.pat[p].mel[t][s].gate  = (uint8_t)rng.below(kGateMax + 1);
                }
        }
        drawEveryScreen("full");
    }

    // 5. extremes: highest and lowest notes, max gates, every chord type
    for (int p = 0; p < kPatternCount; ++p) {
        app.proj.pat[p].length = kMaxSteps;
        for (int t = 0; t < kMelTracks; ++t)
            for (int s = 0; s < kMaxSteps; ++s) {
                Step& st = app.proj.pat[p].mel[t][s];
                st.note  = (uint8_t)(s & 1 ? 127 : 1);
                st.vel   = 127;
                st.gate  = kGateMax;
                st.chord = CHORD_SEVENTH;
                st.flags = 0xFF;
            }
    }
    app.proj.scale = 0;
    drawEveryScreen("extremes");
    app.proj.scale = kScaleCount - 1;
    drawEveryScreen("extremes-scale");

    // 6. every preset and kit on the SOUND / DRUM pages
    for (uint16_t i = 0; i < kPresetCount; ++i) {
        loadPreset(app.proj.patch[0], i);
        loadPreset(app.proj.patch[1], (uint16_t)((i * 7) % kPresetCount));
        app.presetIndex[0] = i;
        for (uint8_t pg = 0; pg < synthPageCount(app.proj.patch[0].engine()); ++pg) {
            app.soundPage = pg;
            for (uint8_t c = 0; c < 6; ++c) { app.soundCursor = c; ++g_run; uiDraw(app); }
        }
    }
    for (uint8_t k = 0; k < kKitCount; ++k) {
        loadKit(app.proj.kit, k);
        app.mode = M_DRUM;
        for (uint8_t l = 0; l < DL_COUNT; ++l) { app.drumLane = l; ++g_run; uiDraw(app); }
    }

    // 7. transport running, recording, song mode, queued patterns
    app.engine.post(EV_PLAY);
    app.engine.post(EV_REC, 1);
    app.engine.post(EV_PATTERN, 5, 0);
    static int16_t out[kBlockSize];
    for (int b = 0; b < 400; ++b) {
        app.engine.renderBlock(out, kBlockSize);
        if ((b % 16) == 0) drawEveryScreen("playing");
    }
    app.engine.post(EV_SONGMODE, 1);
    for (int b = 0; b < 200; ++b) {
        app.engine.renderBlock(out, kBlockSize);
        if ((b % 16) == 0) drawEveryScreen("song");
    }

    // 8. now hammer every action on every screen, twice, redrawing each time
    dispatchEverything();
    dispatchEverything();
    tabThroughEverything(5);

    // 9. every physical key, under every modifier combination, on every screen -
    //    the real mapAction path rather than synthesised Action values.
    for (int mods = 0; mods < 16; ++mods) {
        Mods m;
        m.fn    = mods & 1;
        m.shift = mods & 2;
        m.ctrl  = mods & 4;
        m.alt   = mods & 8;
        for (uint8_t id = 0; id < kKeyCount; ++id) {
            Action act = mapAction(id, m);
            if (act.act == A_STEP)
                CHECK(act.arg >= 0 && act.arg < 16, "key %s gave step arg %d", keyName(id), act.arg);
            // The number row offers eight jumps; only the first M_COUNT name
            // a mode, and the app must ignore the rest rather than wrap.
            if (act.act == A_MODE_JUMP) {
                CHECK(act.arg >= 0 && act.arg < 8, "key %s gave mode arg %d", keyName(id), act.arg);
                if (act.arg >= M_COUNT) {
                    // handleAction lives inside app.cpp; screenAction is the
                    // reachable half, and neither may move the mode.
                    const Mode before = app.mode;
                    screenAction(app, act);
                    CHECK(app.mode == before, "an out-of-range mode jump moved the mode");
                }
            }
            if (act.act == A_PATTERN_SEL)
                CHECK(act.arg >= 0 && act.arg < kPatternCount, "key %s gave pattern %d",
                      keyName(id), act.arg);
            int8_t semi = keyToSemitone(id);
            CHECK(semi >= -1 && semi <= 16, "key %s gave semitone %d", keyName(id), semi);
            int8_t lane = keyToDrumLane(id);
            CHECK(lane >= -1 && lane < DL_COUNT, "key %s gave drum lane %d", keyName(id), lane);
            hostCheckString(keyName(id));
            for (int mo = 0; mo < M_COUNT; ++mo) {
                app.mode = (Mode)mo;
                ++g_run;
                screenAction(app, act);
                uiDraw(app);
            }
        }
    }

    // 10. degenerate song data that a corrupt file could produce
    app.proj.song.length = 1;
    app.songCursor = 0;
    drawEveryScreen("song-min");
    app.proj.song.length = kSongSlots;
    app.songCursor = kSongSlots - 1;
    drawEveryScreen("song-max");

    printf("%d operations, %d failures\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
