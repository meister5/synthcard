#include "drums.h"
#include <string.h>

namespace synth {

const char* const kDrumNames[DL_COUNT] = {
    "KICK", "SNARE", "CL HAT", "OP HAT", "RIDE", "CRASH",
    "CLAP", "TOM", "RIM", "COWBELL", "SHAKER", "PERC"
};
const char* const kDrumShort[DL_COUNT] = {
    "KIK", "SNR", "CHH", "OHH", "RID", "CRS",
    "CLP", "TOM", "RIM", "CWB", "SHK", "PRC"
};
const char* const kDrumParamNames[DP_COUNT] =
    {"TUNE", "DECAY", "TONE", "LEVEL", "SNAP", "DRIVE", "DELAY", "REVERB"};
const char* const kDrumMacroNames[DM_COUNT] = {"PUNCH", "TONE", "SPACE"};

// Closed hat, open hat and ride cut each other. Nothing else chokes: a snare
// cutting a clap would be wrong, and lanes that choke themselves just
// retrigger, which they already do.
const uint8_t kChokeGroup[DL_COUNT] =
    { 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };

// Which family renders each lane. Resolved once per lane per block.
enum Family : uint8_t { F_MEMBRANE = 0, F_SNARE, F_METAL, F_NOISE, F_PERC };
static const uint8_t kFamily[DL_COUNT] = {
    F_MEMBRANE,  // KICK
    F_SNARE,     // SNARE
    F_METAL,     // CL HAT
    F_METAL,     // OP HAT
    F_METAL,     // RIDE
    F_METAL,     // CRASH
    F_NOISE,     // CLAP
    F_MEMBRANE,  // TOM
    F_SNARE,     // RIM
    F_METAL,     // COWBELL
    F_NOISE,     // SHAKER
    F_PERC,      // PERC
};

// ------------------------------------------------------------------- kits --
// tune, decay, tone, level, snap, drive, delay, reverb
#define K(a,b,c,d,e,f,g,h) {a,b,c,d,e,f,g,h}
const KitPreset kKits[] = {
  {"808", {
    K( 34, 92, 30,122, 42, 44,  8, 10),  // KICK   long, deep, saturated
    K( 52, 50, 52,100, 62, 22, 14, 26),  // SNARE
    K( 60, 14, 96, 76, 30, 10,  6, 10),  // CL HAT
    K( 60, 52, 96, 72, 24, 10, 10, 20),  // OP HAT
    K( 55, 68, 68, 42, 18,  8,  8, 26),  // RIDE
    K( 50,102, 84, 62, 14,  8, 12, 42),  // CRASH
    K( 60, 44, 70, 88, 40, 16, 22, 38),  // CLAP
    K( 50, 60, 40, 84, 30, 22, 10, 20),  // TOM
    K( 78, 12, 80, 70, 52, 10,  8, 14),  // RIM
    K( 64, 40, 60, 28, 20, 10, 10, 16),  // COWBELL
    K( 70, 24, 80,116, 62,  4,  6, 12),  // SHAKER
    K( 76, 26, 64, 68, 36, 14, 14, 20),  // PERC
  }},
  {"909", {
    K( 50, 66, 62,126, 62, 58,  6,  8),
    K( 62, 44, 74,108, 74, 30, 12, 20),
    K( 72, 10,110, 82, 40, 14,  4,  8),
    K( 72, 44,108, 76, 34, 14,  8, 16),
    K( 66, 60, 84, 46, 26, 10,  6, 20),
    K( 60, 96,100, 66, 20, 10, 10, 34),
    K( 62, 40, 78, 94, 48, 20, 18, 30),
    K( 58, 52, 54, 86, 40, 26,  8, 16),
    K( 88, 10, 92, 72, 60, 12,  6, 12),
    K( 70, 36, 66, 26, 24, 12,  8, 14),
    K( 78, 20, 88,116, 56,  6,  4, 10),
    K( 84, 22, 80, 70, 40, 16, 12, 18),
  }},
  {"ELECTRO", {
    K( 58, 58, 88,120, 74, 70,  8, 12),
    K( 72, 38, 96,102, 84, 44, 16, 24),
    K( 84,  8,120, 80, 46, 20,  6, 10),
    K( 84, 34,118, 72, 38, 20, 10, 18),
    K( 78, 54, 96, 44, 30, 14,  8, 22),
    K( 74, 88,116, 62, 22, 14, 12, 36),
    K( 70, 34, 90, 88, 56, 30, 24, 34),
    K( 66, 44, 72, 82, 48, 34, 10, 18),
    K( 96,  8,110, 74, 70, 18,  8, 14),
    K( 82, 32, 78, 28, 30, 18, 10, 16),
    K( 90, 16,104,118, 66, 10,  6, 12),
    K( 96, 18,104, 74, 50, 24, 16, 22),
  }},
  {"MINIMAL", {
    K( 42, 62, 20,112, 26, 24,  4,  6),
    K( 50, 32, 40, 88, 40, 12,  8, 14),
    K( 60,  8, 88, 62, 22,  6,  2,  6),
    K( 60, 28, 84, 54, 18,  6,  6, 10),
    K( 54, 50, 60, 34, 14,  4,  4, 14),
    K( 48, 72, 70, 46, 10,  4,  8, 24),
    K( 56, 28, 58, 72, 28, 10, 12, 20),
    K( 48, 44, 30, 74, 22, 12,  6, 12),
    K( 72,  8, 66, 58, 36,  6,  4,  8),
    K( 60, 30, 50, 20, 16,  6,  6, 10),
    K( 64, 16, 66,108, 48,  2,  4,  8),
    K( 68, 16, 52, 62, 26,  8,  8, 14),
  }},
  {"INDUSTRIAL", {
    K( 30,100,110,127, 96,110, 14, 26),
    K( 44, 62,110,112, 98, 84, 20, 40),
    K( 76, 16,120, 86, 60, 44,  8, 20),
    K( 76, 58,118, 78, 52, 44, 14, 34),
    K( 70, 70,104, 52, 40, 34, 12, 36),
    K( 66,110,124, 74, 34, 34, 18, 54),
    K( 52, 54,104, 94, 72, 60, 28, 50),
    K( 40, 74, 96, 92, 66, 70, 16, 34),
    K( 92, 14,120, 84, 84, 40, 10, 26),
    K( 74, 44, 92, 36, 44, 38, 14, 30),
    K( 84, 30,112,126, 78, 24, 10, 24),
    K( 60, 40,116, 86, 62, 54, 20, 38),
  }},
  {"LOFI", {
    K( 46, 70, 12,108, 20, 34, 10, 24),
    K( 52, 42, 26, 92, 34, 24, 16, 40),
    K( 48, 12, 58, 60, 18, 14,  8, 20),
    K( 48, 38, 54, 56, 16, 14, 12, 30),
    K( 44, 56, 44, 34, 12, 10, 10, 32),
    K( 40, 78, 52, 48,  8, 10, 14, 44),
    K( 50, 36, 44, 76, 26, 20, 20, 44),
    K( 50, 54, 22, 78, 20, 22, 12, 30),
    K( 60, 10, 48, 56, 30, 12,  8, 22),
    K( 54, 34, 40, 18, 14, 12, 10, 24),
    K( 58, 22, 50,108, 40,  8,  8, 20),
    K( 58, 24, 36, 62, 24, 18, 16, 30),
  }},
  {"ACOUSTIC", {
    K( 44, 56, 24,112, 54, 16,  4, 18),
    K( 56, 46, 60,102, 82, 12, 10, 34),
    K( 66, 12, 76, 74, 44,  6,  4, 16),
    K( 66, 40, 74, 68, 36,  6,  8, 28),
    K( 58, 76, 56, 48, 34,  4,  6, 34),
    K( 52,100, 72, 64, 26,  4, 10, 46),
    K( 58, 40, 56, 84, 50,  8, 14, 40),
    K( 52, 62, 34, 88, 56, 10,  8, 30),
    K( 80, 12, 70, 72, 74,  6,  4, 20),
    K( 66, 38, 54, 24, 28,  6,  6, 22),
    K( 68, 26, 70,114, 70,  2,  4, 18),
    K( 72, 28, 48, 66, 44, 10, 10, 26),
  }},
  {"TRAP", {
    K( 26,110, 26,127, 34, 52,  4,  8),
    K( 58, 40, 82,104, 86, 26, 10, 22),
    K( 76,  6,112, 84, 52, 12,  2,  6),
    K( 76, 30,110, 74, 42, 12,  6, 14),
    K( 70, 52, 88, 42, 30,  8,  4, 18),
    K( 64, 92,108, 62, 22,  8,  8, 32),
    K( 66, 34, 84, 92, 62, 18, 14, 28),
    K( 44, 66, 44, 86, 40, 28,  6, 16),
    K( 90,  8,100, 74, 76, 12,  4, 10),
    K( 76, 30, 74, 26, 30, 10,  6, 12),
    K( 86, 12, 98,122, 74,  6,  2,  8),
    K( 88, 20, 88, 72, 48, 16, 10, 16),
  }},
  {"BREAKS", {
    K( 46, 60, 46,116, 66, 40,  6, 14),
    K( 60, 48, 70,106, 88, 34, 14, 30),
    K( 70, 12, 98, 80, 50, 18,  6, 14),
    K( 70, 42, 96, 74, 42, 18, 10, 24),
    K( 62, 66, 78, 48, 34, 12,  8, 28),
    K( 56, 94, 96, 64, 26, 12, 12, 40),
    K( 62, 42, 74, 90, 60, 26, 18, 34),
    K( 54, 58, 48, 88, 54, 30, 10, 24),
    K( 84, 10, 88, 74, 72, 16,  6, 18),
    K( 72, 36, 68, 26, 32, 14,  8, 18),
    K( 80, 20, 90,118, 66,  8,  6, 14),
    K( 82, 24, 76, 72, 46, 20, 12, 22),
  }},
  {"HOUSE", {
    K( 44, 70, 44,124, 58, 46,  4, 10),
    K( 58, 44, 68,100, 70, 24, 12, 26),
    K( 68, 10,104, 84, 44, 14,  4, 10),
    K( 68, 46,102, 80, 36, 14, 10, 22),
    K( 62, 64, 82, 50, 30, 10,  8, 26),
    K( 56, 98,102, 64, 22, 10, 12, 38),
    K( 62, 40, 76, 90, 52, 20, 20, 34),
    K( 54, 54, 50, 82, 40, 24,  8, 20),
    K( 84, 10, 90, 70, 62, 14,  6, 16),
    K( 70, 34, 70, 26, 28, 12,  8, 16),
    K( 78, 22, 92,120, 60,  6,  6, 14),
    K( 80, 24, 78, 70, 44, 18, 14, 22),
  }},
  {"GLITCH", {
    K( 80, 44,102,110, 88, 78, 20, 26),
    K( 96, 62, 92, 96, 96, 60, 26, 34),
    K(110, 18,124, 74, 70, 40, 14, 22),
    K(104, 62,120, 68, 60, 40, 20, 30),
    K( 98, 58,112, 44, 50, 34, 16, 30),
    K( 92, 84,124, 64, 40, 34, 22, 44),
    K( 88, 58,110, 88, 78, 50, 30, 40),
    K(100, 40,104, 84, 70, 56, 18, 28),
    K(116, 24,116, 78, 92, 34, 14, 24),
    K(104, 40,100, 34, 52, 30, 16, 26),
    K(112, 22,118,124, 84, 20, 12, 22),
    K(120, 50,120, 82, 74, 46, 24, 34),
  }},
  {"EXPERIMENT", {
    K( 88, 74,116,106, 70, 90, 34, 48),
    K(102, 84, 96, 94, 80, 74, 40, 56),
    K(118, 30,124, 70, 58, 54, 26, 40),
    K(112, 74,120, 66, 50, 54, 32, 50),
    K(106, 90,110, 44, 44, 44, 30, 52),
    K(100,116,124, 64, 36, 44, 38, 66),
    K( 94, 74,112, 84, 66, 62, 44, 60),
    K(108, 60,108, 82, 62, 68, 34, 48),
    K(122, 40,118, 76, 84, 48, 28, 44),
    K(110, 56,104, 32, 46, 44, 30, 46),
    K(118, 40,120,122, 78, 34, 24, 42),
    K(126, 70,124, 80, 70, 60, 40, 54),
  }},
};
#undef K
const uint8_t kKitCount = sizeof(kKits) / sizeof(kKits[0]);

void DrumKit::reset() { loadKit(*this, 0); }

void loadKit(DrumKit& dst, uint8_t index) {
    if (index >= kKitCount) index = 0;
    const KitPreset& k = kKits[index];
    for (uint8_t l = 0; l < DL_COUNT; ++l) {
        for (uint8_t p = 0; p < DP_COUNT; ++p) dst.p[l][p] = k.p[l][p];
        // Kits are authored with the base parameters; the macros are the
        // player's performance layer and always start neutral.
        for (uint8_t m = 0; m < DM_COUNT; ++m) dst.m[l][m] = kMacroNeutral;
    }
    strncpy(dst.name, k.name, sizeof(dst.name) - 1);
    dst.name[sizeof(dst.name) - 1] = 0;
}

uint8_t DrumKit::effective(uint8_t lane, uint8_t par) const {
    int v = get(lane, par);
    const int punch = (int)macro(lane, DM_PUNCH) - (int)kMacroNeutral;
    const int tone  = (int)macro(lane, DM_TONE)  - (int)kMacroNeutral;
    const int space = (int)macro(lane, DM_SPACE) - (int)kMacroNeutral;
    switch (par) {
        // PUNCH is "hit harder": more transient, more saturation, a shorter
        // tail so the hit reads as tighter rather than merely louder.
        case DP_SNAP:  v += punch * 50 / 64; break;
        case DP_DRIVE: v += punch * 45 / 64; break;
        case DP_DECAY: v -= punch * 18 / 64; break;
        case DP_TONE:  v += tone  * 55 / 64; break;
        case DP_DLY:   v += space * 45 / 64; break;
        case DP_REV:   v += space * 55 / 64; break;
        default: break;
    }
    return (uint8_t)clampi(v, 0, 127);
}

// ----------------------------------------------------------------- engine --
void DrumEngine::init() {
    for (uint8_t i = 0; i < DL_COUNT; ++i) {
        v_[i] = DV();
        v_[i].rng.s = 0xB5297A4Du + i * 0x68E31DA4u;
        v_[i].filt.reset();
        v_[i].filt2.reset();
    }
}

void DrumEngine::allOff() {
    for (auto& d : v_) { d.active = false; d.amp = 0.0f; }
}

float DrumEngine::laneLevel(uint8_t lane) const {
    return lane < DL_COUNT ? v_[lane].amp : 0.0f;
}
bool DrumEngine::laneActive(uint8_t lane) const {
    return lane < DL_COUNT && v_[lane].active;
}

void DrumEngine::trigger(uint8_t lane, uint8_t vel) {
    if (lane >= DL_COUNT || !kit_ || vel == 0) return;

    // Choke first: a closed hat has to silence the open hat it replaces, and
    // a 2 ms fade rather than a hard cut, because a hard cut clicks.
    const uint8_t grp = kChokeGroup[lane];
    if (grp) {
        for (uint8_t o = 0; o < DL_COUNT; ++o) {
            if (o == lane || kChokeGroup[o] != grp || !v_[o].active) continue;
            v_[o].ampCoef = decCoef(2.0f);
        }
    }

    DV& d = v_[lane];
    DrumHit h;
    h.tune  = kit_->effective(lane, DP_TUNE)  * (1.0f / 127.0f);
    h.decay = kit_->effective(lane, DP_DECAY) * (1.0f / 127.0f);
    h.tone  = kit_->effective(lane, DP_TONE)  * (1.0f / 127.0f);
    h.level = kit_->effective(lane, DP_LEVEL) * (1.0f / 127.0f);
    h.snap  = kit_->effective(lane, DP_SNAP)  * (1.0f / 127.0f);
    h.drive = kit_->effective(lane, DP_DRIVE) * (1.0f / 127.0f);
    h.vel   = (float)vel * (1.0f / 127.0f);

    d.active  = true;
    d.t       = 0;
    d.amp     = 1.0f;
    d.gain    = h.level * h.vel;
    d.sendDly = kit_->effective(lane, DP_DLY) * (1.0f / 127.0f);
    d.sendRev = kit_->effective(lane, DP_REV) * (1.0f / 127.0f);
    // Fields a family does not set must not carry over from the last hit.
    d.noiseAmp = d.noise2Amp = d.snap = d.subAmp = 0.0f;
    d.noiseCoef = d.noise2Coef = d.snapCoef = d.subCoef = 0.0f;
    d.pitch = d.pitch2 = 0.0f;
    d.pitchCoef = d.pitch2Coef = 1.0f;
    d.atk = 1.0f; d.atkInc = 0.0f;
    d.mcount = 0;

    switch (kFamily[lane]) {
        case F_MEMBRANE: membraneTrigger(d, h, lane); break;
        case F_SNARE:    snareTrigger(d, h, lane); break;
        case F_METAL:    metalTrigger(d, h, lane); break;
        case F_NOISE:    noiseTrigger(d, h, lane); break;
        default:         percTrigger(d, h, lane); break;
    }
}

void DrumEngine::render(float* out, float* dly, float* rev, int n) {
    if (!kit_) return;
    for (uint8_t lane = 0; lane < DL_COUNT; ++lane) {
        DV& d = v_[lane];
        if (!d.active) continue;
        d.filt.flush();
        d.filt2.flush();

        // The family is resolved once here rather than once per sample, which
        // is the whole reason the old switch-in-the-inner-loop is gone.
        switch (kFamily[lane]) {
            case F_MEMBRANE: membraneRender(d, lane, scratch_, n); break;
            case F_SNARE:    snareRender(d, lane, scratch_, n); break;
            case F_METAL:    metalRender(d, lane, scratch_, n); break;
            case F_NOISE:    noiseRender(d, lane, scratch_, n); break;
            default:         percRender(d, lane, scratch_, n); break;
        }

        const float sd = d.sendDly, sr = d.sendRev;
        for (int i = 0; i < n; ++i) {
            const float s = softClip(scratch_[i]) * 0.55f;
            out[i] += s;
            dly[i] += s * sd;
            rev[i] += s * sr;
        }
        if (d.amp < 0.0004f) { d.active = false; d.amp = 0.0f; }
    }
}

}  // namespace synth
