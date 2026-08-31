#include "keys.h"
#include <M5Cardputer.h>

namespace synth {

// A key held longer than this with no controller traffic is treated as a lost
// release event. Eight seconds is far longer than any real note but short
// enough that a wedged FIFO does not leave a drone running.
static constexpr uint32_t kStuckMs = 8000;

void Keys::begin() {
    for (int i = 0; i < kKeyCount; ++i) { held_[i] = false; heldSince_[i] = 0; }
    evCount_ = 0;
}

void Keys::update(uint32_t nowMs) {
    evCount_ = 0;
    watchdog_ = false;

    // Each updateKeyList() consumes at most one FIFO entry; drain aggressively.
    for (int i = 0; i < 12; ++i) M5Cardputer.Keyboard.updateKeyList();

    bool now[kKeyCount] = {false};
    const auto& list = M5Cardputer.Keyboard.keyList();
    for (const auto& k : list) {
        if (k.y < 0 || k.y > 3 || k.x < 0 || k.x > 13) continue;
        now[KID((uint8_t)k.y, (uint8_t)k.x)] = true;
    }

    mods_.fn    = now[KID(2, 0)];
    mods_.shift = now[KID(2, 1)];
    mods_.ctrl  = now[KID(3, 0)];
    mods_.alt   = now[KID(3, 2)];

    for (uint8_t id = 0; id < kKeyCount; ++id) {
        if (now[id] && !held_[id]) {
            held_[id] = true;
            heldSince_[id] = nowMs;
            if (evCount_ < kMaxEvents)
                ev_[evCount_++] = {id, true, mods_.fn, mods_.shift, mods_.ctrl, mods_.alt};
        } else if (!now[id] && held_[id]) {
            held_[id] = false;
            if (evCount_ < kMaxEvents)
                ev_[evCount_++] = {id, false, mods_.fn, mods_.shift, mods_.ctrl, mods_.alt};
        } else if (now[id] && held_[id] && nowMs - heldSince_[id] > kStuckMs) {
            held_[id] = false;
            watchdog_ = true;
            if (evCount_ < kMaxEvents)
                ev_[evCount_++] = {id, false, mods_.fn, mods_.shift, mods_.ctrl, mods_.alt};
        }
    }
}

// --------------------------------------------------------------- mapping ---
// Row 3 whites: z x c v b n m , . /   Row 2 blacks: s d _ g h j _ l ;
int8_t keyToSemitone(uint8_t id) {
    uint8_t r = id / 14, c = id % 14;
    if (r == 3) {
        static const int8_t w[10] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16};
        if (c >= 3 && c <= 12) return w[c - 3];
    } else if (r == 2) {
        switch (c) {
            case 3:  return 1;   // s  C#
            case 4:  return 3;   // d  D#
            case 6:  return 6;   // g  F#
            case 7:  return 8;   // h  G#
            case 8:  return 10;  // j  A#
            case 10: return 13;  // l  C#'
            case 11: return 15;  // ;  D#'
            default: break;
        }
    }
    return -1;
}

int8_t keyToDrumLane(uint8_t id) {
    uint8_t r = id / 14, c = id % 14;
    // Ten pads along the white-key row, then the two black keys above its
    // left end for the last pair - the kit has twelve lanes and the bottom
    // row only has ten keys.
    if (r == 3 && c >= 3 && c <= 12) return (int8_t)(c - 3);        // Z .. /   lanes 0-9
    if (r == 2 && (c == 3 || c == 4)) return (int8_t)(10 + c - 3);  // S D      lanes 10-11
    return -1;
}

