// SynthCard - metallic lanes: CL HAT, OP HAT, RIDE, CRASH, COWBELL.
//
// The old engine's comment promised "six detuned squares through a high-pass"
// and summed two of them under 0.85 of white noise, so the ear heard noise
// rather than metal. That is the single biggest reason the kit sounded cheap.
//
// This is the real thing: six square oscillators at the TR-808 cymbal
// frequency ratios, summed and then band-passed and high-passed, with noise
// demoted to a seasoning. Six mutually inharmonic squares beat against each
// other into a dense, metallic spectrum that noise cannot imitate - that is
// the difference between "psh" and "tsss".
#include "drum_voice.h"
#include "drums.h"

namespace synth {

// The 808's six cymbal oscillators are 205.3, 304.4, 369.6, 522.7, 540 and
// 800 Hz. Held as ratios so TUNE can move the whole bank while keeping the
// inharmonic relationships that make it sound like metal.
static const float kMetalRatio[kMetalOsc] =
    {1.0f, 1.4827f, 1.8003f, 2.5461f, 2.6303f, 3.8967f};

void metalTrigger(DV& d, const DrumHit& h, uint8_t lane) {
    const float velTilt = 0.6f + h.vel * 0.4f;

    float base, ms, bp, bpQ, hp, noise;
    switch (lane) {
        case DL_CHH:
            base  = 210.0f + h.tune * 260.0f;
            ms    = 12.0f + h.decay * 120.0f;
            bp    = 7000.0f + h.tone * 5000.0f; bpQ = 0.9f;
            hp    = 5500.0f + h.tone * 4500.0f;
            noise = 0.10f;
            break;
        case DL_OHH:
            base  = 210.0f + h.tune * 260.0f;
            ms    = 90.0f + h.decay * 1150.0f;
            bp    = 6500.0f + h.tone * 5000.0f; bpQ = 0.8f;
            hp    = 5000.0f + h.tone * 4200.0f;
            noise = 0.12f;
            break;
        case DL_RIDE:
            base  = 150.0f + h.tune * 170.0f;
            ms    = 320.0f + h.decay * 2000.0f;
            bp    = 3200.0f + h.tone * 4200.0f; bpQ = 1.6f;
            hp    = 2400.0f + h.tone * 2600.0f;
            noise = 0.07f;
            break;
        case DL_COWBELL:
            // Only two oscillators, the 540/800 Hz pair, band-passed tight.
            base  = 480.0f + h.tune * 420.0f;
            ms    = 130.0f + h.decay * 420.0f;
            bp    = 1900.0f + h.tone * 2200.0f; bpQ = 2.6f;
            hp    = 480.0f;
            noise = 0.0f;
            break;
        default:   // DL_CRASH
            base  = 130.0f + h.tune * 200.0f;
            ms    = 420.0f + h.decay * 3000.0f;
            bp    = 4200.0f + h.tone * 6000.0f; bpQ = 0.6f;
            hp    = 3000.0f + h.tone * 3600.0f;
            noise = 0.30f;
            break;
    }

    d.mcount = (lane == DL_COWBELL) ? 2 : kMetalOsc;
    for (int i = 0; i < kMetalOsc; ++i) {
        // The cowbell wants the 540/800 pair, which are ratios 4 and 5.
        const float r = (lane == DL_COWBELL) ? kMetalRatio[4 + i] / kMetalRatio[4]
                                             : kMetalRatio[i];
        d.minc[i] = clampf(base * r * kInvSampleRate, 0.0f, 0.49f);
        // A fixed phase scatter: identical phases would give one fat click on
        // every hit instead of a shimmer.
        d.mph[i]  = 0.137f * (float)i;
    }

    d.ampCoef   = decCoef(ms);
    d.noiseAmp  = noise * velTilt;
    d.noiseCoef = d.ampCoef;
    d.snap      = h.snap * velTilt * 0.8f;
    d.snapCoef  = decCoef(0.9f);
    d.drive     = 1.0f + h.drive * 5.0f;

    // A crash swells rather than starting at full tilt.
    if (lane == DL_CRASH) {
        d.atk    = 0.25f;
        d.atkInc = 1.0f / (0.012f * kSampleRate);
    } else {
        d.atk    = 1.0f;
        d.atkInc = 0.0f;
    }

    d.filt.reset();
    d.filt.setCoeffs(clampf(bp, 300.0f, 13800.0f), bpQ);
    d.filt2.reset();
    d.filt2.setCoeffs(clampf(hp, 200.0f, 13500.0f), 0.72f);
}

void metalRender(DV& d, uint8_t lane, float* out, int n) {
    const int   count = d.mcount ? d.mcount : kMetalOsc;
    const float oscGain = 1.0f / (float)count;
    const float drive = d.drive;
    const float comp = 1.0f / (1.0f + drive * 0.12f);
    const bool  bell = (lane == DL_COWBELL);

    for (int i = 0; i < n; ++i) {
        float metal = 0.0f;
        for (int o = 0; o < count; ++o) {
            d.mph[o] = wrap01(d.mph[o] + d.minc[o]);
            metal += (d.mph[o] < 0.5f ? 1.0f : -1.0f);
        }
        metal *= oscGain;

        if (d.noiseAmp > 0.0005f) {
            metal += d.rng.bipolar() * d.noiseAmp;
            d.noiseAmp *= d.noiseCoef;
        }
        if (drive > 1.001f) metal = softClip(metal * drive) * comp;

        // Band-pass then high-pass. The band-pass picks the metallic region,
        // the high-pass removes the low thud the square edges leave behind.
        float s = d.filt.process(metal, 3) * 1.8f;
        if (!bell) s = d.filt2.process(s, 2);

        if (d.snap > 0.0005f) {
            s += d.rng.bipolar() * d.snap * 0.9f;
            d.snap *= d.snapCoef;
        }

        if (d.atkInc > 0.0f && d.atk < 1.0f) {
            d.atk += d.atkInc;
            if (d.atk > 1.0f) d.atk = 1.0f;
        }

        out[i] = s * d.amp * d.atk * d.gain;
        d.amp *= d.ampCoef;
        ++d.t;
    }
}

}  // namespace synth
