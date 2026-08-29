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

namespace synth {

constexpr int kMaxSteps     = 64;
constexpr int kPatternCount = 8;
constexpr int kMelTracks    = 2;      // 0 = LEAD, 1 = BASS
constexpr int kSongSlots    = 64;

enum StepFlag : uint8_t { SF_MUTE = 0x01, SF_SLIDE = 0x02, SF_ACCENT = 0x04 };

struct Step {
    uint8_t note;    // 0 = rest, otherwise MIDI note number
    uint8_t vel;     // 1..127
    uint8_t gate;    // 0..15, fraction of the step; 15 = tie into the next step
    uint8_t flags;   // SF_* in the low nibble, probability 0..8 in the high nibble
    inline bool  on()   const { return note != 0 && !(flags & SF_MUTE); }
    inline uint8_t prob() const { return (uint8_t)(flags >> 4); }     // 0 = always
    inline void setProb(uint8_t p) { flags = (uint8_t)((flags & 0x0F) | ((p & 0x0F) << 4)); }
};

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
};

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
    void setRecording(bool on) { recording_ = on; }
    inline bool recording() const { return recording_; }
    void recordNote(uint8_t track, uint8_t note, uint8_t vel);
    void recordDrum(uint8_t lane, uint8_t vel);
    int  nearestStep() const;

    void allNotesOff();
    void refreshTiming();               // after a BPM / swing change

private:
    void fireStep();
    int  stepSamples(int stepIndex) const;

    Project* proj_ = nullptr;
    SeqSink* sink_ = nullptr;

    bool     playing_ = false, recording_ = false, songMode_ = false;
    uint8_t  curPat_ = 0, queuedPat_ = 0xFF, songPos_ = 0, songRep_ = 0;
    int      curStep_ = 0;
    int      stepCountdown_ = 1;
    int      gateCountdown_[kMelTracks] = {0, 0};
    uint8_t  soundingNote_[kMelTracks] = {0, 0};
    int      curStepSamples_ = 1;
    Rng      rng_;
};

// Pattern length of the pattern that is currently playing, in steps.
inline int patternLength(const Project& p, uint8_t idx) {
    int l = p.pat[idx % kPatternCount].length;
    return l < 1 ? 1 : (l > kMaxSteps ? kMaxSteps : l);
}

}  // namespace synth