Action mapAction(uint8_t id, const Mods& m) {
    const uint8_t r = id / 14, c = id % 14;
    Action a;

    // CTRL is otherwise unused, which leaves the familiar undo chord free.
    if (m.ctrl && r == 3 && c == 3) { a.act = A_UNDO; return a; }

    if (r == 3 && c == 13) { a.act = m.fn ? A_TAP_TEMPO : A_PLAY_STOP; return a; }
    if (r == 1 && c == 0)  { a.act = m.fn ? A_MODE_PREV : A_MODE_NEXT; return a; }
    if (r == 0 && c == 0)  { a.act = m.fn ? A_MENU : A_HELP; return a; }
    if (r == 0 && c == 13) { a.act = m.fn ? A_CLEAR_TRACK : A_BACK; return a; }
    if (r == 2 && c == 13) { a.act = m.fn ? A_MENU : A_CONFIRM; return a; }
    if (r == 1 && c == 13) { a.act = m.fn ? A_SONG_MODE : A_RECORD; return a; }

    // Row 0 digits: steps 1-8, then BPM / octave.
    if (r == 0 && c >= 1 && c <= 8) {
        // Only the keys that name a real mode bind the jump; binding all eight
        // would put a mode in the legend and the manual that does not exist.
        if (m.fn && c <= kModeJumpCount) { a.act = A_MODE_JUMP; a.arg = (int8_t)(c - 1); return a; }
        if (m.fn)    { return a; }
        if (m.shift) { a.act = A_PATTERN_SEL; a.arg = (int8_t)(c - 1); return a; }
        a.act = A_STEP; a.arg = (int8_t)(c - 1); return a;
    }
    if (r == 0 && c == 9)  { a.act = A_BPM_DOWN; a.arg = m.fn ? 10 : 1; return a; }
    if (r == 0 && c == 10) { a.act = A_BPM_UP;   a.arg = m.fn ? 10 : 1; return a; }
    if (r == 0 && c == 11) { a.act = A_OCT_DOWN; return a; }
    if (r == 0 && c == 12) { a.act = A_OCT_UP;   return a; }

    // Row 1: steps 9-16 (plus the Fn generator layer), cursor, value, record.
    if (r == 1 && c >= 1 && c <= 8) {
        if (m.fn) {
            static const Act fnRow1[8] = {A_RND_DRUMS, A_RND_BASS, A_RND_MELODY, A_RND_SOUND,
                                          A_EUCLID, A_COPY, A_PASTE, A_CLEAR_PATTERN};
            a.act = fnRow1[c - 1];
            return a;
        }
        a.act = A_STEP; a.arg = (int8_t)(c + 7); return a;      // 8..15
    }
    // SHIFT is the pattern layer everywhere, so it also sizes the pattern.
    if (r == 1 && c == 9)  {
        if (m.shift) { a.act = A_PATLEN_DOWN; return a; }
        a.act = m.fn ? A_SAVE : A_CURSOR_PREV; return a;
    }
    if (r == 1 && c == 10) {
        if (m.shift) { a.act = A_PATLEN_UP; return a; }
        a.act = m.fn ? A_LOAD : A_CURSOR_NEXT; return a;
    }
    if (r == 1 && c == 11) {
        if (m.shift) { a.act = A_PATTERN_STEP; a.arg = -1; return a; }
        a.act = A_VALUE_DOWN; a.arg = m.fn ? 10 : 1; return a;
    }
    if (r == 1 && c == 12) {
        if (m.shift) { a.act = A_PATTERN_STEP; a.arg = 1; return a; }
        a.act = A_VALUE_UP;   a.arg = m.fn ? 10 : 1; return a;
    }

    // Row 2 command keys.
    if (r == 2 && c == 2)  { a.act = m.fn ? A_ARP_MODE : A_ARP_TOGGLE; return a; }
    if (r == 2 && c == 5)  { a.act = A_PRESET_PREV; a.arg = m.fn ? 10 : 1; return a; }
    if (r == 2 && c == 9)  { a.act = A_PRESET_NEXT; a.arg = m.fn ? 10 : 1; return a; }
    if (r == 2 && c == 12) { a.act = A_MUTE; return a; }

    // Fn + the four arrow-marked keys.
    if (m.fn && r == 2 && c == 11) { a.act = A_UP;    return a; }   // ;
    if (m.fn && r == 3 && c == 11) { a.act = A_DOWN;  return a; }   // .
    if (m.fn && r == 3 && c == 10) { a.act = A_LEFT;  return a; }   // ,
    if (m.fn && r == 3 && c == 12) { a.act = A_RIGHT; return a; }   // /

    return a;
}

// Ordered to match the Act enum exactly. The unit tests walk both and fail if
// they fall out of step.
static const char* const kActLabels[] = {
    "",                 // A_NONE
    "PLAY / STOP",      "RECORD",        "NEXT MODE",     "PREV MODE",
    "GO TO MODE",       "MENU",
    "BPM DOWN",         "BPM UP",        "OCTAVE DOWN",   "OCTAVE UP",
    "TAP TEMPO",
    "PREV PARAM",       "NEXT PARAM",    "VALUE DOWN",    "VALUE UP",
    "CONFIRM",          "BACK",          "CLEAR TRACK",
    "ARP ON/OFF",       "ARP MODE",      "PREV SOUND",    "NEXT SOUND",
    "MUTE",
    "UP",               "DOWN",          "LEFT",          "RIGHT",
    "STEP",             "PATTERN",       "PATTERN +/-",
    "SONG MODE",        "MANUAL",        "UNDO / REDO",
    "SHORTER",          "LONGER",
    "RANDOM DRUMS",     "RANDOM BASS",   "RANDOM LEAD",   "RANDOM SOUND",
    "EUCLID",
    "COPY",             "PASTE",         "CLEAR PATTERN", "SAVE",  "LOAD",
};

const char* actionLabel(Act a) {
    const int n = (int)(sizeof(kActLabels) / sizeof(kActLabels[0]));
    return ((int)a < n) ? kActLabels[a] : "";
}

const char* keyName(uint8_t id) {
    static const char* const kNames[56] = {
        "`","1","2","3","4","5","6","7","8","9","0","-","=","BKSP",
        "TAB","Q","W","E","R","T","Y","U","I","O","P","[","]","\\",
        "FN","SHIFT","A","S","D","F","G","H","J","K","L",";","'","ENTER",
        "CTRL","OPT","ALT","Z","X","C","V","B","N","M",",",".","/","SPACE",
    };
    return id < 56 ? kNames[id] : "?";
}

}  // namespace synth
