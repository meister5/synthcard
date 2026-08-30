#include "sequencer.h"
#include <stdio.h>

namespace synth {

void Pattern::clear() {
    memset(name, 0, sizeof(name));
    strncpy(name, "PATTERN", sizeof(name) - 1);
    length   = 16;
    muteMel  = 0;
    muteDrum = 0;
    memset(mel, 0, sizeof(mel));
    memset(drum, 0, sizeof(drum));
    for (int t = 0; t < kMelTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s) { mel[t][s].vel = 100; mel[t][s].gate = 8; }
}

void Pattern::clearTrack(uint8_t track) {
    if (track < kMelTracks) {
        for (int s = 0; s < kMaxSteps; ++s) { mel[track][s] = Step(); mel[track][s].vel = 100; mel[track][s].gate = 8; }
    } else {
        uint8_t lane = (uint8_t)(track - kMelTracks);
        if (lane < DL_COUNT) memset(drum[lane], 0, kMaxSteps);
    }
}

void Song::clear() {
    length = 4;
    for (int i = 0; i < kSongSlots; ++i) { slot[i].pattern = 0; slot[i].repeat = 1; }
}

void Project::reset() {
    memset(name, 0, sizeof(name));
    strncpy(name, "UNTITLED", sizeof(name) - 1);
    bpm    = 120;
    swing  = 0;
    scale  = 0;      // chromatic
    root   = 0;      // C
    octave = 4;
    arpOn = 0; arpMode = 0; arpRate = 2; arpOct = 1; arpGate = 8;
    for (int i = 0; i < kPatternCount; ++i) {
        pat[i].clear();
        snprintf(pat[i].name, sizeof(pat[i].name), "PAT %02d", i + 1);
    }
    song.clear();
    loadPreset(patch[0], 0);    // BASIC LEAD
    loadPreset(patch[1], 5);    // SUB BASS
    loadKit(kit, 0);            // 808
    fx.reset();
}

// ---------------------------------------------------------------------------

void formatGate(uint8_t gate, char* buf, int len) {
    if (gate < 16) snprintf(buf, len, "%d/16", gate + 1);
    else           snprintf(buf, len, "%dSTP", gate - 14);
}

void Sequencer::init(Project* proj, SeqSink* sink) {
    proj_ = proj;
    sink_ = sink;
    curPat_ = 0; queuedPat_ = 0xFF; songPos_ = 0; songRep_ = 0;
    curStep_ = 0;
    playing_ = recording_ = songMode_ = false;
    rng_.s = 0x1F123BB5u;
    refreshTiming();
    stepCountdown_ = 1;
}

int Sequencer::stepSamples(int stepIndex) const {
    if (!proj_) return 1000;
    float bpm = (float)clampi(proj_->bpm, 20, 400);
    float base = 60.0f / bpm * 0.25f * kSampleRate;      // one 16th note
    float sw   = clampf(proj_->swing * (1.0f / 100.0f), 0.0f, 1.0f) * 0.33f;
    float f    = (stepIndex & 1) ? (1.0f - sw) : (1.0f + sw);
    int   n    = (int)(base * f);
    return n < 8 ? 8 : n;
}

void Sequencer::refreshTiming() { curStepSamples_ = stepSamples(curStep_); }

void Sequencer::play() {
    if (playing_) return;
    playing_ = true;
    curStep_ = 0;
    stepCountdown_ = 1;             // fire step 0 on the very next sample
    if (songMode_) { songPos_ = 0; songRep_ = 0; curPat_ = proj_->song.slot[0].pattern % kPatternCount; }
}

void Sequencer::stop() {
    playing_ = false;
    allNotesOff();
    curStep_ = 0;
    stepCountdown_ = 1;
}

void Sequencer::toggle() { if (playing_) stop(); else play(); }

void Sequencer::setSongMode(bool on) {
    songMode_ = on;
    if (on && proj_) { songPos_ = 0; songRep_ = 0; curPat_ = proj_->song.slot[0].pattern % kPatternCount; }
}

void Sequencer::selectPattern(uint8_t idx, bool immediate) {
    idx %= kPatternCount;
    if (immediate || !playing_) { curPat_ = idx; queuedPat_ = 0xFF; if (!playing_) curStep_ = 0; }
    else queuedPat_ = idx;
}

float Sequencer::progress() const {
    if (!proj_) return 0.0f;
    int len = patternLength(*proj_, curPat_);
    return (float)curStep_ / (float)len;
}

void Sequencer::allNotesOff() {
    for (int t = 0; t < kMelTracks; ++t) {
        if (soundingNote_[t]) { if (sink_) sink_->seqNoteOff((uint8_t)t, soundingNote_[t]); soundingNote_[t] = 0; }
        gateCountdown_[t] = 0;
    }
}

int Sequencer::samplesUntilNext() const {
    if (!playing_) return 1 << 20;
    int m = stepCountdown_ > 0 ? stepCountdown_ : 1;
    for (int t = 0; t < kMelTracks; ++t)
        if (gateCountdown_[t] > 0 && gateCountdown_[t] < m) m = gateCountdown_[t];
    return m < 1 ? 1 : m;
}

void Sequencer::advance(int samples) {
    if (!playing_ || !proj_ || !sink_) return;
    sampleClock_ += (uint32_t)samples;
    for (int t = 0; t < kMelTracks; ++t) {
        if (gateCountdown_[t] > 0) {
            gateCountdown_[t] -= samples;
            if (gateCountdown_[t] <= 0) {
                gateCountdown_[t] = 0;
                if (soundingNote_[t]) { sink_->seqNoteOff((uint8_t)t, soundingNote_[t]); soundingNote_[t] = 0; }
            }
        }
    }
    stepCountdown_ -= samples;
    if (stepCountdown_ <= 0) {
        fireStep();
        stepCountdown_ += curStepSamples_;
        if (stepCountdown_ <= 0) stepCountdown_ = 1;
    }
}

void Sequencer::fireStep() {
    Pattern& p = proj_->pat[curPat_];
    const int len = patternLength(*proj_, curPat_);
    const int s = curStep_ % len;
    curStepSamples_ = stepSamples(s);

    // --- melodic tracks
    for (int t = 0; t < kMelTracks; ++t) {
        if (p.muteMel & (1 << t)) continue;
        const Step& st = p.mel[t][s];
        if (!st.on()) continue;
        uint8_t pr = st.prob();
        if (pr && rng_.below(8) >= pr) continue;      // prob 8 = always, 1 = 1/8
        if (soundingNote_[t]) { sink_->seqNoteOff((uint8_t)t, soundingNote_[t]); soundingNote_[t] = 0; }
        uint8_t vel = st.vel ? st.vel : 100;
        if (st.flags & SF_ACCENT) vel = (uint8_t)clampi(vel + 30, 1, 127);
        sink_->seqNoteOn((uint8_t)t, st.note, vel, (st.flags & SF_SLIDE) != 0);
        soundingNote_[t] = st.note;
        int g = (st.gate < 16) ? (curStepSamples_ * (st.gate + 1)) / 16
                               : curStepSamples_ * (st.gate - 14);
        gateCountdown_[t] = g < 32 ? 32 : g;
    }

    // --- drums
    for (uint8_t l = 0; l < DL_COUNT; ++l) {
        if (p.muteDrum & (1u << l)) continue;
        uint8_t v = p.drum[l][s];
        if (v) sink_->seqDrum(l, v);
    }

    // --- advance position
    ++curStep_;
    if (curStep_ >= len) {
        curStep_ = 0;
        if (songMode_) {
            Song& sg = proj_->song;
            uint8_t slots = sg.length ? sg.length : 1;
            if (++songRep_ >= (sg.slot[songPos_].repeat ? sg.slot[songPos_].repeat : 1)) {
                songRep_ = 0;
                songPos_ = (uint8_t)((songPos_ + 1) % slots);
            }
            curPat_ = sg.slot[songPos_].pattern % kPatternCount;
        } else if (queuedPat_ != 0xFF) {
            curPat_ = queuedPat_;
            queuedPat_ = 0xFF;
        }
    }
}

int Sequencer::nearestStep() const {
    if (!proj_) return 0;
    const int len = patternLength(*proj_, curPat_);
    if (!playing_) return curStep_ % len;
    // stepCountdown_ counts down to the NEXT step; round to whichever is closer.
    int elapsed = curStepSamples_ - stepCountdown_;
    int s = curStep_ - 1;                    // curStep_ already points at the next one
    if (elapsed * 2 >= curStepSamples_) ++s;
    s %= len;
    if (s < 0) s += len;
    return s;
}

void Sequencer::setRecording(bool on) {
    recording_ = on;
    if (!on) for (int t = 0; t < kMelTracks; ++t) recNote_[t] = 0;
}

uint8_t Sequencer::gateForHeld(uint32_t heldSamples) const {
    const int len = curStepSamples_ > 0 ? curStepSamples_ : 1;
    // Length in sixteenths of a step, rounded to nearest.
    int sixteenths = (int)(((uint64_t)heldSamples * 16 + len / 2) / (uint32_t)len);
    if (sixteenths < 1) sixteenths = 1;
    if (sixteenths <= 16) return (uint8_t)(sixteenths - 1);
    int steps = (sixteenths + 8) / 16;              // round to whole steps
    return (uint8_t)clampi(14 + steps, 16, 31);
}

void Sequencer::recordNote(uint8_t track, uint8_t note, uint8_t vel) {
    if (!proj_ || track >= kMelTracks) return;
    const int s = nearestStep();
    Step& st = proj_->pat[curPat_].mel[track][s];
    st.note  = note;
    st.vel   = vel ? vel : 100;
    if (st.gate == 0) st.gate = 8;
    st.flags = (uint8_t)(st.flags & 0xF0);
    recNote_[track]  = note;
    recStep_[track]  = (uint8_t)s;
    recStart_[track] = sampleClock_;
}

void Sequencer::recordNoteOff(uint8_t track, uint8_t note) {
    if (!proj_ || track >= kMelTracks) return;
    if (recNote_[track] != note || note == 0) return;
    recNote_[track] = 0;
    Step& st = proj_->pat[curPat_].mel[track][recStep_[track] % kMaxSteps];
    if (st.note != note) return;                    // overwritten in the meantime
    st.gate = gateForHeld(sampleClock_ - recStart_[track]);
}

void Sequencer::eraseStep(uint8_t track) {
    if (!proj_ || !playing_) return;
    Pattern& p = proj_->pat[curPat_];
    const int s = nearestStep();
    if (track < kMelTracks) {
        p.mel[track][s].note = 0;
        p.mel[track][s].flags = (uint8_t)(p.mel[track][s].flags & 0xF0);
        if (recNote_[track]) recNote_[track] = 0;
    } else {
        uint8_t lane = (uint8_t)(track - kMelTracks);
        if (lane < DL_COUNT) p.drum[lane][s] = 0;
    }
}

void Sequencer::recordDrum(uint8_t lane, uint8_t vel) {
    if (!proj_ || lane >= DL_COUNT) return;
    proj_->pat[curPat_].drum[lane][nearestStep()] = vel ? vel : 100;
}

}  // namespace synth
