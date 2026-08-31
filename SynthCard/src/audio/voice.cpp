#include "voice.h"

namespace synth {

void Voice::init(uint32_t seed) {
    rng_.s = seed ? seed : 0x12345678u;
    ampEnv_.reset();
    filEnv_.reset();
    filter_.reset();
    filter2_.reset();
    memset(static_cast<void*>(&eng_), 0, sizeof(eng_));
    active_ = gate_ = false;
    cost_ = 1;
}

void Voice::startEngine(bool legato) {
    const Patch& pt = *patch_;
    switch (engId_) {
        case ENG_FM:    fmNoteOn(eng_.fm, pt, rng_); break;
        case ENG_WT:    wtNoteOn(eng_.wt, pt, curHz_ * kInvSampleRate, rng_); break;
        case ENG_PLUCK: pluckNoteOn(eng_.pluck, pt, curHz_, (float)vel_ * (1.0f / 127.0f), rng_); break;
        case ENG_ORGAN: organNoteOn(eng_.organ, pt, curHz_, rng_); break;
        case ENG_CHIP:  chipNoteOn(eng_.chip, pt, rng_); break;
        default:        analogNoteOn(eng_.analog, pt, rng_); break;
    }
    (void)legato;
}

void Voice::noteOn(uint8_t note, uint8_t vel, bool legato) {
    note_ = note;
    vel_  = vel ? vel : 1;
    const Patch& pt = *patch_;

    // Switching engine mid-voice would reinterpret the state union as another
    // engine's fields, so a changed engine always counts as a fresh note.
    const uint8_t newEng = pt.engine();
    const bool engChanged = (newEng != engId_);
    if (engChanged) {
        if (engId_ == ENG_PLUCK) pluckRelease(eng_.pluck);
        memset(static_cast<void*>(&eng_), 0, sizeof(eng_));
        engId_ = newEng;
    }
    cost_ = patchVoiceCost(pt);

    const float fine = pt.bip(P_FINE) * 0.5f;                 // +-50 cents
    targetHz_ = noteToHzFast((float)note_ + fine);

    const float glideMs = pt.norm(P_GLIDE) * pt.norm(P_GLIDE) * 1200.0f;
    glideCoef_ = glideMs < 1.0f ? 1.0f
               : 1.0f - expf(-4.0f / (glideMs * 0.001f * kSampleRate / kCtrlChunk));
    const bool fresh = !active_ || !legato || engChanged;
    if (fresh) curHz_ = targetHz_;

    const float va = pt.norm(P_VELO_AMT);
    velGain_ = (1.0f - va) + va * ((float)vel_ * (1.0f / 127.0f));

    ampEnv_.configure(envMsFor(pt.get(P_AMP_A)), envMsFor(pt.get(P_AMP_D)),
                      pt.norm(P_AMP_S), envMsFor(pt.get(P_AMP_R)));
    // The filter envelope is stepped once per control chunk, so it is
    // configured against the control rate rather than the sample rate.
    filEnv_.configure(envMsFor(pt.get(P_FEG_A)), envMsFor(pt.get(P_FEG_D)),
                      pt.norm(P_FEG_S), envMsFor(pt.get(P_FEG_R)),
                      kSampleRate / (float)kCtrlChunk);
    lfo_.configure(pt.get(P_LFO_WAVE), 0.05f + pt.norm(P_LFO_RATE) * pt.norm(P_LFO_RATE) * 30.0f);

    if (fresh) {
        startEngine(false);
        filter_.reset();
        filter2_.reset();
        if (pt.get(P_LFO_SYNC)) lfo_.retrigger();
        ampEnv_.gate(true);
        filEnv_.gate(true);
    } else if (pt.get(P_VOICE_MODE) != 2) {   // legato: retrigger only outside LEGATO mode
        startEngine(true);
        ampEnv_.gate(true);
        filEnv_.gate(true);
    }
    active_ = true;
    gate_   = true;
}

void Voice::noteOff() {
    if (!active_) return;
    gate_ = false;
    ampEnv_.gate(false);
    filEnv_.gate(false);
}

void Voice::kill() {
    if (engId_ == ENG_PLUCK) pluckRelease(eng_.pluck);
    active_ = gate_ = false;
    ampEnv_.reset();
    filEnv_.reset();
    filter_.reset();
    filter2_.reset();
}

void Voice::updateControl() {
    const Patch& pt = *patch_;
    lfoVal_ = lfo_.step() * pt.norm(P_LFO_AMT);
    const uint8_t dest = pt.get(P_LFO_DEST);

    curHz_ += (targetHz_ - curHz_) * glideCoef_;

    float hz = curHz_;
    if (dest == 0) hz *= fastExp2(lfoVal_ * (2.0f / 12.0f));   // +-2 semitones

    ctx_.pt   = &pt;
    ctx_.inc  = clampf(hz * kInvSampleRate, 0.0f, 0.49f);
    ctx_.lfo  = lfoVal_;
    ctx_.vel  = (float)vel_ * (1.0f / 127.0f);
    ctx_.note = note_;
    ctx_.rng  = &rng_;

    // Shape drives pulse width on ANALOG/CHIP and the wavetable morph on WT,
    // so one LFO destination covers "the knob that changes the waveform".
    const uint8_t shapeParam = (engId_ == ENG_WT) ? (uint8_t)PW_MORPH : (uint8_t)PA_PW;
    float shape = pt.norm(shapeParam);
    if (dest == 3) shape += lfoVal_ * 0.45f;
    ctx_.shape = (engId_ == ENG_WT) ? clampf(shape, 0.0f, 1.0f)
                                    : clampf(shape * 0.9f + 0.05f, 0.03f, 0.97f);

    ampLfo_ = (dest == 2) ? (1.0f - 0.5f * (lfoVal_ * 0.5f + 0.5f)) : 1.0f;

    const float fenv = filEnv_.process();
    const float keytrk = pt.norm(P_KEYTRK) * ((float)note_ - 60.0f) / 12.0f;
    const float velCut = pt.norm(P_VEL_CUT) * ((float)vel_ * (1.0f / 127.0f)) * 5.0f;
    const float oct = pt.norm(P_CUTOFF) * 10.5f + pt.bip(P_FEG_AMT) * fenv * 7.0f + keytrk
                    + velCut + (dest == 1 ? lfoVal_ * 4.0f : 0.0f);
    const float cut = clampf(20.0f * fastExp2(oct), 25.0f, kSampleRate * 0.45f);
    const float q = 0.55f + pt.norm(P_RESO) * pt.norm(P_RESO) * 9.0f;
    filter_.setCoeffs(cut, q);
    // Cascading two stages at full Q would double the resonant peak; the
    // second stage runs tamer so 24 dB sounds like a steeper filter rather
    // than a louder one.
    if (pt.get(P_FIL_POLES)) filter2_.setCoeffs(cut, 0.55f + (q - 0.55f) * 0.35f);
}

void Voice::renderEngine(float* buf, int n) {
    switch (engId_) {
        case ENG_FM:    fmRender(eng_.fm, ctx_, buf, n); break;
        case ENG_WT:    wtRender(eng_.wt, ctx_, buf, n); break;
        case ENG_PLUCK: pluckRender(eng_.pluck, ctx_, buf, n); break;
        case ENG_ORGAN: organRender(eng_.organ, ctx_, buf, n); break;
        case ENG_CHIP:  chipRender(eng_.chip, ctx_, buf, n); break;
        default:        analogRender(eng_.analog, ctx_, buf, n); break;
    }
}

void Voice::render(float* out, int n) {
    if (!active_ || !patch_) return;
    const Patch& pt = *patch_;
    const uint8_t ftype = pt.get(P_FIL_TYPE);
    const bool    fourPole = pt.get(P_FIL_POLES) != 0;
    const float   drv = pt.norm(P_DRIVE);
    const float   lvl = pt.norm(P_LEVEL) * velGain_;

    filter_.flush();
    filter2_.flush();

    float buf[kCtrlChunk];
    int done = 0;
    while (done < n) {
        updateControl();
        int m = n - done;
        if (m > kCtrlChunk) m = kCtrlChunk;
        renderEngine(buf, m);

        for (int i = 0; i < m; ++i) {
            float s = buf[i];
            if (drv > 0.001f) s = softClip(s * (1.0f + drv * 12.0f)) * (1.0f - drv * 0.45f);
            if (ftype) {
                s = filter_.process(s, ftype);
                if (fourPole) s = filter2_.process(s, ftype);
            }
            out[done + i] += s * ampEnv_.process() * lvl * ampLfo_ * 0.32f;
        }
        done += m;
    }
    if (ampEnv_.idle()) {
        if (engId_ == ENG_PLUCK) pluckRelease(eng_.pluck);
        active_ = false;
        gate_ = false;
    }
}

}  // namespace synth
