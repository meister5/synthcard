#include "drums.h"

namespace synth {

const char* const kDrumNames[DL_COUNT] =
    {"KICK", "SNARE", "CL HAT", "OP HAT", "CLAP", "TOM", "RIM", "CRASH", "PERC"};
const char* const kDrumShort[DL_COUNT] =
    {"KIK", "SNR", "CHH", "OHH", "CLP", "TOM", "RIM", "CRS", "PRC"};
const char* const kDrumParamNames[DP_COUNT] = {"TUNE", "DECAY", "TONE", "LEVEL"};

// tune, decay, tone, level
#define K(a,b,c,d) {a,b,c,d}
const KitPreset kKits[] = {
  {"808", {K(40,86,30,120),K(56,52,54,100),K(64,14,96,74),K(64,54,96,70),K(64,44,70,88),
           K(52,60,40,84),K(80,12,80,70),K(70,110,88,64),K(76,26,64,70)}, 18, 26},
  {"909", {K(52,70,64,124),K(64,46,74,108),K(70,10,110,80),K(70,44,108,74),K(60,40,76,92),
           K(58,52,52,86),K(88,10,92,72),K(74,104,100,68),K(84,22,80,72)}, 14, 22},
  {"ELECTRO", {K(60,60,86,118),K(72,40,96,102),K(84,8,120,78),K(84,34,118,70),K(70,34,90,86),
               K(66,44,72,82),K(96,8,110,74),K(86,88,116,62),K(96,18,104,76)}, 30, 34},
  {"MINIMAL", {K(44,64,20,112),K(50,34,40,88),K(60,8,88,64),K(60,28,84,56),K(56,28,58,72),
               K(48,44,30,74),K(72,8,66,58),K(64,72,70,48),K(68,16,52,60)}, 10, 18},
  {"INDUSTRIAL", {K(34,96,110,127),K(44,64,110,112),K(76,16,120,86),K(76,58,118,80),K(52,54,104,96),
                  K(40,74,96,94),K(92,14,120,84),K(80,120,124,76),K(60,40,116,88)}, 40, 54},
  {"LOFI", {K(48,70,12,108),K(52,44,26,92),K(48,12,58,62),K(48,38,54,58),K(50,36,44,76),
            K(50,54,22,78),K(60,10,48,56),K(56,84,52,50),K(58,24,36,64)}, 34, 62},
  {"EXPERIMENT", {K(80,50,100,110),K(96,70,88,96),K(110,20,124,72),K(104,66,120,68),K(88,60,110,90),
                  K(100,40,104,86),K(116,24,116,78),K(102,110,120,70),K(120,50,120,84)}, 62, 70},
};
#undef K
const uint8_t kKitCount = sizeof(kKits) / sizeof(kKits[0]);

void DrumKit::reset() { loadKit(*this, 0); }

void loadKit(DrumKit& dst, uint8_t index) {
    if (index >= kKitCount) index = 0;
    const KitPreset& k = kKits[index];
    for (uint8_t l = 0; l < DL_COUNT; ++l)
        for (uint8_t p = 0; p < DP_COUNT; ++p) dst.p[l][p] = k.p[l][p];
    dst.sendDly = k.sendDly;
    dst.sendRev = k.sendRev;
    strncpy(dst.name, k.name, sizeof(dst.name) - 1);
    dst.name[sizeof(dst.name) - 1] = 0;
}

void DrumEngine::init() {
    for (uint8_t i = 0; i < DL_COUNT; ++i) {
        v_[i] = DV();
        v_[i].rng.s = 0xB5297A4Du + i * 0x68E31DA4u;
        v_[i].filt.reset();
    }
}

void DrumEngine::allOff() { for (auto& d : v_) { d.active = false; d.amp = 0.0f; } }

// Per-sample decay factor such that the level falls 60 dB in `ms`, so the
// numbers in the kit tables are real decay times.
static inline float decCoef(float ms) { return expf(-6.9f / (ms * 0.001f * kSampleRate)); }

void DrumEngine::trigger(uint8_t lane, uint8_t vel) {
    if (lane >= DL_COUNT || !kit_ || vel == 0) return;
    DV& d = v_[lane];
    const float tune  = kit_->get(lane, DP_TUNE) * (1.0f / 127.0f);
    const float dec   = kit_->get(lane, DP_DECAY) * (1.0f / 127.0f);
    const float tone  = kit_->get(lane, DP_TONE) * (1.0f / 127.0f);
    const float level = kit_->get(lane, DP_LEVEL) * (1.0f / 127.0f);

    d.active = true;
    d.t = 0;
    d.ph = 0.0f; d.ph2 = 0.0f;
    d.amp = 1.0f;
    d.noiseAmp = 1.0f;
    d.gain = level * ((float)vel * (1.0f / 127.0f));
    d.filt.reset();

    switch (lane) {
        case DL_KICK:
            d.inc = (30.0f + tune * 60.0f) * kInvSampleRate;
            d.ampCoef   = decCoef(40.0f + dec * 700.0f);
            d.pitch     = 5.0f + tone * 9.0f;
            d.pitchCoef = decCoef(12.0f + tone * 40.0f);
            d.noiseAmp  = tone * 0.5f;
            d.noiseCoef = decCoef(2.0f + tone * 6.0f);
            break;
        case DL_SNARE:
            d.inc  = (140.0f + tune * 220.0f) * kInvSampleRate;
            d.inc2 = d.inc * 1.48f;
            d.ampCoef   = decCoef(30.0f + dec * 300.0f);
            d.noiseCoef = decCoef(40.0f + dec * 320.0f);
            d.noiseAmp  = 0.6f + tone * 0.7f;
            d.pitch = 1.6f; d.pitchCoef = decCoef(8.0f);
            d.filt.setCoeffs(600.0f + tone * 5000.0f, 0.8f);
            break;
        case DL_CHH:
        case DL_OHH: {
            float ms = (lane == DL_CHH) ? (14.0f + dec * 130.0f) : (80.0f + dec * 1200.0f);
            d.ampCoef   = decCoef(ms);
            d.noiseCoef = d.ampCoef;
            d.noiseAmp  = 1.0f;
            d.inc  = (2400.0f + tune * 5000.0f) * kInvSampleRate;
            d.filt.setCoeffs(clampf(3500.0f + tone * 9000.0f, 500.0f, 13500.0f), 0.9f);
            break;
        }
        case DL_CLAP:
            d.ampCoef   = decCoef(90.0f + dec * 400.0f);
            d.noiseCoef = d.ampCoef;
            d.noiseAmp  = 1.0f;
            d.filt.setCoeffs(900.0f + tone * 3200.0f, 1.6f);
            break;
        case DL_TOM:
            d.inc = (70.0f + tune * 260.0f) * kInvSampleRate;
            d.ampCoef   = decCoef(80.0f + dec * 600.0f);
            d.pitch     = 1.2f + tone * 2.5f;
            d.pitchCoef = decCoef(30.0f + tone * 90.0f);
            d.noiseAmp  = tone * 0.35f;
            d.noiseCoef = decCoef(10.0f);
            break;
        case DL_RIM:
            d.inc  = (400.0f + tune * 1400.0f) * kInvSampleRate;
            d.inc2 = d.inc * 1.63f;
            d.ampCoef   = decCoef(6.0f + dec * 60.0f);
            d.noiseAmp  = 0.5f + tone * 0.6f;
            d.noiseCoef = d.ampCoef;
            d.filt.setCoeffs(1800.0f + tone * 6000.0f, 2.2f);
            break;
        case DL_CRASH:
            d.ampCoef   = decCoef(300.0f + dec * 2600.0f);
            d.noiseCoef = d.ampCoef;
            d.noiseAmp  = 1.0f;
            d.inc  = (5200.0f + tune * 4000.0f) * kInvSampleRate;
            d.filt.setCoeffs(clampf(4200.0f + tone * 9000.0f, 800.0f, 13800.0f), 0.75f);
            break;
        default:   // PERC - short FM blip
            d.inc  = (300.0f + tune * 1800.0f) * kInvSampleRate;
            d.inc2 = d.inc * (1.0f + tone * 5.0f);
            d.ampCoef   = decCoef(20.0f + dec * 400.0f);
            d.pitch     = 1.0f + tone * 5.0f;
            d.pitchCoef = decCoef(15.0f + dec * 60.0f);
            d.noiseAmp  = 0.0f;
            d.noiseCoef = 0.5f;
            break;
    }
}

void DrumEngine::render(float* out, int n) {
    if (!kit_) return;
    for (uint8_t lane = 0; lane < DL_COUNT; ++lane) {
        DV& d = v_[lane];
        if (!d.active) continue;
        for (int i = 0; i < n; ++i) {
            float s = 0.0f;
            switch (lane) {
                case DL_KICK: {
                    d.ph = wrap01(d.ph + d.inc * (1.0f + d.pitch));
                    s = fastSin01(d.ph) * 1.15f;
                    s += d.rng.bipolar() * d.noiseAmp;
                    d.pitch *= d.pitchCoef;
                    d.noiseAmp *= d.noiseCoef;
                    break;
                }
                case DL_SNARE: {
                    d.ph  = wrap01(d.ph  + d.inc  * (1.0f + d.pitch));
                    d.ph2 = wrap01(d.ph2 + d.inc2 * (1.0f + d.pitch));
                    s = (fastSin01(d.ph) * 0.6f + fastSin01(d.ph2) * 0.3f);
                    s += d.filt.process(d.rng.bipolar(), 2) * d.noiseAmp;
                    d.pitch *= d.pitchCoef;
                    d.noiseAmp *= d.noiseCoef;
                    break;
                }
                case DL_CHH: case DL_OHH: case DL_CRASH: {
                    // Six detuned squares through a high-pass = classic metallic hat.
                    d.ph  = wrap01(d.ph  + d.inc);
                    d.ph2 = wrap01(d.ph2 + d.inc * 1.4713f);
                    float metal = ((d.ph < 0.5f ? 1.0f : -1.0f) + (d.ph2 < 0.5f ? 1.0f : -1.0f)) * 0.32f;
                    s = d.filt.process(metal * 0.5f + d.rng.bipolar() * 0.85f, 2) * d.noiseAmp;
                    break;
                }
                case DL_CLAP: {
                    // Three fast bursts then a tail.
                    float burst = 1.0f;
                    if (d.t < 1400) {
                        uint32_t m = d.t % 460;
                        burst = (m < 200) ? 1.0f : 0.35f;
                    }
                    s = d.filt.process(d.rng.bipolar(), 3) * 2.0f * burst * d.noiseAmp;
                    break;
                }
                case DL_TOM: {
                    d.ph = wrap01(d.ph + d.inc * (1.0f + d.pitch));
                    s = fastSin01(d.ph) + d.rng.bipolar() * d.noiseAmp;
                    d.pitch *= d.pitchCoef;
                    d.noiseAmp *= d.noiseCoef;
                    break;
                }
                case DL_RIM: {
                    d.ph  = wrap01(d.ph  + d.inc);
                    d.ph2 = wrap01(d.ph2 + d.inc2);
                    s = (d.ph < 0.5f ? 0.7f : -0.7f) + (d.ph2 < 0.5f ? 0.4f : -0.4f);
                    s = d.filt.process(s + d.rng.bipolar() * d.noiseAmp, 3) * 1.6f;
                    break;
                }
                default: {
                    d.ph2 = wrap01(d.ph2 + d.inc2);
                    float mod = fastSin01(d.ph2) * d.pitch;
                    d.ph = wrap01(d.ph + d.inc);
                    s = fastSin01(wrap01(d.ph + mod));
                    d.pitch *= d.pitchCoef;
                    break;
                }
            }
            out[i] += softClip(s) * d.amp * d.gain * 0.55f;
            d.amp *= d.ampCoef;
            ++d.t;
        }
        if (d.amp < 0.0004f) { d.active = false; d.amp = 0.0f; }
    }
}

}  // namespace synth
