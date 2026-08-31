// SynthCard - noise lanes: CLAP and SHAKER.
//
// The old clap was `d.t % 460`, a two-level square gate that produced a buzz
// rather than a clap. A real clap is several hands arriving a few milliseconds
// apart: three tight bursts at 0, 10 and 20 ms, a fourth around 30 ms, and
// then a longer room tail. That spacing is the whole sound, and it is cheap -
// it is a table lookup, not extra DSP.
//
// The shaker is here rather than with the hats because what separates a shaker
// from a hat is its attack ramp: beads accelerate, they do not click.
#include "drum_voice.h"
#include "drums.h"

namespace synth {

// Burst onsets in milliseconds, and how long each burst lasts. The fourth is
// deliberately weaker and later - it is the straggler that stops the clap
// sounding mechanical.
static const float kClapBurstMs[4]  = {0.0f, 9.5f, 19.0f, 29.5f};
static const float kClapBurstGain[4] = {1.0f, 0.92f, 0.78f, 0.5f};
static constexpr float kBurstLenMs = 1.6f;

void noiseTrigger(DV& d, const DrumHit& h, uint8_t lane) {
    const float velTilt = 0.6f + h.vel * 0.4f;

    if (lane == DL_CLAP) {
        // The bursts are driven from d.t, so amp holds the tail envelope only.
        d.ampCoef   = decCoef(85.0f + h.decay * 420.0f);
        d.noiseAmp  = velTilt;
        d.noiseCoef = 1.0f;                      // gated by the burst table
        // Tail: the room, decaying more slowly than the bursts.
        d.noise2Amp  = (0.30f + h.tone * 0.35f) * velTilt;
        d.noise2Coef = decCoef(110.0f + h.decay * 520.0f);
        d.snap      = h.snap * velTilt * 0.7f;
        d.snapCoef  = decCoef(0.8f);
        d.atk = 1.0f; d.atkInc = 0.0f;
        d.filt.reset();
        d.filt.setCoeffs(clampf(900.0f + h.tone * 2600.0f + h.tune * 700.0f,
                                400.0f, 9000.0f), 1.7f);
        d.filt2.reset();
        d.filt2.setCoeffs(clampf(700.0f + h.tone * 1800.0f, 300.0f, 7000.0f), 1.1f);
    } else {   // DL_SHAKER
        d.ampCoef   = decCoef(28.0f + h.decay * 190.0f);
        d.noiseAmp  = velTilt;
        d.noiseCoef = 1.0f;
        d.noise2Amp = 0.0f;
        d.noise2Coef = 1.0f;
        d.snap      = 0.0f;
        d.snapCoef  = 1.0f;
        // The ramp: 2-7 ms of swell. Without it this is just a short hat.
        const float atkMs = 2.0f + (1.0f - h.snap) * 5.0f;
        d.atk    = 0.0f;
        d.atkInc = 1.0f / (atkMs * 0.001f * kSampleRate);
        d.filt.reset();
        d.filt.setCoeffs(clampf(4200.0f + h.tone * 6000.0f + h.tune * 2000.0f,
                                1500.0f, 13500.0f), 1.2f);
        d.filt2.reset();
        d.filt2.setCoeffs(clampf(3000.0f + h.tone * 4000.0f, 1000.0f, 12000.0f), 0.8f);
    }
    d.drive = 1.0f + h.drive * 5.0f;
    d.ph = 0.0f;
}

void noiseRender(DV& d, uint8_t lane, float* out, int n) {
    const float drive = d.drive;
    const float comp = 1.0f / (1.0f + drive * 0.12f);

    if (lane == DL_CLAP) {
        // Precomputed in samples so the inner loop is integer comparisons.
        const uint32_t burstAt[4] = {
            (uint32_t)(kClapBurstMs[0] * 0.001f * kSampleRate),
            (uint32_t)(kClapBurstMs[1] * 0.001f * kSampleRate),
            (uint32_t)(kClapBurstMs[2] * 0.001f * kSampleRate),
            (uint32_t)(kClapBurstMs[3] * 0.001f * kSampleRate),
        };
        const uint32_t burstLen = (uint32_t)(kBurstLenMs * 0.001f * kSampleRate);

        for (int i = 0; i < n; ++i) {
            float burst = 0.0f;
            for (int b = 0; b < 4; ++b) {
                if (d.t >= burstAt[b] && d.t < burstAt[b] + burstLen) {
                    // Each burst decays sharply across its own 1.6 ms.
                    const float f = 1.0f - (float)(d.t - burstAt[b]) / (float)burstLen;
                    burst += kClapBurstGain[b] * f * f;
                }
            }
            float s = 0.0f;
            if (burst > 0.0f)
                s += d.filt.process(d.rng.bipolar(), 3) * 2.3f * burst * d.noiseAmp;
            if (d.noise2Amp > 0.0005f) {
                s += d.filt2.process(d.rng.bipolar(), 3) * 1.5f * d.noise2Amp;
                d.noise2Amp *= d.noise2Coef;
            }
            if (d.snap > 0.0005f) {
                s += d.rng.bipolar() * d.snap;
                d.snap *= d.snapCoef;
            }
            if (drive > 1.001f) s = softClip(s * drive) * comp;

            out[i] = s * d.amp * d.gain;
            // The tail envelope only starts once the bursts are done, so a
            // long DECAY lengthens the room rather than smearing the claps.
            if (d.t > burstAt[3] + burstLen) d.amp *= d.ampCoef;
            ++d.t;
        }
    } else {
        for (int i = 0; i < n; ++i) {
            float s = d.filt.process(d.rng.bipolar(), 3) * 1.9f * d.noiseAmp;
            s = d.filt2.process(s, 2);
            if (drive > 1.001f) s = softClip(s * drive) * comp;

            if (d.atk < 1.0f) {
                d.atk += d.atkInc;
                if (d.atk > 1.0f) d.atk = 1.0f;
            }
            out[i] = s * d.amp * d.atk * d.gain;
            d.amp *= d.ampCoef;
            ++d.t;
        }
    }
}

}  // namespace synth
