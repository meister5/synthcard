// SynthCard - CHIP engine: NES-style pulse, LFSR noise, arp and vibrato.
//
// What makes a chiptune sound like a chiptune is not the square wave - it is
// the 15-bit LFSR noise, the fixed duty cycles, the fast per-note arpeggio
// used to fake chords on a mono channel, and vibrato that starts late. All
// four are here; a plain square wave is not enough.
#include "engines.h"

namespace synth {

// Semitone cycles for the chip arp. Terminated by 127.
static const int8_t kArpTables[6][4] = {
    {  0, 127, 127, 127},   // OFF (unused)
    {  0,   4,   7, 127},   // MAJOR
    {  0,   3,   7, 127},   // MINOR
    {  0,  12, 127, 127},   // OCT
    {  0,   7, 127, 127},   // FIFTH
    { 12,   7,   0, 127},   // DOWN
};
static const float kDuty[4] = {0.125f, 0.25f, 0.5f, 0.75f};

void chipNoteOn(ChipState& s, const Patch& pt, Rng& rng) {
    s.lfsr = 0x7FFF;                       // all-ones seed, as the hardware boots
    s.ph   = 0.0f;
    s.ph2  = 0.0f;
    s.duty = kDuty[pt.get(PC_DUTY) & 3];

    s.arpMode = pt.get(PC_ARP);
    s.arpStep = 0;
    s.arpOffset = 0.0f;
    // Fast end of the range is ~50 Hz, which is the classic "chord" shimmer.
    const float rate = 8.0f + pt.norm(PC_ARP_SPD) * 42.0f;
    s.arpPeriod = (int)(kSampleRate / rate);
    if (s.arpPeriod < 8) s.arpPeriod = 8;
    s.arpCountdown = s.arpPeriod;

    s.vibDepth = pt.norm(PC_VIB_DEP) * 0.35f;      // semitones
    s.vibPh    = 0.0f;
    s.vibDelay = (int)(pt.norm(PC_VIB_DLY) * 0.6f * kSampleRate);

    s.noiseMode = pt.get(PC_NOISE_MODE);
    s.noiseDiv   = 0;
    s.noiseCount = 0;
    s.noiseVal   = 0.0f;

    const float semi = (float)pt.get(PC_O2_SEMI) - 24.0f;
    s.rat2 = powf(2.0f, semi / 12.0f);
    (void)rng;
}

// NES noise channel: 15-bit LFSR, tap 1 in long mode and tap 6 in short mode.
// PERIODIC clocks it slowly enough to become a rough pitched buzz.
static inline float lfsrStep(ChipState& s) {
    const uint32_t tap = (s.noiseMode == 1) ? 6u : 1u;
    const uint32_t bit = ((s.lfsr ^ (s.lfsr >> tap)) & 1u);
    s.lfsr = (s.lfsr >> 1) | (bit << 14);
    return (s.lfsr & 1u) ? 1.0f : -1.0f;
}

void chipRender(ChipState& s, const EngineCtx& c, float* out, int n) {
    const Patch& pt = *c.pt;
    const float l2 = pt.norm(PC_O2_LEVEL);
    const bool  noise = pt.get(PC_NOISE_MODE) != 0 && pt.get(PC_DUTY) == 0;
    const float duty = s.duty;

    for (int i = 0; i < n; ++i) {
        // --- chip arp: steps the pitch through a chord shape.
        if (s.arpMode != 0) {
            if (--s.arpCountdown <= 0) {
                s.arpCountdown = s.arpPeriod;
                const int8_t* tbl = kArpTables[s.arpMode < 6 ? s.arpMode : 0];
                s.arpStep = (uint8_t)((s.arpStep + 1) & 3);
                if (tbl[s.arpStep] == 127) s.arpStep = 0;
                s.arpOffset = (float)tbl[s.arpStep];
            }
        }
        // --- vibrato, delayed like a tracker's.
        float vib = 0.0f;
        if (s.vibDepth > 0.0f) {
            if (s.vibDelay > 0) --s.vibDelay;
            else {
                s.vibPh = wrap01(s.vibPh + 6.0f * kInvSampleRate);
                vib = fastSin01(s.vibPh) * s.vibDepth;
            }
        }
        const float bend = s.arpOffset + vib;
        // exp2 on a small argument: two terms are plenty for +-12 semitones.
        const float x = bend * (1.0f / 12.0f);
        const float mul = 1.0f + x * (0.6931472f + x * 0.2402265f);
        const float inc = clampf(c.inc * mul, 0.0f, 0.49f);

        float sig;
        if (noise) {
            // The noise channel is clocked at the note's rate, which is what
            // turns the LFSR into a pitched percussion source.
            s.noiseCount += (int)(inc * 65536.0f);
            if (s.noiseCount >= 65536) { s.noiseCount -= 65536; s.noiseVal = lfsrStep(s); }
            sig = s.noiseVal;
        } else {
            s.ph = wrap01(s.ph + inc);
            sig = (s.ph < duty ? 1.0f : -1.0f) + polyBlep(s.ph, inc)
                  - polyBlep(wrap01(s.ph + 1.0f - duty), inc);
            if (l2 > 0.001f) {
                const float inc2 = clampf(inc * s.rat2, 0.0f, 0.49f);
                s.ph2 = wrap01(s.ph2 + inc2);
                sig += ((s.ph2 < duty ? 1.0f : -1.0f) + polyBlep(s.ph2, inc2)
                        - polyBlep(wrap01(s.ph2 + 1.0f - duty), inc2)) * l2;
            }
        }
        // 4-bit output stage: the quantisation is part of the sound.
        out[i] = (float)((int)(sig * 8.0f)) * (1.0f / 8.0f) * 0.7f;
    }
}

}  // namespace synth
