#include "app.h"
#include "ui/ui.h"
#include "music/music.h"
#include <M5Cardputer.h>

namespace synth {

static App a;

// ===========================================================================
// small shared helpers
// ===========================================================================
void showParam(App& app, const char* name, const char* value, float frac) {
    strncpy(app.ovName, name, sizeof(app.ovName) - 1);
    app.ovName[sizeof(app.ovName) - 1] = 0;
    strncpy(app.ovValue, value, sizeof(app.ovValue) - 1);
    app.ovValue[sizeof(app.ovValue) - 1] = 0;
    app.ovFrac  = frac;
    app.ovUntil = millis() + 750;
}

void showToast(App& app, const char* msg, bool error) {
    strncpy(app.toast, msg, sizeof(app.toast) - 1);
    app.toast[sizeof(app.toast) - 1] = 0;
    app.toastError = error;
    app.toastUntil = millis() + (error ? 2200 : 1200);
}

void applyPreset(App& app, uint8_t track, int index) {
    if (track >= kMelTracks) return;
    int n = (int)kPresetCount;
    index = ((index % n) + n) % n;
    app.presetIndex[track] = (uint16_t)index;
    loadPreset(app.proj.patch[track], (uint16_t)index);
    showParam(app, track == 0 ? "LEAD SOUND" : "BASS SOUND", app.proj.patch[track].name, -1.0f);
}

void applyKit(App& app, int index) {
    int n = (int)kKitCount;
    index = ((index % n) + n) % n;
    app.kitIndex = (uint8_t)index;
    loadKit(app.proj.kit, (uint8_t)index);
    showParam(app, "DRUM KIT", app.proj.kit.name, -1.0f);
}

uint8_t liveBaseNote(const App& app) {
    return (uint8_t)clampi((app.proj.octave + 1) * 12 + app.proj.root, 0, 120);
}

void refreshFileList(App& app) {
    app.engine.suspendAudio();
    app.fileCount = projectList(app.fileNames, kMaxProjectFiles);
    app.engine.resumeAudio();
    app.fileListValid = true;
    app.fileCursor = 0;
    if (app.fileCount == 0) showToast(app, "NO PROJECTS ON CARD", true);
}

// Call before anything that throws work away. Cheap enough (one 9 KB copy) to
// sit in front of every destructive action.
void snapshotUndo(App& app) {
    app.undoBuf = app.proj;
    app.undoValid = true;
}

// ===========================================================================
// live keyboard
// ===========================================================================
// Which scale degree a note sits on, so chord mode can stack thirds that stay
// inside the current key.
static int noteToDegree(uint8_t note, uint8_t rootPc, uint8_t scaleIdx) {
    int rootNote = rootPc;
    while (rootNote + 12 <= (int)note) rootNote += 12;
    for (int d = -24; d < 60; ++d)
        if (scaleDegree(rootPc, d, scaleIdx) >= note) return d;
    return 0;
}

static void buildChord(const App& app, uint8_t note, uint8_t* out, int& n) {
    n = 0;
    out[n++] = note;
    switch (app.chordMode) {
        case CH_POWER:
            out[n++] = (uint8_t)clampi(note + 7, 0, 127);
            out[n++] = (uint8_t)clampi(note + 12, 0, 127);
            break;
        case CH_TRIAD:
        case CH_SEVENTH: {
            if (app.proj.scale == 0) {
                out[n++] = (uint8_t)clampi(note + 4, 0, 127);
                out[n++] = (uint8_t)clampi(note + 7, 0, 127);
                if (app.chordMode == CH_SEVENTH) out[n++] = (uint8_t)clampi(note + 11, 0, 127);
            } else {
                int d = noteToDegree(note, app.proj.root, app.proj.scale);
                out[n++] = scaleDegree(app.proj.root, d + 2, app.proj.scale);
                out[n++] = scaleDegree(app.proj.root, d + 4, app.proj.scale);
                if (app.chordMode == CH_SEVENTH) out[n++] = scaleDegree(app.proj.root, d + 6, app.proj.scale);
            }
            break;
        }
        default: break;
    }
}

static void noteKeyDown(App& app, uint8_t id, int8_t semi, const Mods& m) {
    if (app.mode == M_DRUM) {
        int8_t lane = keyToDrumLane(id);
        if (lane >= 0) {
            app.drumLane = (uint8_t)lane;
            app.engine.drumHit((uint8_t)lane, m.shift ? 127 : 105);
        }
        return;
    }
    int n = liveBaseNote(app) + semi;
    if (app.proj.scale) n = scaleQuantize(n, app.proj.root, app.proj.scale);
    n = clampi(n, 0, 127);

    uint8_t vel = m.shift ? 127 : (m.alt ? 62 : 100);
    uint8_t chord[4];
    int cn = 0;
    buildChord(app, (uint8_t)n, chord, cn);
    const uint8_t tr = app.engine.liveTrack();
    for (int i = 0; i < cn && i < 4; ++i) {
        app.keyNotes[id][i] = chord[i];
        // Only the root is recorded; a sequencer track is monophonic, so
        // recording every chord tone would just leave the top note behind.
        app.engine.noteOn(i == 0 ? tr : (uint8_t)(tr | AudioEngine::kNoRecord), chord[i], vel);
    }
    for (int i = cn; i < 4; ++i) app.keyNotes[id][i] = 0;

    // Step entry: in SEQ mode a played key writes into the selected step.
    // Skipped while the transport is recording, or the same keypress would be
    // entered twice - once at the cursor and once under the playhead.
    if (app.mode == M_SEQ && !(app.engine.seq().recording() && app.engine.seq().playing())) {
        Pattern& p = app.proj.pat[app.engine.seq().currentPattern()];
        const int len = patternLength(app.proj, app.engine.seq().currentPattern());
        const int pages = (len + 15) / 16;
        int st = clampi((app.seqPage % (pages ? pages : 1)) * 16 + (app.seqStep % 16), 0, kMaxSteps - 1);
        Step& s = p.mel[app.seqTrack % kMelTracks][st];
        s.note = (uint8_t)n;
        s.vel = vel;
        if (s.gate == 0) s.gate = 8;
        s.flags = (uint8_t)(s.flags & 0xF0);
        app.seqStep = (uint8_t)((app.seqStep + 1) % 16);
    }
}

static void noteKeyUp(App& app, uint8_t id) {
    const uint8_t tr = app.engine.liveTrack();
    for (int i = 0; i < 4; ++i) {
        if (!app.keyNotes[id][i]) continue;
        app.engine.noteOff(i == 0 ? tr : (uint8_t)(tr | AudioEngine::kNoRecord), app.keyNotes[id][i]);
        app.keyNotes[id][i] = 0;
    }
}

// ===========================================================================
// global actions
// ===========================================================================
static void tapTempo(App& app) {
    uint32_t now = millis();
    if (app.tapCount && now - app.tapTimes[(app.tapCount - 1) % 4] > 2500) app.tapCount = 0;
    app.tapTimes[app.tapCount % 4] = now;
    ++app.tapCount;
    if (app.tapCount >= 3) {
        int taps = app.tapCount < 4 ? app.tapCount : 4;
        uint32_t first = app.tapTimes[(app.tapCount - taps) % 4];
        uint32_t last  = app.tapTimes[(app.tapCount - 1) % 4];
        uint32_t span  = last - first;
        if (span > 0) {
            int bpm = (int)(60000.0f * (taps - 1) / (float)span);
            app.proj.bpm = (uint16_t)clampi(bpm, 40, 300);
            char v[16]; snprintf(v, sizeof(v), "%d", app.proj.bpm);
            showParam(app, "TAP TEMPO", v, (app.proj.bpm - 40) / 260.0f);
        }
    } else showToast(app, "TAP...");
}

static void doSave(App& app) {
    char err[48];
    app.engine.suspendAudio();
    bool ok = projectSave(app.proj, app.proj.name, err, sizeof(err));
    app.engine.resumeAudio();
    if (ok) {
        strncpy(app.settings.lastProject, app.proj.name, kNameLen - 1);
        settingsSave(app.settings);
        app.fileListValid = false;
        char m[48]; snprintf(m, sizeof(m), "SAVED %s", app.proj.name);
        showToast(app, m);
    } else showToast(app, err, true);
}

static void quickJamSetup(App& app) {
    app.proj.reset();
    Pattern& p = app.proj.pat[0];
    p.length = 16;
    for (int i = 0; i < 16; i += 4) p.drum[DL_KICK][i] = 110;
    p.drum[DL_SNARE][4] = 105;
    p.drum[DL_SNARE][12] = 105;
    for (int i = 0; i < 16; i += 2) p.drum[DL_CHH][i] = (i % 4 == 0) ? 100 : 66;
    p.drum[DL_OHH][14] = 84;
    applyPreset(app, 0, 0);
    applyPreset(app, 1, 6);      // ACID BASS
    applyKit(app, 0);
    app.proj.fx.set(FX_DLY_MIX, 26);
    app.proj.fx.set(FX_REV_MIX, 30);
    app.mode = M_PLAY;
    showToast(app, "QUICK JAM - SPACE TO START");
}

static bool handleGlobal(App& app, const Action& act) {
    Sequencer& s = app.engine.seq();
    switch (act.act) {
        case A_PLAY_STOP: app.engine.post(EV_TOGGLE); return true;
        case A_RECORD:
            if (!s.recording()) snapshotUndo(app);     // so a whole take can be undone
            app.engine.post(EV_REC, s.recording() ? 0 : 1);
            if (!s.playing()) app.engine.post(EV_PLAY);
            showToast(app, s.recording() ? "REC OFF" : "REC ON");
            return true;
        case A_MODE_NEXT: app.mode = (Mode)((app.mode + 1) % M_COUNT); return true;
        case A_MODE_PREV: app.mode = (Mode)((app.mode + M_COUNT - 1) % M_COUNT); return true;
        case A_MODE_JUMP: app.mode = (Mode)(act.arg % M_COUNT); return true;
        case A_MENU: app.menuOpen = !app.menuOpen; app.menuCursor = 0; return true;
        case A_HELP: app.helpOpen = !app.helpOpen; return true;
        case A_TAP_TEMPO: tapTempo(app); return true;
        case A_BPM_DOWN: case A_BPM_UP: {
            int d = (act.act == A_BPM_UP ? 1 : -1) * (act.arg ? act.arg : 1);
            app.proj.bpm = (uint16_t)clampi(app.proj.bpm + d, 40, 300);
            char v[16]; snprintf(v, sizeof(v), "%d", app.proj.bpm);
            showParam(app, "BPM", v, (app.proj.bpm - 40) / 260.0f);
            return true;
        }
        case A_OCT_DOWN: case A_OCT_UP: {
            app.proj.octave = (uint8_t)clampi(app.proj.octave + (act.act == A_OCT_UP ? 1 : -1), 0, 8);
            char v[16]; snprintf(v, sizeof(v), "%d", app.proj.octave);
            showParam(app, "OCTAVE", v, app.proj.octave / 8.0f);
            return true;
        }
        case A_PATTERN_SEL: {
            app.engine.post(EV_PATTERN, (uint8_t)act.arg, s.playing() ? 0 : 1);
            char m[32]; snprintf(m, sizeof(m), s.playing() ? "PATTERN %d QUEUED" : "PATTERN %d", act.arg + 1);
            showToast(app, m);
            return true;
        }
        case A_UNDO:
            if (!app.undoValid) { showToast(app, "NOTHING TO UNDO", true); return true; }
            app.engine.requestUndo();
            showToast(app, "UNDO");
            return true;
        case A_PATLEN_DOWN: case A_PATLEN_UP: {
            Pattern& pp = app.proj.pat[s.currentPattern()];
            pp.length = (uint8_t)clampi(pp.length + (act.act == A_PATLEN_UP ? 1 : -1), 1, kMaxSteps);
            char v[16]; snprintf(v, sizeof(v), "%d", pp.length);
            showParam(app, "PATTERN LEN", v, pp.length / (float)kMaxSteps);
            return true;
        }
        case A_PATTERN_STEP: {
            // Step relative to whatever is already queued, so two taps move two.
            int base = (s.queuedPattern() != 0xFF) ? s.queuedPattern() : s.currentPattern();
            int n = (base + kPatternCount + act.arg) % kPatternCount;
            app.engine.post(EV_PATTERN, (uint8_t)n, s.playing() ? 0 : 1);
            char m[32];
            snprintf(m, sizeof(m), s.playing() ? "PATTERN %d NEXT BAR" : "PATTERN %d", n + 1);
            showToast(app, m);
            return true;
        }
        case A_SONG_MODE:
            app.engine.post(EV_SONGMODE, s.songMode() ? 0 : 1);
            showToast(app, s.songMode() ? "PATTERN MODE" : "SONG MODE");
            return true;
        case A_ARP_TOGGLE:
            app.proj.arpOn = app.proj.arpOn ? 0 : 1;
            app.engine.post(EV_ARP_ON, app.proj.arpOn);
            showToast(app, app.proj.arpOn ? "ARP ON" : "ARP OFF");
            return true;
        case A_ARP_MODE:
            app.proj.arpMode = (uint8_t)((app.proj.arpMode + 1) % ARP_MODE_COUNT);
            showParam(app, "ARP MODE", kArpModeNames[app.proj.arpMode], -1.0f);
            return true;
        case A_RND_DRUMS:
            snapshotUndo(app);
            randomDrums(app.proj.pat[s.currentPattern()], app.rng, 65);
            showToast(app, "RANDOM BEAT");
            return true;
        case A_RND_BASS:
            snapshotUndo(app);
            randomBass(app.proj.pat[s.currentPattern()], 1, app.rng, app.proj.root,
                       app.proj.scale ? app.proj.scale : 4, (uint8_t)clampi(app.proj.octave - 1, 1, 7));
            showToast(app, "RANDOM BASS");
            return true;
        case A_RND_MELODY:
            snapshotUndo(app);
            randomMelody(app.proj.pat[s.currentPattern()], 0, app.rng, app.proj.root,
                         app.proj.scale ? app.proj.scale : 4, app.proj.octave);
            showToast(app, "RANDOM LEAD");
            return true;
        case A_RND_SOUND:
            snapshotUndo(app);
            randomizePatch(app.proj.patch[app.engine.liveTrack()], app.rng, 45);
            showToast(app, "RANDOM SOUND");
            return true;
        case A_COPY:
            app.clipboard = app.proj.pat[s.currentPattern()];
            app.clipboardValid = true;
            showToast(app, "PATTERN COPIED");
            return true;
        case A_PASTE:
            if (!app.clipboardValid) { showToast(app, "CLIPBOARD EMPTY", true); return true; }
            snapshotUndo(app);
            app.proj.pat[s.currentPattern()] = app.clipboard;
            showToast(app, "PATTERN PASTED");
            return true;
        case A_CLEAR_PATTERN: {
            snapshotUndo(app);
            char keep[9];
            memcpy(keep, app.proj.pat[s.currentPattern()].name, sizeof(keep));
            app.proj.pat[s.currentPattern()].clear();
            memcpy(app.proj.pat[s.currentPattern()].name, keep, sizeof(keep));
            showToast(app, "PATTERN CLEARED");
            return true;
        }
        case A_SAVE: doSave(app); return true;
        case A_LOAD: app.mode = M_FILE; app.fileAction = 2; refreshFileList(app); return true;
        default: return false;
    }
}

static void handleFallback(App& app, const Action& act) {
    switch (act.act) {
        case A_PRESET_PREV:
            applyPreset(app, app.engine.liveTrack(), (int)app.presetIndex[app.engine.liveTrack()] - (act.arg > 1 ? 5 : 1));
            break;
        case A_PRESET_NEXT:
            applyPreset(app, app.engine.liveTrack(), (int)app.presetIndex[app.engine.liveTrack()] + (act.arg > 1 ? 5 : 1));
            break;
        case A_MUTE: {
            Pattern& p = app.proj.pat[app.engine.seq().currentPattern()];
            uint8_t tr = app.engine.liveTrack();
            p.muteMel ^= (uint8_t)(1u << tr);
            showToast(app, ((p.muteMel >> tr) & 1) ? "TRACK MUTED" : "TRACK ON");
            break;
        }
        case A_CLEAR_TRACK:
            snapshotUndo(app);
            app.proj.pat[app.engine.seq().currentPattern()].clearTrack(app.engine.liveTrack());
            showToast(app, "TRACK CLEARED");
            break;
        default: break;
    }
}

// ===========================================================================
// menu
// ===========================================================================
static void menuActivate(App& app) {
    Sequencer& s = app.engine.seq();
    switch (app.menuCursor) {
        case 0: quickJamSetup(app); app.menuOpen = false; break;
        case 1: app.engine.post(EV_PANIC); app.proj.reset(); applyPreset(app, 0, 0);
                applyPreset(app, 1, 5); app.menuOpen = false; showToast(app, "NEW PROJECT"); break;
        case 2: app.menuOpen = false; doSave(app); break;
        case 3: app.menuOpen = false; app.mode = M_FILE; app.fileAction = 2; refreshFileList(app); break;
        case 4: randomDrums(app.proj.pat[s.currentPattern()], app.rng, 65); showToast(app, "RANDOM BEAT"); break;
        case 5: randomBass(app.proj.pat[s.currentPattern()], 1, app.rng, app.proj.root,
                           app.proj.scale ? app.proj.scale : 4, (uint8_t)clampi(app.proj.octave - 1, 1, 7));
                showToast(app, "RANDOM BASS"); break;
        case 6: randomMelody(app.proj.pat[s.currentPattern()], 0, app.rng, app.proj.root,
                             app.proj.scale ? app.proj.scale : 4, app.proj.octave);
                showToast(app, "RANDOM LEAD"); break;
        case 7: randomizePatch(app.proj.patch[app.engine.liveTrack()], app.rng, 45);
                showToast(app, "RANDOM SOUND"); break;
        case 8: app.chordMode = (uint8_t)((app.chordMode + 1) % CH_COUNT); break;
        case 9: app.proj.scale = (uint8_t)((app.proj.scale + 1) % kScaleCount); break;
        case 10: app.proj.root = (uint8_t)((app.proj.root + 1) % 12); break;
        case 11: app.proj.swing = (uint8_t)((app.proj.swing + 5) % 105); break;
        case 12: app.menuOpen = false; app.helpOpen = true; break;
        default: app.menuOpen = false; break;
    }
}

// ===========================================================================
// per-frame input
// ===========================================================================
// The key map stores TAB/ENTER/BACKSPACE as HID codes that happen to land in
// the printable ASCII range ('+', '(', '*'), so they have to be excluded by
// position rather than by value.
static char charForKey(uint8_t id, bool shift) {
    uint8_t r = id / 14, c = id % 14;
    if (r > 3 || c > 13) return 0;
    if ((r == 1 && c == 0) || (r == 2 && c == 13) || (r == 0 && c == 13)) return 0;  // TAB ENTER BKSP
    if (r == 2 && c <= 1) return 0;                                                  // FN SHIFT
    if (r == 3 && c <= 2) return 0;                                                  // CTRL OPT ALT
    if (r == 3 && c == 13) return 0;                                                 // SPACE stays transport
    KeyValue_t kv = _key_value_map[r][c];
    char ch = shift ? kv.value_second : kv.value_first;
    return (ch >= 32 && ch < 127) ? ch : 0;
}

// Project names are uppercase A-Z, 0-9, '-' and '_' so that what the field
// shows is exactly the filename that lands on the card.
static char nameCharFor(uint8_t id, bool shift) {
    char ch = charForKey(id, shift);
    if (ch >= 'a' && ch <= 'z') return (char)(ch - 32);
    if (ch >= 'A' && ch <= 'Z') return ch;
    if (ch >= '0' && ch <= '9') return ch;
    if (ch == '-' || ch == '_') return ch;
    return 0;
}

static void handleBootKey(App& app, const KeyEvent& e) {
    if (!e.pressed) return;
    const uint8_t up = KID(2, 11), down = KID(3, 11), enter = KID(2, 13);
    if (e.id == up)        app.bootSel = (uint8_t)((app.bootSel + BOOT_COUNT - 1) % BOOT_COUNT);
    else if (e.id == down) app.bootSel = (uint8_t)((app.bootSel + 1) % BOOT_COUNT);
    else if (e.id == enter || e.id == KID(3, 13)) {
        switch (app.bootSel) {
            case BOOT_JAM: quickJamSetup(app); break;
            case BOOT_NEW: app.proj.reset(); applyPreset(app, 0, 0); applyPreset(app, 1, 5);
                           app.mode = M_PLAY; showToast(app, "NEW PROJECT"); break;
            default:       app.mode = M_FILE; app.fileAction = 2; refreshFileList(app); break;
        }
        app.booted = true;
    }
}

static void handleKey(App& app, const KeyEvent& e) {
    const Mods m{e.fn, e.shift, e.ctrl, e.alt};

    if (app.helpOpen) {
        if (!e.pressed) return;
        if (e.fn && e.id == KID(3, 12)) { app.helpPage = (uint8_t)((app.helpPage + 1) % uiHelpPageCount()); return; }
        if (e.fn && e.id == KID(3, 10)) { app.helpPage = (uint8_t)((app.helpPage + uiHelpPageCount() - 1) % uiHelpPageCount()); return; }
        app.helpOpen = false;
        return;
    }

    if (app.menuOpen) {
        if (!e.pressed) return;
        Action act = mapAction(e.id, m);
        if (act.act == A_UP)        app.menuCursor = (uint8_t)((app.menuCursor + uiMenuCount() - 1) % uiMenuCount());
        else if (act.act == A_DOWN) app.menuCursor = (uint8_t)((app.menuCursor + 1) % uiMenuCount());
        else if (act.act == A_CONFIRM) menuActivate(app);
        else if (act.act == A_MENU || act.act == A_BACK) app.menuOpen = false;
        else if (act.act == A_VALUE_UP || act.act == A_VALUE_DOWN) menuActivate(app);
        return;
    }

    // FILE screen turns the whole letter keyboard into a text field for the
    // project name. TAB, ENTER, BKSP, SPACE and the FN layer stay commands.
    if (app.mode == M_FILE && app.fileAction == 0 && e.pressed && !m.fn) {
        char ch = nameCharFor(e.id, m.shift);
        if (ch) {
            int n = (int)strlen(app.proj.name);
            if (n < kNameLen - 1) { app.proj.name[n] = ch; app.proj.name[n + 1] = 0; }
            return;
        }
    }

    // Musical keys (unless the command layer is active).
    int8_t semi = keyToSemitone(e.id);
    if (semi >= 0 && !m.fn && !m.ctrl && app.mode != M_FILE) {
        if (e.pressed) noteKeyDown(app, e.id, semi, m);
        else           noteKeyUp(app, e.id);
        return;
    }
    if (!e.pressed) { noteKeyUp(app, e.id); return; }

    Action act = mapAction(e.id, m);
    if (act.act == A_NONE) return;
    if (handleGlobal(app, act)) return;
    if (screenAction(app, act)) return;
    handleFallback(app, act);
}

// ===========================================================================
// entry points
// ===========================================================================
static bool s_adv = true;

void appSetup() {
    auto cfg = M5.config();
    cfg.internal_spk = true;
    cfg.internal_mic = false;      // Mic and Speaker share the codec; pick one.
    cfg.output_power = false;
    M5Cardputer.begin(cfg, true);

    s_adv = (M5.getBoard() == m5::board_t::board_M5CardputerADV);
    uiBegin();
    if (!s_adv) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setTextColor(TFT_RED);
        M5.Display.setTextSize(2);
        M5.Display.setCursor(8, 40);
        M5.Display.print("CARDPUTER ADV");
        M5.Display.setCursor(8, 62);
        M5.Display.print("REQUIRED");
        return;
    }

