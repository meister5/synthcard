// SynthCard - musical helpers: scales, arpeggiator, euclidean rhythms and
// the "controlled randomisation" generators.
#pragma once
#include <stdint.h>
#include "../audio/dsp.h"
#include "../sequencer/sequencer.h"

namespace synth {

// ------------------------------------------------------------------ scales --
struct Scale { const char* name; uint8_t n; uint8_t iv[12]; };
extern const Scale kScales[];
extern const uint8_t kScaleCount;
extern const char* const kNoteNames[12];

// Snaps a MIDI note to the nearest tone of the given scale/root.
uint8_t scaleQuantize(int note, uint8_t root, uint8_t scaleIdx);
// The n-th degree of a scale above the root note (n may be negative).
uint8_t scaleDegree(int rootNote, int degree, uint8_t scaleIdx);
void    noteName(uint8_t note, char* buf, int len);   // "C#4"

// ------------------------------------------------------------- arpeggiator --
enum ArpMode : uint8_t { ARP_UP = 0, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM, ARP_ORDER, ARP_MODE_COUNT };
extern const char* const kArpModeNames[ARP_MODE_COUNT];
extern const char* const kArpRateNames[6];
extern const uint16_t    kArpRateDiv[6];   // steps-per-note x 4 (i.e. in 1/64ths)

class ArpSink {
public:
    virtual ~ArpSink() {}
    virtual void arpNoteOn(uint8_t note, uint8_t vel) = 0;
    virtual void arpNoteOff(uint8_t note) = 0;
};

class Arp {
public:
    void init(ArpSink* sink) { sink_ = sink; reset(); }
    void configure(uint8_t mode, uint8_t rate, uint8_t octaves, uint8_t gate, uint16_t bpm);
    void setEnabled(bool on);
    inline bool enabled() const { return on_; }
    void noteOn(uint8_t note, uint8_t vel);
    void noteOff(uint8_t note);
    void reset();
    inline bool holding() const { return count_ > 0; }
    int  samplesUntilNext() const;
    void advance(int samples);

private:
    void fire();
    ArpSink* sink_ = nullptr;
    bool     on_ = false;
    uint8_t  notes_[8] = {0}, order_[8] = {0}, count_ = 0, vel_ = 100;
    uint8_t  mode_ = ARP_UP, octaves_ = 1, gate_ = 8;
    int      period_ = 8000, countdown_ = 1, gateCountdown_ = 0;
    uint8_t  sounding_ = 0;
    int      cursor_ = 0, dir_ = 1;
    Rng      rng_;
};

// ------------------------------------------------------------- generators --
// Euclidean rhythm: true when step i of `steps` is one of `hits`, rotated.
bool euclid(int i, int steps, int hits, int rotation);
void applyEuclid(Pattern& p, uint8_t lane, int hits, int rotation, uint8_t vel);

void randomDrums(Pattern& p, Rng& rng, uint8_t density);
void randomBass(Pattern& p, uint8_t track, Rng& rng, uint8_t root, uint8_t scale, uint8_t octave);
void randomMelody(Pattern& p, uint8_t track, Rng& rng, uint8_t root, uint8_t scale, uint8_t octave);
void randomizePatch(Patch& pt, Rng& rng, uint8_t amountPct);

}  // namespace synth
