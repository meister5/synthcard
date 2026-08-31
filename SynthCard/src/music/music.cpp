#include "music.h"
#include <stdio.h>

namespace synth {

const char* const kNoteNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

const Scale kScales[] = {
    {"CHROMATIC", 12, {0,1,2,3,4,5,6,7,8,9,10,11}},
    {"MAJOR",      7, {0,2,4,5,7,9,11}},
    {"MINOR",      7, {0,2,3,5,7,8,10}},
    {"PENT MAJ",   5, {0,2,4,7,9}},
    {"PENT MIN",   5, {0,3,5,7,10}},
    {"BLUES",      6, {0,3,5,6,7,10}},
    {"DORIAN",     7, {0,2,3,5,7,9,10}},
    {"MIXOLYD",    7, {0,2,4,5,7,9,10}},
    {"HARM MIN",   7, {0,2,3,5,7,8,11}},
    {"PHRYGIAN",   7, {0,1,3,5,7,8,10}},
};
const uint8_t kScaleCount = sizeof(kScales) / sizeof(kScales[0]);

uint8_t scaleQuantize(int note, uint8_t root, uint8_t scaleIdx) {
    if (scaleIdx >= kScaleCount || scaleIdx == 0) return (uint8_t)clampi(note, 0, 127);
    const Scale& sc = kScales[scaleIdx];
    int rel = ((note - root) % 12 + 12) % 12;
    int best = sc.iv[0], bestD = 99;
    for (uint8_t i = 0; i < sc.n; ++i) {
        int d = sc.iv[i] - rel; if (d < 0) d = -d;
        int d2 = 12 - d;                      // wrap-around distance
        int dd = d < d2 ? d : d2;
        if (dd < bestD) { bestD = dd; best = sc.iv[i]; }
    }
    int base = note - rel;
    int cand = base + best;
    if (cand - note > 6)  cand -= 12;
    if (note - cand > 6)  cand += 12;
    return (uint8_t)clampi(cand, 0, 127);
}

uint8_t scaleDegree(int rootNote, int degree, uint8_t scaleIdx) {
    if (scaleIdx >= kScaleCount) scaleIdx = 0;
    const Scale& sc = kScales[scaleIdx];
    int n = sc.n ? sc.n : 12;
    int oct = degree / n;
    int idx = degree % n;
    if (idx < 0) { idx += n; --oct; }
    return (uint8_t)clampi(rootNote + oct * 12 + sc.iv[idx], 0, 127);
}

void noteName(uint8_t note, char* buf, int len) {
    if (len < 2) return;
    snprintf(buf, len, "%s%d", kNoteNames[note % 12], (int)(note / 12) - 1);
}

// -------------------------------------------------------------- chords -----
const char* const kChordNames[CHORD_COUNT] = {"OFF", "POWER", "TRIAD", "7TH"};

// Which scale degree a note sits on, so thirds can be stacked inside the key.
static int noteToDegree(uint8_t note, uint8_t rootPc, uint8_t scaleIdx) {
    for (int d = -24; d < 60; ++d)
        if (scaleDegree(rootPc, d, scaleIdx) >= note) return d;
    return 0;
}

int buildChord(uint8_t note, uint8_t type, uint8_t root, uint8_t scaleIdx, uint8_t out[4]) {
    int n = 0;
    out[n++] = note;
    switch (type) {
        case CHORD_POWER:
            out[n++] = (uint8_t)clampi(note + 7, 0, 127);
            out[n++] = (uint8_t)clampi(note + 12, 0, 127);
            break;
        case CHORD_TRIAD:
        case CHORD_SEVENTH:
            if (scaleIdx == 0) {                    // chromatic: plain major
                out[n++] = (uint8_t)clampi(note + 4, 0, 127);
                out[n++] = (uint8_t)clampi(note + 7, 0, 127);
                if (type == CHORD_SEVENTH) out[n++] = (uint8_t)clampi(note + 11, 0, 127);
            } else {
                const int d = noteToDegree(note, root, scaleIdx);
                out[n++] = scaleDegree(root, d + 2, scaleIdx);
                out[n++] = scaleDegree(root, d + 4, scaleIdx);
                if (type == CHORD_SEVENTH) out[n++] = scaleDegree(root, d + 6, scaleIdx);
            }
            break;
        default: break;
    }
    return n;
}

// ------------------------------------------------------------- arpeggiator --
const char* const kArpModeNames[ARP_MODE_COUNT] = {"UP", "DOWN", "UP/DN", "RANDOM", "ORDER"};
const char* const kArpRateNames[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
// Duration of one arp note expressed in 1/64th notes.
const uint16_t kArpRateDiv[6] = {16, 8, 5, 4, 3, 2};

void Arp::configure(uint8_t mode, uint8_t rate, uint8_t octaves, uint8_t gate, uint16_t bpm) {
    mode_    = mode < (uint8_t)ARP_MODE_COUNT ? mode : (uint8_t)ARP_UP;
    octaves_ = (uint8_t)clampi(octaves, 1, 4);
    gate_    = (uint8_t)clampi(gate, 1, 15);
    uint16_t div = kArpRateDiv[rate < 6 ? rate : 3];
    float sixtyFourth = 60.0f / (float)clampi(bpm, 20, 400) / 16.0f * kSampleRate;
    period_ = (int)(sixtyFourth * div);
    if (period_ < 64) period_ = 64;
}

void Arp::setEnabled(bool on) {
    if (on_ == on) return;
    on_ = on;
    if (!on && sounding_ && sink_) { sink_->arpNoteOff(sounding_); sounding_ = 0; }
    countdown_ = 1;
    cursor_ = 0; dir_ = 1;
}

void Arp::reset() {
    count_ = 0; cursor_ = 0; dir_ = 1; countdown_ = 1; gateCountdown_ = 0;
    if (sounding_ && sink_) sink_->arpNoteOff(sounding_);
    sounding_ = 0;
    rng_.s = 0x2545F491u;
}

void Arp::noteOn(uint8_t note, uint8_t vel) {
    for (uint8_t i = 0; i < count_; ++i) if (notes_[i] == note) return;
    if (count_ >= 8) return;
    // keep notes_ sorted ascending; order_ keeps the played order
    uint8_t pos = 0;
    while (pos < count_ && notes_[pos] < note) ++pos;
    for (uint8_t i = count_; i > pos; --i) notes_[i] = notes_[i - 1];
    notes_[pos] = note;
    order_[count_] = note;
    ++count_;
    vel_ = vel ? vel : 100;
    if (count_ == 1) { countdown_ = 1; cursor_ = 0; dir_ = 1; }
}

void Arp::noteOff(uint8_t note) {
    for (uint8_t i = 0; i < count_; ++i) {
        if (notes_[i] != note) continue;
        for (uint8_t j = i; j + 1 < count_; ++j) notes_[j] = notes_[j + 1];
        break;
    }
    for (uint8_t i = 0; i < count_; ++i) {
        if (order_[i] != note) continue;
        for (uint8_t j = i; j + 1 < count_; ++j) order_[j] = order_[j + 1];
        break;
    }
    if (count_) --count_;
    if (!count_) {
        if (sounding_ && sink_) { sink_->arpNoteOff(sounding_); sounding_ = 0; }
        gateCountdown_ = 0;
    }
}

int Arp::samplesUntilNext() const {
    if (!on_ || !count_) return 1 << 20;
    int m = countdown_ > 0 ? countdown_ : 1;
    if (gateCountdown_ > 0 && gateCountdown_ < m) m = gateCountdown_;
    return m < 1 ? 1 : m;
}

void Arp::advance(int samples) {
    if (!on_ || !count_ || !sink_) return;
    if (gateCountdown_ > 0) {
        gateCountdown_ -= samples;
        if (gateCountdown_ <= 0) {
            gateCountdown_ = 0;
            if (sounding_) { sink_->arpNoteOff(sounding_); sounding_ = 0; }
        }
    }
    countdown_ -= samples;
    if (countdown_ <= 0) {
        fire();
        countdown_ += period_;
        if (countdown_ <= 0) countdown_ = 1;
    }
}

void Arp::fire() {
    int span = count_ * octaves_;
    if (span <= 0) return;
    int idx;
    switch (mode_) {
        case ARP_DOWN:   idx = span - 1 - (cursor_ % span); ++cursor_; break;
        case ARP_UPDOWN: {
            idx = cursor_;
            cursor_ += dir_;
            if (span > 1) {
                if (cursor_ >= span) { cursor_ = span - 2; dir_ = -1; }
                else if (cursor_ < 0) { cursor_ = 1; dir_ = 1; }
            } else cursor_ = 0;
            if (idx < 0 || idx >= span) idx = 0;
            break;
        }
        case ARP_RANDOM: idx = (int)rng_.below((uint32_t)span); break;
        case ARP_ORDER:  idx = cursor_ % span; ++cursor_; break;
        default:         idx = cursor_ % span; ++cursor_; break;
    }
    int oct = idx / count_;
    int n   = idx % count_;
    uint8_t note = (mode_ == ARP_ORDER) ? order_[n] : notes_[n];
    note = (uint8_t)clampi(note + oct * 12, 0, 127);
    if (sounding_) sink_->arpNoteOff(sounding_);
    sink_->arpNoteOn(note, vel_);
    sounding_ = note;
    gateCountdown_ = (period_ * gate_) / 16;
    if (gateCountdown_ < 32) gateCountdown_ = 32;
}

// -------------------------------------------------------------- generators --
bool euclid(int i, int steps, int hits, int rotation) {
    if (steps <= 0 || hits <= 0) return false;
    if (hits >= steps) return true;
    int k = ((i - rotation) % steps + steps) % steps;
    return ((k * hits) % steps) < hits;
}

void applyEuclid(Pattern& p, uint8_t lane, int hits, int rotation, uint8_t vel) {
    if (lane >= DL_COUNT) return;
    int len = p.length ? p.length : 16;
    for (int i = 0; i < kMaxSteps; ++i)
        p.drum[lane][i] = (i < len && euclid(i, len, hits, rotation)) ? vel : 0;
}

void randomDrums(Pattern& p, Rng& rng, uint8_t density) {
    const int len = p.length ? p.length : 16;
    const float d = clampf(density * (1.0f / 100.0f), 0.05f, 1.0f);
    for (uint8_t l = 0; l < DL_COUNT; ++l) memset(p.drum[l], 0, kMaxSteps);

    // Kick: on the beat plus euclidean colour.
    int kickHits = clampi((int)(len * 0.25f * (0.7f + d)), 1, len);
    for (int i = 0; i < len; ++i)
        if ((i % 4 == 0 && rng.unipolar() < 0.85f) || euclid(i, len, kickHits, 0))
            p.drum[DL_KICK][i] = (uint8_t)(96 + rng.below(24));
    // Snare on the backbeat.
    for (int i = 2; i < len; i += 4)
        if (rng.unipolar() < 0.9f) p.drum[DL_SNARE][i] = (uint8_t)(92 + rng.below(28));
    // Hats.
    int hatEvery = (rng.unipolar() < 0.6f) ? 2 : 1;
    for (int i = 0; i < len; i += hatEvery)
        if (rng.unipolar() < 0.55f + d * 0.4f)
            p.drum[DL_CHH][i] = (uint8_t)((i % 4 == 0 ? 100 : 62) + rng.below(20));
    for (int i = 2; i < len; i += 4)
        if (rng.unipolar() < d * 0.35f) p.drum[DL_OHH][i] = (uint8_t)(80 + rng.below(24));
    // Sparse colour.
    for (int i = 0; i < len; ++i) {
        if (rng.unipolar() < d * 0.10f) p.drum[DL_CLAP][i] = (uint8_t)(70 + rng.below(30));
        if (rng.unipolar() < d * 0.08f) p.drum[DL_TOM][i]  = (uint8_t)(64 + rng.below(36));
        if (rng.unipolar() < d * 0.07f) p.drum[DL_RIM][i]  = (uint8_t)(60 + rng.below(30));
        if (rng.unipolar() < d * 0.05f) p.drum[DL_PERC][i] = (uint8_t)(60 + rng.below(40));
    }
    if (rng.unipolar() < 0.5f) p.drum[DL_CRASH][0] = 90;
}

static void fillMelodic(Pattern& p, uint8_t track, Rng& rng, uint8_t root, uint8_t scale,
                        uint8_t octave, bool bass) {
    const int len = p.length ? p.length : 16;
    const int baseNote = clampi(root + (int)octave * 12, 0, 108);
    p.clearTrack(track);
    int degree = 0;
    for (int i = 0; i < len; ++i) {
        Step& st = p.mel[track][i];
        float chance = bass ? (i % 4 == 0 ? 0.92f : 0.34f) : (i % 2 == 0 ? 0.62f : 0.36f);
        if (rng.unipolar() > chance) { st.note = 0; continue; }
        if (bass) {
            // Bass stays close to the root and mostly walks by small steps.
            int pick = (int)rng.below(4);
            degree = (i % 4 == 0) ? 0 : (pick == 0 ? 0 : (pick == 1 ? 2 : (pick == 2 ? 4 : -3)));
        } else {
            int move = (int)rng.below(5) - 2;
            degree = clampi(degree + move, -4, 9);
        }
        st.note  = scaleDegree(baseNote, degree, scale);
        st.vel   = (uint8_t)(bass ? 95 + rng.below(30) : 70 + rng.below(52));
        st.gate  = (uint8_t)(bass ? 6 + rng.below(6) : 4 + rng.below(9));
        st.flags = 0;
        if (!bass && rng.unipolar() < 0.12f) st.setProb(6);
        if (bass && rng.unipolar() < 0.15f) st.flags |= SF_SLIDE;
    }
}

void randomBass(Pattern& p, uint8_t track, Rng& rng, uint8_t root, uint8_t scale, uint8_t octave) {
    fillMelodic(p, track, rng, root, scale, octave, true);
}
void randomMelody(Pattern& p, uint8_t track, Rng& rng, uint8_t root, uint8_t scale, uint8_t octave) {
    fillMelodic(p, track, rng, root, scale, octave, false);
}

void randomizePatch(Patch& pt, Rng& rng, uint8_t amountPct) {
    // Controlled randomisation: only parameters that stay musical when moved,
    // and only by +-amount around their current value. The engine itself is
    // never rolled - the player picks the engine, the dice pick a sound
    // inside it.
    static const uint8_t kSafe[] = {
        P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,
        P_CUTOFF, P_RESO, P_FEG_AMT, P_FEG_D, P_FEG_S,
        P_LFO_WAVE, P_LFO_RATE, P_LFO_AMT, P_LFO_DEST,
        P_DRIVE, P_SEND_DLY, P_SEND_REV,
    };
    // Ranges that keep a patch playable no matter what the dice say.
    static const uint8_t kLo[] = {  0, 20,  0, 10,  40,  0, 40, 16,  0,  0,  4,  0, 0,  0,  0,  0 };
    static const uint8_t kHi[] = { 60,110,127, 90, 124,110,110, 90, 90,  4,110, 80, 3, 80, 80, 90 };
    const float amt = clampf(amountPct * 0.01f, 0.05f, 1.0f);

    for (unsigned i = 0; i < sizeof(kSafe) / sizeof(kSafe[0]); ++i) {
        const uint8_t id = kSafe[i];
        const int lo = kLo[i], hi = kHi[i];
        const int span = clampi((int)((hi - lo) * amt), 1, 127);
        const int v = pt.p[id] + (int)rng.below((uint32_t)(span * 2 + 1)) - span;
        pt.set(id, clampi(v, lo, hi));
    }

    // The engine's own parameters, over their declared ranges. Going through
    // engineRandomSlots() means a new engine is randomisable the moment its
    // table exists, with no second list to keep in step here.
    const uint8_t* slots = engineRandomSlots(pt.engine());
    for (int i = 0; slots[i] != 0xFF; ++i) {
        const uint8_t id = slots[i];
        const ParamInfo& in = paramInfo(pt.engine(), id);
        if (in.max == 0) continue;
        const int span = clampi((int)(in.max * amt), 1, 127);
        const int v = pt.p[id] + (int)rng.below((uint32_t)(span * 2 + 1)) - span;
        pt.set(id, clampi(v, 0, in.max));
    }

    strncpy(pt.name, "RANDOM", sizeof(pt.name) - 1);
    pt.name[sizeof(pt.name) - 1] = 0;
}

}  // namespace synth
