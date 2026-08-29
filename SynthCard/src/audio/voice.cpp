#include "voice.h"

namespace synth {

// Control-rate chunk: filter/LFO coefficients are refreshed every 16 samples
// (0.5 ms). Fast enough that even a 20 ms filter envelope is smooth, cheap
// enough that tan() and friends cost almost nothing per sample.
static constexpr int kCtrlChunk = 16;

void Env::configure(float aMs, float dMs, float sus, float rMs, float rate) {
    sus_     = clampf(sus, 0.0f, 1.0f);
    atkInc_  = 1.0f / (clampf(aMs, 0.5f, 20000.0f) * 0.001f * rate);
    // -6.9 = ln(1000): the labelled time is the time to fall 60 dB, which is
    // what "decay" and "release" mean on a hardware synth.
    decCoef_ = clampf(1.0f - expf(-6.9f / (clampf(dMs, 1.0f, 20000.0f) * 0.001f * rate)), 0.0f, 1.0f);
    relCoef_ = clampf(1.0f - expf(-6.9f / (clampf(rMs, 1.0f, 20000.0f) * 0.001f * rate)), 0.0f, 1.0f);
}

void Env::gate(bool on) {
    if (on) { stage_ = E_ATK; }
    else if (stage_ != E_IDLE) { stage_ = E_REL; }
}

void SVF::setCoeffs(float cutoffHz, float q) {
    cutoffHz = clampf(cutoffHz, 20.0f, kSampleRate * 0.45f);
    // tan(pi*f/sr) via a 3-term series; accurate enough below 0.45*sr.
    float x = kPi * cutoffHz * kInvSampleRate;
    float x2 = x * x;
    float g = x * (1.0f + x2 * (0.3333333f + x2 * 0.1333333f));
    k_ = 1.0f / clampf(q, 0.5f, 12.0f);
    float a1 = 1.0f / (1.0f + g * (g + k_));
    a1_ = a1;
    a2_ = g * a1;
    a3_ = g * a2_;
}

float Lfo::step() {
    phase_ += inc_ * kCtrlChunk;
    bool wrapped = false;
    while (phase_ >= 1.0f) { phase_ -= 1.0f; wrapped = true; }
    switch (wave_) {
        case 1: return phase_ < 0.5f ? (phase_ * 4.0f - 1.0f) : (3.0f - phase_ * 4.0f);   // tri
        case 2: return phase_ < 0.5f ? 1.0f : -1.0f;                                      // square
        case 3: if (wrapped) sh_ = rng_.bipolar(); return sh_;                            // sample+hold
        case 4: return phase_ * 2.0f - 1.0f;                                              // saw
        default: return fastSin01(phase_);                                                // sine
    }
}

void Voice::init(uint32_t seed) {
    rng_.s = seed ? seed : 0x12345678u;
    ampEnv_.reset();
    filEnv_.reset();
    filter_.reset();
    active_ = gate_ = false;
}

void Voice::noteOn(uint8_t note, uint8_t vel, bool legato) {
    note_ = note;
    vel_  = vel ? vel : 1;
    const Patch& pt = *patch_;

    float fine = pt.bip(P_FINE) * 0.5f;                 // +-50 cents
    targetHz_ = noteToHz((float)note_ + fine);

    float glideMs = pt.norm(P_GLIDE) * pt.norm(P_GLIDE) * 1200.0f;
    glideCoef_ = glideMs < 1.0f ? 1.0f : 1.0f - expf(-4.0f / (glideMs * 0.001f * kSampleRate / kCtrlChunk));
    if (!active_ || !legato) curHz_ = targetHz_;

    float va = pt.norm(P_VELO_AMT);
    velGain_ = (1.0f - va) + va * ((float)vel_ * (1.0f / 127.0f));

    ampEnv_.configure(envMs(pt.get(P_AMP_A)), envMs(pt.get(P_AMP_D)), pt.norm(P_AMP_S), envMs(pt.get(P_AMP_R)));
    // The filter envelope is stepped once per control chunk, so it is
    // configured against the control rate rather than the sample rate.
    filEnv_.configure(envMs(pt.get(P_FEG_A)), envMs(pt.get(P_FEG_D)), pt.norm(P_FEG_S),
                      envMs(pt.get(P_FEG_R)), kSampleRate / (float)kCtrlChunk);
    lfo_.configure(pt.get(P_LFO_WAVE), 0.05f + pt.norm(P_LFO_RATE) * pt.norm(P_LFO_RATE) * 30.0f);

    if (!(legato && active_)) {
        // Start phases at a small random offset so stacked voices do not all
        // fire the same transient, and reset the filter to avoid a click.
        ph1_ = rng_.unipolar() * 0.02f;
        ph2_ = rng_.unipolar() * 0.02f;
        phSub_ = 0.0f;
        filter_.reset();
        if (pt.get(P_LFO_SYNC)) lfo_.retrigger();
        ampEnv_.gate(true);
        filEnv_.gate(true);
    } else if (pt.get(P_VOICE_MODE) != 2) {   // legato: retrigger only outside LEGATO mode
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
    active_ = gate_ = false;
    ampEnv_.reset();
    filEnv_.reset();
    filter_.reset();
}

void Voice::updateControl() {
    const Patch& pt = *patch_;
    lfoVal_ = lfo_.step() * pt.norm(P_LFO_AMT);
    uint8_t dest = pt.get(P_LFO_DEST);

    curHz_ += (targetHz_ - curHz_) * glideCoef_;

    float hz = curHz_;
    if (dest == 0) hz *= powf(2.0f, lfoVal_ * 0.5f / 12.0f * 4.0f);   // +-2 semitones max

    uint8_t eng = pt.get(P_ENGINE);
    float o2semi = (float)pt.get(P_O2_SEMI) - 24.0f;
    float o2cent = pt.bip(P_O2_DETUNE) * 0.5f;
    float hz2 = (eng == 1) ? hz * powf(2.0f, o2semi / 12.0f)
                           : hz * powf(2.0f, (o2semi + o2cent) / 12.0f);

    inc1_   = clampf(hz  * kInvSampleRate, 0.0f, 0.49f);
    inc2_   = clampf(hz2 * kInvSampleRate, 0.0f, 0.49f);
    incSub_ = clampf(hz * 0.5f * kInvSampleRate, 0.0f, 0.49f);

    pwm_ = clampf(pt.norm(P_PW) * 0.9f + 0.05f + (dest == 3 ? lfoVal_ * 0.4f : 0.0f), 0.03f, 0.97f);

    float fenv = filEnv_.process();
    float keytrk = pt.norm(P_KEYTRK) * ((float)note_ - 60.0f) / 12.0f;
    float oct = pt.norm(P_CUTOFF) * 10.5f + pt.bip(P_FEG_AMT) * fenv * 7.0f + keytrk
              + (dest == 1 ? lfoVal_ * 4.0f : 0.0f);
    cutoffBase_ = clampf(20.0f * powf(2.0f, oct), 25.0f, kSampleRate * 0.45f);
    filter_.setCoeffs(cutoffBase_, 0.55f + pt.norm(P_RESO) * pt.norm(P_RESO) * 9.0f);
}

void Voice::render(float* out, int n) {
    if (!active_ || !patch_) return;
    const Patch& pt = *patch_;
    const uint8_t eng   = pt.get(P_ENGINE);
    const uint8_t w1    = pt.get(P_O1_WAVE);
    const uint8_t w2    = pt.get(P_O2_WAVE);
    const uint8_t ftype = pt.get(P_FIL_TYPE);
    const uint8_t dest  = pt.get(P_LFO_DEST);
    const float l1  = pt.norm(P_O1_LEVEL);
    const float l2  = pt.norm(P_O2_LEVEL);
    const float lsb = pt.norm(P_SUB_LEVEL);
    const float lnz = pt.norm(P_NOISE);
    const float drv = pt.norm(P_DRIVE);
    const float lvl = pt.norm(P_LEVEL) * velGain_;
    const bool  subSine = pt.get(P_SUB_WAVE) != 0;

    int done = 0;
    while (done < n) {
        updateControl();
        int m = n - done; if (m > kCtrlChunk) m = kCtrlChunk;
        const float ampLfo = (dest == 2) ? (1.0f - 0.5f * (lfoVal_ * 0.5f + 0.5f)) : 1.0f;

        for (int i = 0; i < m; ++i) {
            float s;
            if (eng == 1) {
                // --- FM: sine carrier, sine modulator, index from O2 LEVEL.
                ph2_ = wrap01(ph2_ + inc2_);
                float mod = fastSin01(ph2_) * l2 * 4.0f;
                ph1_ = wrap01(ph1_ + inc1_);
                s = fastSin01(wrap01(ph1_ + mod)) * l1;
            } else if (eng == 2) {
                // --- Wavetable: morph saw -> square -> tri -> sine.
                ph1_ = wrap01(ph1_ + inc1_);
                float pos = pwm_ * 3.0f;
                int   ia  = (int)pos; if (ia > 2) ia = 2;
                float fr  = pos - (float)ia;
                float tbl[4];
                tbl[0] = 2.0f * ph1_ - 1.0f - polyBlep(ph1_, inc1_);
                tbl[1] = (ph1_ < 0.5f ? 1.0f : -1.0f) + polyBlep(ph1_, inc1_) - polyBlep(wrap01(ph1_ + 0.5f), inc1_);
                tbl[2] = ph1_ < 0.5f ? (4.0f * ph1_ - 1.0f) : (3.0f - 4.0f * ph1_);
                tbl[3] = fastSin01(ph1_);
                s = lerpf(tbl[ia], tbl[ia + 1], fr) * l1;
                ph2_ = wrap01(ph2_ + inc2_);
                s += (2.0f * ph2_ - 1.0f - polyBlep(ph2_, inc2_)) * l2;
            } else {
                // --- Subtractive / chip.
                ph1_ = wrap01(ph1_ + inc1_);
                float a;
                switch (w1) {
                    case 1: a = (ph1_ < 0.5f ? 1.0f : -1.0f) + polyBlep(ph1_, inc1_) - polyBlep(wrap01(ph1_ + 0.5f), inc1_); break;
                    case 2: a = (ph1_ < pwm_ ? 1.0f : -1.0f) + polyBlep(ph1_, inc1_) - polyBlep(wrap01(ph1_ + 1.0f - pwm_), inc1_); break;
                    case 3: a = ph1_ < 0.5f ? (4.0f * ph1_ - 1.0f) : (3.0f - 4.0f * ph1_); break;
                    case 4: a = fastSin01(ph1_); break;
                    case 5: a = rng_.bipolar(); break;
                    default: a = 2.0f * ph1_ - 1.0f - polyBlep(ph1_, inc1_); break;
                }
                s = a * l1;
                if (l2 > 0.001f) {
                    ph2_ = wrap01(ph2_ + inc2_);
                    float b;
                    switch (w2) {
                        case 1: b = (ph2_ < 0.5f ? 1.0f : -1.0f) + polyBlep(ph2_, inc2_) - polyBlep(wrap01(ph2_ + 0.5f), inc2_); break;
                        case 2: b = (ph2_ < pwm_ ? 1.0f : -1.0f); break;
                        case 3: b = ph2_ < 0.5f ? (4.0f * ph2_ - 1.0f) : (3.0f - 4.0f * ph2_); break;
                        case 4: b = fastSin01(ph2_); break;
                        case 5: b = rng_.bipolar(); break;
                        default: b = 2.0f * ph2_ - 1.0f - polyBlep(ph2_, inc2_); break;
                    }
                    s += b * l2;
                }
            }
            if (lsb > 0.001f) {
                phSub_ = wrap01(phSub_ + incSub_);
                s += (subSine ? fastSin01(phSub_) : (phSub_ < 0.5f ? 1.0f : -1.0f)) * lsb;
            }
            if (lnz > 0.001f) s += rng_.bipolar() * lnz;

            if (drv > 0.001f) s = softClip(s * (1.0f + drv * 12.0f)) * (1.0f - drv * 0.45f);
            if (ftype) s = filter_.process(s, ftype);
            if (eng == 3) {                                  // chip: 5-bit crush
                s = (float)((int)(s * 16.0f)) * (1.0f / 16.0f);
            }
            out[done + i] += s * ampEnv_.process() * lvl * ampLfo * 0.32f;
        }
        done += m;
    }
    if (ampEnv_.idle()) { active_ = false; gate_ = false; }
}

}  // namespace synth