    settingsLoad(a.settings);
    M5.Display.setBrightness(a.settings.brightness ? a.settings.brightness : 80);

    a.rng.s = (uint32_t)esp_random();
    a.proj.reset();
    a.keys.begin();
    a.engine.begin(&a.proj);
    a.engine.setUndoBuffer(&a.undoBuf);
    a.engine.setMetronome(a.settings.metronome);
    audioSetVolume(a.settings.volume);
    if (!audioStart(&a.engine)) {
        showToast(a, "AUDIO INIT FAILED", true);
    }
    a.clipboard.clear();
    applyPreset(a, 0, 0);
    applyPreset(a, 1, 5);
    a.toastUntil = 0;
}

void appLoop() {
    if (!s_adv) { delay(200); return; }

    const uint32_t now = millis();
    M5.update();
    a.keys.update(now);

    if (a.keys.watchdogFired()) {
        a.engine.post(EV_ALL_OFF);
        for (int i = 0; i < kKeyCount; ++i) for (int j = 0; j < 4; ++j) a.keyNotes[i][j] = 0;
    }

    // Hold BACKSPACE while the transport runs to rub out whatever is under the
    // playhead - the standard groovebox erase, and the answer to "how do I
    // remove what I just recorded".
    if (a.booted && !a.menuOpen && !a.helpOpen && !a.keys.mods().fn &&
        a.keys.held(KID(0, 13)) && a.engine.seq().playing() &&
        !(a.mode == M_FILE || a.mode == M_SYS)) {
        uint8_t track = a.engine.liveTrack();
        if (a.mode == M_DRUM)     track = (uint8_t)(kMelTracks + a.drumLane);
        else if (a.mode == M_SEQ) track = (uint8_t)(a.seqTrack % kMelTracks);
        a.engine.eraseStep(track);
    }

    for (int i = 0; i < a.keys.eventCount(); ++i) {
        const KeyEvent& e = a.keys.event(i);
        if (!a.booted) handleBootKey(a, e);
        else           handleKey(a, e);
    }

    // ~33 fps cap: audio owns core 0, the display push owns core 1.
    if (now - a.lastDrawMs >= 30) {
        float dt = (float)(now - a.lastDrawMs);
        if (dt > 0.0f) a.fps += (1000.0f / dt - a.fps) * 0.15f;
        a.lastDrawMs = now;
        if (!a.booted) uiDrawBoot(a);
        else           uiDraw(a);
    }
    delay(2);
}

}  // namespace synth
