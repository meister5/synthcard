// SynthCard - keyboard layer.
//
// The ADV's TCA8418 hands back exactly ONE FIFO entry per reader update, so a
// fast player outruns a once-per-frame poll. We drain the FIFO in a loop and
// diff the held-key set ourselves to get real note-on / note-off edges.
#pragma once
#include <stdint.h>

namespace synth {

// Physical key id = row * 14 + col, matching the M5Cardputer key matrix.
constexpr uint8_t kKeyCount = 56;
constexpr uint8_t KID(uint8_t row, uint8_t col) { return (uint8_t)(row * 14 + col); }

enum Act : uint8_t {
    A_NONE = 0,
    A_PLAY_STOP, A_RECORD, A_MODE_NEXT, A_MODE_PREV, A_MODE_JUMP, A_MENU,
    A_BPM_DOWN, A_BPM_UP, A_OCT_DOWN, A_OCT_UP, A_TAP_TEMPO,
    A_CURSOR_PREV, A_CURSOR_NEXT, A_VALUE_DOWN, A_VALUE_UP,
    A_CONFIRM, A_BACK, A_CLEAR_TRACK,
    A_ARP_TOGGLE, A_ARP_MODE, A_PRESET_PREV, A_PRESET_NEXT, A_MUTE,
    A_UP, A_DOWN, A_LEFT, A_RIGHT,
    A_STEP,                       // arg = 0..15
    A_PATTERN_SEL,                // arg = 0..7
    A_SONG_MODE, A_HELP,
    A_RND_DRUMS, A_RND_BASS, A_RND_MELODY, A_RND_SOUND, A_EUCLID,
    A_COPY, A_PASTE, A_CLEAR_PATTERN, A_SAVE, A_LOAD,
};

struct KeyEvent {
    uint8_t id;
    bool    pressed;
    bool    fn, shift, ctrl, alt;
};

struct Mods { bool fn = false, shift = false, ctrl = false, alt = false; };

struct Action { Act act = A_NONE; int8_t arg = 0; };

class Keys {
public:
    void begin();
    // Drains the controller FIFO and fills the event list for this frame.
    void update(uint32_t nowMs);

    int  eventCount() const { return evCount_; }
    const KeyEvent& event(int i) const { return ev_[i]; }
    const Mods& mods() const { return mods_; }
    bool held(uint8_t id) const { return id < kKeyCount && held_[id]; }
    // True if the controller looked wedged and we force-released everything.
    bool watchdogFired() const { return watchdog_; }

private:
    static constexpr int kMaxEvents = 24;
    KeyEvent ev_[kMaxEvents];
    int      evCount_ = 0;
    bool     held_[kKeyCount] = {false};
    uint32_t heldSince_[kKeyCount] = {0};
    Mods     mods_;
    bool     watchdog_ = false;
};

// -1 when the key is not part of the musical keyboard, otherwise the semitone
// offset above the current base note (0..16, i.e. C to E an octave up).
int8_t keyToSemitone(uint8_t id);
// Drum lane for a musical key (piano keys double as drum pads on the DRUM page).
int8_t keyToDrumLane(uint8_t id);
// Command layer.
Action mapAction(uint8_t id, const Mods& m);
// Human readable key name for the on-device help screen.
const char* keyName(uint8_t id);

}  // namespace synth
