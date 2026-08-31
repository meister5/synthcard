// SynthCard - membrane drums: KICK and TOM.
//
// The kick is the lane that matters most and the one the old engine got most
// wrong. Two facts drive this design:
//
//  1. The Cardputer speaker has almost no output below ~200 Hz. A kick whose
//     weight lives in its fundamental is inaudible on the device however well
//     it is tuned. So the perceived weight has to come from harmonics inside
//     the band the speaker can actually move.
//  2. A single exponential pitch drop reads as a "boop". Real kicks drop very
//     fast at first and then settle, which is what the ear hears as a thump.
//
// Hence: a two-stage pitch envelope that sweeps the body down through the
// 400-100 Hz region, saturation to put harmonics there and keep them there, a
// separate click transient for attack definition, and a clean sub layer
// underneath for headphones and line-out.
#include "drum_voice.h"
#include "drums.h"

namespace synth {

void membraneTrigger(DV& d, const DrumHit& h, uint8_t lane) {
    const bool kick = (lane == DL_KICK);

    const float f0 = kick ? (34.0f + h.tune * 62.0f)          // 34 - 96 Hz
                          : (72.0f + h.tune * 250.0f);        // 72 - 322 Hz
    d.inc  = f0 * kInvSampleRate;
    d.ph   = 0.0f;
    d.ph2  = 0.0f;

    // Harder hits sweep from higher up and snap harder: velocity shaping the
    // pitch envelope is most of what makes programmed drums feel played.
    const float velTilt = 0.55f + h.vel * 0.45f;

    if (kick) {
        // Stage 1: down from ~8x f0 in about 6 ms. That sweep passes straight
        // through 400-150 Hz, which is the part you actually hear.
        d.pitch      = (5.0f + h.tone * 5.0f) * velTilt;
        d.pitchCoef  = decCoef(4.0f + h.tone * 5.0f);
        // Stage 2: a slower settle so the tail is not dead flat.
        d.pitch2     = 0.55f + h.tone * 0.5f;
        d.pitch2Coef = decCoef(30.0f + h.tone * 60.0f);
        d.ampCoef    = decCoef(45.0f + h.decay * 720.0f);
        // Click: 1.5-3 ms of high-passed noise. Attack definition on a tiny
        // transducer comes almost entirely from this.
        d.snap       = h.snap * velTilt;
        d.snapCoef   = decCoef(1.2f + h.snap * 2.2f);
        // Sub: the fundamental, no pitch envelope, longer tail. Inaudible on
        // the speaker by design - it is there for headphones and line-out.
        d.subAmp     = 0.45f;
        d.subCoef    = decCoef(70.0f + h.decay * 900.0f);
        d.subInc     = d.inc;
        d.subPh      = 0.0f;
        d.noiseAmp   = 0.0f;
        // Saturation is not optional here: it is what generates the audible
        // harmonics. Even at DRIVE 0 the body gets pushed a little.
        d.drive      = 1.6f + h.drive * 9.0f;
    } else {
        d.pitch      = (1.1f + h.tone * 2.4f) * velTilt;
        d.pitchCoef  = decCoef(28.0f + h.tone * 90.0f);
        d.pitch2     = 0.0f;
        d.pitch2Coef = 1.0f;
        d.ampCoef    = decCoef(85.0f + h.decay * 620.0f);
        d.snap       = h.snap * 0.6f * velTilt;
        d.snapCoef   = decCoef(2.0f + h.snap * 4.0f);
        d.subAmp     = 0.0f;
        d.subCoef    = 1.0f;
        // A tom keeps a little skin noise; it is not a pure sine.
        d.noiseAmp   = 0.12f + h.tone * 0.3f;
        d.noiseCoef  = decCoef(12.0f + h.decay * 40.0f);
        d.drive      = 1.0f + h.drive * 6.0f;
    }

    // The click rides a high-pass so it reads as a beater, not as a pop.
    d.filt.reset();
    d.filt.setCoeffs(clampf(1800.0f + h.tone * 5000.0f, 400.0f, 13000.0f), 0.8f);
}

void membraneRender(DV& d, uint8_t lane, float* out, int n) {
    const bool kick = (lane == DL_KICK);
    const float drive = d.drive;
    // Pulled back as drive rises so DRIVE changes tone, not just loudness.
    const float comp = 1.0f / (1.0f + drive * 0.16f);

    for (int i = 0; i < n; ++i) {
        const float bend = 1.0f + d.pitch + d.pitch2;
        d.ph = wrap01(d.ph + d.inc * bend);
        float body = fastSin01(d.ph);

        body = softClip(body * drive) * comp;

        float s = body * 1.05f;

        if (d.subAmp > 0.0005f) {
            d.subPh = wrap01(d.subPh + d.subInc);
            s += fastSin01(d.subPh) * d.subAmp;
            d.subAmp *= d.subCoef;
        }
        if (d.noiseAmp > 0.0005f) {
            s += d.rng.bipolar() * d.noiseAmp;
            d.noiseAmp *= d.noiseCoef;
        }
        if (d.snap > 0.0005f) {
            s += d.filt.process(d.rng.bipolar(), 2) * d.snap * (kick ? 1.5f : 1.1f);
            d.snap *= d.snapCoef;
        }

        d.pitch  *= d.pitchCoef;
        d.pitch2 *= d.pitch2Coef;

        out[i] = s * d.amp * d.gain;
        d.amp *= d.ampCoef;
        ++d.t;
    }
}

}  // namespace synth
