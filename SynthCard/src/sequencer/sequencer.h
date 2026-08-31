// SynthCard - clock, patterns, song arrangement and live recording.
//
// The sequencer is driven from the audio thread in sample counts, so step
// timing is sample-accurate rather than quantised to the render block.
#pragma once
#include <stdint.h>
#include <string.h>
#include "../audio/dsp.h"
#include "../audio/patch.h"
#include "../audio/drums.h"
#include "../audio/effects.h"
#include "../music/chords.h"

namespace synth {

constexpr int kMaxSteps     = 64;
constexpr int kPatternCount = 8;
constexpr int kMelTracks    = 2;      // 0 = LEAD, 1 = BASS
constexpr int kSongSlots    = 64;

enum StepFlag : uint8_t { SF_MUTE = 0x01, SF_SLIDE = 0x02, SF_ACCENT = 0x04 };

// Four bytes, not five. Gate needs five bits and chord needs two, so they
// share a byte: 64 steps x 2 tracks x 8 patterns makes that 1 KB off a
// project, and 2 KB once the undo buffer is counted.
struct Step {
    uint8_t note;    // 0 = rest, otherwise MIDI note number
    uint8_t vel;     // 1..127
    uint8_t gc;      // gate in bits 0-4, ChordType in bits 5-6
    uint8_t flags;   // SF_* in the low nibble, probability 0..8 in the high nibble

    // gate: 0..15  = (gate+1)/16 of one step, so 15 is exactly one step long.
    //       16..31 = whole steps, (gate-14) of them, i.e. 16 = 2 steps ... 31 = 17.
    // Live recording writes real held lengths into this, so a note you hold
    // plays back as long as you held it.
    inline uint8_t gate() const { return (uint8_t)(gc & 0x1F); }
    inline void setGate(int g) { gc = (uint8_t)((gc & 0xE0) | (clampi(g, 0, 31) & 0x1F)); }
    // chord: the step's root is expanded in the song's key at playback.
    inline uint8_t chord() const { return (uint8_t)((gc >> 5) & 0x03); }
    inline void setChord(int c) { gc = (uint8_t)((gc & 0x9F) | ((clampi(c, 0, 3) & 0x03) << 5)); }

    inline bool  on()   const { return note != 0 && !(flags & SF_MUTE); }
    inline uint8_t prob() const { return (uint8_t)(flags >> 4); }     // 0 = always
    inline void setProb(uint8_t p) { flags = (uint8_t)((flags & 0x0F) | ((p & 0x0F) << 4)); }
};
static_assert(sizeof(Step) == 4, "Step must stay four bytes");

struct Pattern {
    char    name[9];
    uint8_t length;                        // 1..kMaxSteps
    uint8_t muteMel;                       // bit per melodic track
    uint16_t muteDrum;                     // bit per drum lane
    Step    mel[kMelTracks][kMaxSteps];
    uint8_t drum[DL_COUNT][kMaxSteps];     // velocity, 0 = off
    void clear();
    void clearTrack(uint8_t track);        // 0,1 melodic; 2+ = drum lane (track-2)
};

struct SongSlot { uint8_t pattern, repeat; };
struct Song {
    uint8_t  length;
    SongSlot slot[kSongSlots];
    void clear();
};

struct Project {
    char       name[17];
    uint16_t   bpm;          // 40..300
    uint8_t    swing;        // 0..100 -> 50%..66% shuffle
    uint8_t    scale, root, octave;
    uint8_t    arpOn, arpMode, arpRate, arpOct, arpGate;
    Pattern    pat[kPatternCount];
    Song       song;
    Patch      patch[kMelTracks];
    DrumKit    kit;
    FxSettings fx;
    void reset();
};

// Everything the sequencer needs to make sound. Implemented by AudioEngine.
class SeqSink {
public:
    virtual ~SeqSink() {}
    virtual void seqNoteOn(uint8_t track, uint8_t note, uint8_t vel, bool slide) = 0;
    virtual void seqNoteOff(uint8_t track, uint8_t note) = 0;
    virtual void seqDrum(uint8_t lane, uint8_t vel) = 0;
    virtual void seqClick(bool accent) = 0;
};

enum MetroMode : uint8_t { METRO_OFF = 0, METRO_ON, METRO_REC, METRO_COUNT };
extern const char* const kMetroNames[METRO_COUNT];

class Sequencer {
public:
    void init(Project* proj, SeqSink* sink);

