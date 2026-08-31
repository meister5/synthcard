// SynthCard - SNARE and RIM.
//
// The old snare had one high-passed noise band, which is why it read as a
// hiss with a beep under it. A real snare is three things at once: two tuned
// shell modes, a mid-band "shell" noise, and a bright "wires" noise that
// outlasts the body. Giving the two noise bands independent decays is what
// makes TONE sweep from a thick tom-like hit to a tight crack.
#include "drum_voice.h"
#include "drums.h"

namespace synth {

void snareTrigger(DV& d, const DrumHit& h, uint8_t lane) {
    const bool rim = (lane == DL_RIM);
    const float velTilt = 0.55f + h.vel * 0.45f;

    if (rim) {
        const float f0 = 380.0f + h.tune * 1500.0f;
        d.inc  = f0 * kInvSampleRate;
        d.inc2 = d.inc * 1.63f;                 // the classic rimshot ratio
        d.ampCoef   = decCoef(5.0f + h.decay * 55.0f);
        d.noiseAmp  = (0.4f + h.tone * 0.5f) * velTilt;
        d.noiseCoef = d.ampCoef;
        d.noise2Amp = 0.0f;
        d.noise2Coef = 1.0f;
        d.snap      = h.snap * velTilt;
        d.snapCoef  = decCoef(0.8f);
        d.pitch = 0.0f; d.pitchCoef = 1.0f;
        d.drive = 1.0f + h.drive * 5.0f;
        d.filt.reset();
        d.filt.setCoeffs(clampf(1700.0f + h.tone * 6200.0f, 400.0f, 13000.0f), 2.2f);
        d.filt2.reset();
        d.filt2.setCoeffs(3000.0f, 0.9f);
    } else {
        const float f0 = 148.0f + h.tune * 215.0f;
        d.inc  = f0 * kInvSampleRate;
        d.inc2 = d.inc * 1.48f;                 // second shell mode
        // A short pitch drop on the shell: snares are struck, not bowed.
        d.pitch     = 0.9f * velTilt;
        d.pitchCoef = decCoef(6.0f);
        d.ampCoef   = decCoef(28.0f + h.decay * 260.0f);

        // Shell band: mid noise, dies with the body.
        d.noiseAmp  = (0.5f + (1.0f - h.tone) * 0.5f) * velTilt;
        d.noiseCoef = decCoef(24.0f + h.decay * 170.0f);
        // Wires band: bright, outlasts everything. This is the band that
        // makes a snare sound like a snare.
        d.noise2Amp  = (0.45f + h.tone * 0.85f) * velTilt;
        d.noise2Coef = decCoef(45.0f + h.decay * 380.0f);

        d.snap      = h.snap * velTilt * 1.2f;
        d.snapCoef  = decCoef(1.0f);
        d.drive = 1.0f + h.drive * 7.0f;

        d.filt.reset();
        // Shell noise: band-passed low, around the drum's own register.
        d.filt.setCoeffs(clampf(f0 * 1.5f, 150.0f, 1200.0f), 1.5f);
        d.filt2.reset();
        // Wires: high-passed, brightened by TONE.
        d.filt2.setCoeffs(clampf(2600.0f + h.tone * 5200.0f, 1200.0f, 13500.0f), 0.8f);
    }
    d.ph = 0.0f;
    d.ph2 = 0.0f;
}

void snareRender(DV& d, uint8_t lane, float* out, int n) {
    const bool rim = (lane == DL_RIM);
    const float drive = d.drive;
    const float comp = 1.0f / (1.0f + drive * 0.15f);

    for (int i = 0; i < n; ++i) {
        float s;
        if (rim) {
            d.ph  = wrap01(d.ph  + d.inc);
            d.ph2 = wrap01(d.ph2 + d.inc2);
            float body = (d.ph < 0.5f ? 0.7f : -0.7f) + (d.ph2 < 0.5f ? 0.42f : -0.42f);
            body = d.filt.process(body + d.rng.bipolar() * d.noiseAmp, 3) * 1.6f;
            s = body;
            d.noiseAmp *= d.noiseCoef;
        } else {
            const float bend = 1.0f + d.pitch;
            d.ph  = wrap01(d.ph  + d.inc  * bend);
            d.ph2 = wrap01(d.ph2 + d.inc2 * bend);
            float body = fastSin01(d.ph) * 0.62f + fastSin01(d.ph2) * 0.34f;

            const float shell = d.filt.process(d.rng.bipolar(), 3) * d.noiseAmp * 1.6f;
            const float wires = d.filt2.process(d.rng.bipolar(), 2) * d.noise2Amp;

            s = softClip((body + shell) * drive) * comp + wires;

            d.pitch     *= d.pitchCoef;
            d.noiseAmp  *= d.noiseCoef;
            d.noise2Amp *= d.noise2Coef;
        }

        if (d.snap > 0.0005f) {
            // 1 ms transient: the stick hitting the head, not part of either
            // noise band.
            s += d.rng.bipolar() * d.snap * 1.3f;
            d.snap *= d.snapCoef;
        }

        out[i] = s * d.amp * d.gain;
        d.amp *= d.ampCoef;
        ++d.t;
    }
}

}  // namespace synth
