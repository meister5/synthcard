// SynthCard - PERC: the catch-all lane.
//
// A two-operator FM blip. It is deliberately the one lane with no fixed
// character: at low TONE it is a wood block, at high TONE and long DECAY it
// is a metallic bleep, and with SNAP up it is a rimshot-ish tick. One knob
// range covers most of the odd percussion a pattern needs.
#include "drum_voice.h"
#include "drums.h"

namespace synth {

void percTrigger(DV& d, const DrumHit& h, uint8_t lane) {
    (void)lane;
    const float velTilt = 0.55f + h.vel * 0.45f;

    const float f0 = 260.0f + h.tune * 1900.0f;
    d.inc  = f0 * kInvSampleRate;
    // A non-integer modulator ratio is what keeps this inharmonic and
    // percussive rather than pitched.
    d.inc2 = d.inc * (1.0f + h.tone * 5.4f);
    d.ph = 0.0f;
    d.ph2 = 0.0f;

    d.ampCoef   = decCoef(18.0f + h.decay * 420.0f);
    // Modulation index falls fast: bright at the attack, pure at the tail.
    d.pitch     = (1.2f + h.tone * 5.0f) * velTilt;
    d.pitchCoef = decCoef(12.0f + h.decay * 70.0f);
    d.pitch2 = 0.0f; d.pitch2Coef = 1.0f;

    d.snap      = h.snap * velTilt;
    d.snapCoef  = decCoef(1.0f);
    d.noiseAmp  = 0.0f;
    d.drive     = 1.0f + h.drive * 6.0f;

    d.filt.reset();
    d.filt.setCoeffs(clampf(2000.0f + h.tone * 6000.0f, 500.0f, 13000.0f), 0.9f);
}

void percRender(DV& d, uint8_t lane, float* out, int n) {
    (void)lane;
    const float drive = d.drive;
    const float comp = 1.0f / (1.0f + drive * 0.14f);

    for (int i = 0; i < n; ++i) {
        d.ph2 = wrap01(d.ph2 + d.inc2);
        const float mod = fastSin01(d.ph2) * d.pitch;
        d.ph = wrap01(d.ph + d.inc);
        float s = fastSin01(wrap01(d.ph + mod));
        if (drive > 1.001f) s = softClip(s * drive) * comp;

        if (d.snap > 0.0005f) {
            s += d.filt.process(d.rng.bipolar(), 2) * d.snap * 1.2f;
            d.snap *= d.snapCoef;
        }
        d.pitch *= d.pitchCoef;

        out[i] = s * d.amp * d.gain;
        d.amp *= d.ampCoef;
        ++d.t;
    }
}

}  // namespace synth