    void play();
    void stop();
    void toggle();
    inline bool playing() const { return playing_; }

    void setSongMode(bool on);
    inline bool songMode() const { return songMode_; }

    void selectPattern(uint8_t idx, bool immediate);
    inline uint8_t currentPattern() const { return curPat_; }
    inline uint8_t queuedPattern()  const { return queuedPat_; }
    inline uint8_t songPos()        const { return songPos_; }

    inline int  step()  const { return curStep_; }
    inline int  bar()   const { return curStep_ / 16; }
    // 0..1 progress through the current pattern, for the UI progress bar.
    float progress() const;

    // --- audio-thread interface -------------------------------------------
    int  samplesUntilNext() const;      // >= 1
    void advance(int samples);          // fires step/gate events when due

    // --- recording ---------------------------------------------------------
    // Chord type stamped onto steps by live recording (the keyboard's setting).
    void setRecordChord(uint8_t c) { recChord_ = c < (uint8_t)CHORD_COUNT ? c : (uint8_t)CHORD_OFF; }
    void setMetronome(uint8_t mode) { metro_ = mode < (uint8_t)METRO_COUNT ? mode : (uint8_t)METRO_OFF; }
    inline uint8_t metronome() const { return metro_; }
    void setRecording(bool on);
    inline bool recording() const { return recording_; }
    void recordNote(uint8_t track, uint8_t note, uint8_t vel);
    // Closes the note opened by recordNote and writes the held length as the
    // step's gate. Safe to call for a note that was never recorded.
    void recordNoteOff(uint8_t track, uint8_t note);
    void recordDrum(uint8_t lane, uint8_t vel);
    int  nearestStep() const;
    // Clears whatever sits under the playhead: track < kMelTracks is melodic,
    // otherwise drum lane (track - kMelTracks). Used by hold-to-erase.
    void eraseStep(uint8_t track);
    // Converts a held length in samples into a Step::gate value.
    uint8_t gateForHeld(uint32_t heldSamples) const;

    void allNotesOff();
    void refreshTiming();               // after a BPM / swing change

private:
    void fireStep();
    void releaseTrack(uint8_t track);
    int  stepSamples(int stepIndex) const;

    Project* proj_ = nullptr;
    SeqSink* sink_ = nullptr;

    bool     playing_ = false, recording_ = false, songMode_ = false;
    uint8_t  curPat_ = 0, queuedPat_ = 0xFF, songPos_ = 0, songRep_ = 0;
    int      curStep_ = 0;
    int      stepCountdown_ = 1;
    int      gateCountdown_[kMelTracks] = {0, 0};
    // Up to four notes can be sounding per track once a step carries a chord.
    uint8_t  soundingNote_[kMelTracks][4] = {{0}};
    uint8_t  soundingCount_[kMelTracks] = {0, 0};
    uint8_t  recChord_ = CHORD_OFF;
    int      curStepSamples_ = 1;
    uint32_t sampleClock_ = 0;
    uint8_t  metro_ = METRO_OFF;
    // One open recording per melodic track: sequencer tracks are monophonic,
    // so the most recent note is the one whose length we are measuring.
    uint8_t  recNote_[kMelTracks] = {0, 0};
    uint8_t  recStep_[kMelTracks] = {0, 0};
    uint32_t recStart_[kMelTracks] = {0, 0};
    Rng      rng_;
};

// Step length expressed in sixteenths of a step, so 16 is exactly one step
// and 32 is two. Keeps the gate encoding in one place.
inline int gateSixteenths(uint8_t gate) { return gate < 16 ? (gate + 1) : (gate - 14) * 16; }
constexpr uint8_t kGateMax = 31;
void formatGate(uint8_t gate, char* buf, int len);

// Pattern length of the pattern that is currently playing, in steps.
inline int patternLength(const Project& p, uint8_t idx) {
    int l = p.pat[idx % kPatternCount].length;
    return l < 1 ? 1 : (l > kMaxSteps ? kMaxSteps : l);
}

}  // namespace synth
