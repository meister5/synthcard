// SynthCard - the real-time audio engine.
//
// Threading model:
//   * A dedicated FreeRTOS task pinned to core 0 renders 128-sample blocks and
//     hands them to M5.Speaker via a 3-buffer rotation.
//   * The UI runs on core 1 and never touches engine state directly - it posts
//     events into a lock-free single-producer ring that the audio task drains
//     at the top of every block.
//   * Nothing in the render path allocates.
#pragma once
#include "dsp.h"
#include "voice.h"
#include "drums.h"
#include "effects.h"
#include "../sequencer/sequencer.h"
#include "../music/music.h"

namespace synth {

constexpr int kMaxVoices  = 8;
constexpr int kScopeSize  = 120;

enum EvType : uint8_t {
    EV_NONE = 0, EV_NOTE_ON, EV_NOTE_OFF, EV_DRUM, EV_ALL_OFF, EV_PANIC,
    EV_PLAY, EV_STOP, EV_TOGGLE, EV_REC, EV_PATTERN, EV_SONGMODE, EV_ARP_ON,
    EV_ERASE_STEP,
};
struct Event { uint8_t type, a, b, c; };

class AudioEngine : public SeqSink, public ArpSink {
public:
    bool begin(Project* proj);
    Sequencer& seq() { return seq_; }
    Arp&       arp() { return arp_; }

    // ---- called from the UI thread (thread-safe, non-blocking) ------------
    void post(uint8_t type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0);
    // Bit 7 of `track` marks a note that should sound but not be recorded -
    // the upper voices of a chord, so a recorded chord keeps its root.
    static constexpr uint8_t kNoRecord = 0x80;
    void noteOn(uint8_t track, uint8_t note, uint8_t vel) { post(EV_NOTE_ON, track, note, vel); }
    void noteOff(uint8_t track, uint8_t note)             { post(EV_NOTE_OFF, track, note); }
    void eraseStep(uint8_t track)                         { post(EV_ERASE_STEP, track); }
    void drumHit(uint8_t lane, uint8_t vel)               { post(EV_DRUM, lane, vel); }
    void panic()                                          { post(EV_PANIC); }
    void setLiveTrack(uint8_t t) { liveTrack_ = t < kMelTracks ? t : 0; }
    uint8_t liveTrack() const { return liveTrack_; }
    // Pause/resume the render task around flash or SD access.
    void suspendAudio();
    void resumeAudio();

    // ---- metering (read-only, UI thread) ---------------------------------
    inline int   activeVoices() const { return activeVoices_; }
    inline int   maxVoices() const { return maxVoices_; }
    inline float cpuLoad() const { return cpuLoad_; }
    inline float peak() const { return fx_.peak(); }
    inline uint32_t underruns() const { return underruns_; }
    const int8_t* scope() const { return scope_; }

    // ---- audio thread ------------------------------------------------------
    void renderBlock(int16_t* out, int n);

    // SeqSink / ArpSink
    void seqNoteOn(uint8_t track, uint8_t note, uint8_t vel, bool slide) override;
    void seqNoteOff(uint8_t track, uint8_t note) override;
    void seqDrum(uint8_t lane, uint8_t vel) override;
    void arpNoteOn(uint8_t note, uint8_t vel) override;
    void arpNoteOff(uint8_t note) override;

private:
    void drainEvents();
    void handle(const Event& e);
    void startNote(uint8_t track, uint8_t note, uint8_t vel, bool slide);
    void stopNote(uint8_t track, uint8_t note);
    void killAll();
    int  allocVoice(uint8_t track, uint8_t note);
    void renderSegment(int offset, int n);
    void adaptLoad(uint32_t microsUsed, int samples);

    Project*  proj_ = nullptr;
    Sequencer seq_;
    Arp       arp_;
    Voice     voices_[kMaxVoices];
    uint8_t   vTrack_[kMaxVoices] = {0};
    uint8_t   vNote_[kMaxVoices]  = {0};
    DrumEngine drums_;
    Effects    fx_;

    float busMel_[kMelTracks][kBlockSize];
    float busDrum_[kBlockSize];
    float mix_[kBlockSize], sendDly_[kBlockSize], sendRev_[kBlockSize];

    static constexpr int kQueueSize = 96;
    volatile Event   queue_[kQueueSize];
    volatile uint16_t qHead_ = 0, qTail_ = 0;

    uint32_t ageCounter_ = 0;
    uint8_t  liveTrack_ = 0;
    int      activeVoices_ = 0, maxVoices_ = kMaxVoices;
    float    cpuLoad_ = 0.0f;
    uint32_t underruns_ = 0;
    int8_t   scope_[kScopeSize] = {0};
    int      scopePos_ = 0, scopeSkip_ = 0;
};

// Starts the speaker + render task. Returns false if the speaker refused.
bool audioStart(AudioEngine* engine);
void audioSetVolume(uint8_t v);
uint8_t audioGetVolume();

}  // namespace synth
