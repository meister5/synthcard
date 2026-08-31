// Minimal WAV writing and signal measurement for the host renderer and the
// benchmark. Header-only so both tools share one copy.
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include "audio/dsp.h"

namespace wav {

inline void write(const std::string& path, const std::vector<float>& samples, int rate) {
    const int n = (int)samples.size();
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); u32((uint32_t)(36 + n * 2)); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(1);
    u32((uint32_t)rate); u32((uint32_t)(rate * 2)); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32((uint32_t)(n * 2));
    for (float s : samples) {
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        int16_t v = (int16_t)(s * 32000.0f);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
}

// ---- analysis -------------------------------------------------------------
// Iterative radix-2 FFT, in place. Enough for spectral measurements; nothing
// here runs on the device.
inline void fft(std::vector<float>& re, std::vector<float>& im) {
    const int n = (int)re.size();
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / len;
        const double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < len / 2; ++k) {
                const double ur = re[i + k], ui = im[i + k];
                const double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                const double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = (float)(ur + vr);       im[i + k] = (float)(ui + vi);
                re[i + k + len / 2] = (float)(ur - vr);
                im[i + k + len / 2] = (float)(ui - vi);
                const double nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
}

struct Spectrum {
    std::vector<float> mag;      // bins 0..n/2
    float binHz;
};

// Hann-windowed magnitude spectrum of the first `n` samples from `off`.
inline Spectrum spectrum(const std::vector<float>& x, int off, int n, float rate) {
    std::vector<float> re(n, 0.0f), im(n, 0.0f);
    for (int i = 0; i < n; ++i) {
        const float w = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * i / (float)(n - 1));
        const int idx = off + i;
        re[i] = (idx < (int)x.size() ? x[idx] : 0.0f) * w;
    }
    fft(re, im);
    Spectrum s;
    s.binHz = rate / (float)n;
    s.mag.resize(n / 2 + 1);
    for (int i = 0; i <= n / 2; ++i) s.mag[i] = sqrtf(re[i] * re[i] + im[i] * im[i]);
    return s;
}

inline float centroidHz(const Spectrum& s) {
    double num = 0.0, den = 0.0;
    for (size_t i = 1; i < s.mag.size(); ++i) { num += s.mag[i] * i * s.binHz; den += s.mag[i]; }
    return den > 1e-9 ? (float)(num / den) : 0.0f;
}

// Share of total energy above `hz`. For the kick this is the number that
// predicts what the Cardputer's speaker can actually reproduce.
inline float energyAbove(const Spectrum& s, float hz) {
    double above = 0.0, total = 0.0;
    for (size_t i = 1; i < s.mag.size(); ++i) {
        const double e = (double)s.mag[i] * s.mag[i];
        total += e;
        if (i * s.binHz >= hz) above += e;
    }
    return total > 1e-12 ? (float)(above / total) : 0.0f;
}

// Peak level surviving a 200 Hz high-pass, in dB relative to the full-band
// peak. This is the number that predicts whether a kick is audible on the
// Cardputer's speaker: an energy *fraction* is swamped by the long
// sub-200 Hz tail and stays near zero even for a kick you can hear perfectly
// well.
inline float highPassPeakDb(const std::vector<float>& x, float hz) {
    synth::SVF f;
    f.setCoeffs(hz, 0.707f);
    float hp = 0.0f, full = 0.0f;
    for (float v : x) {
        full = fmaxf(full, fabsf(v));
        hp = fmaxf(hp, fabsf(f.process(v, 2)));
    }
    if (full < 1e-9f) return -120.0f;
    return 20.0f * log10f(fmaxf(hp, 1e-9f) / full);
}

inline float peak(const std::vector<float>& x) {
    float p = 0.0f;
    for (float v : x) p = fmaxf(p, fabsf(v));
    return p;
}

inline float dcOffset(const std::vector<float>& x) {
    if (x.empty()) return 0.0f;
    double s = 0.0;
    for (float v : x) s += v;
    return (float)(s / x.size());
}

// Milliseconds from the start until the signal first reaches its peak.
inline float peakTimeMs(const std::vector<float>& x, float rate) {
    float p = 0.0f; size_t at = 0;
    for (size_t i = 0; i < x.size(); ++i) if (fabsf(x[i]) > p) { p = fabsf(x[i]); at = i; }
    return (float)at * 1000.0f / rate;
}

// Time from the peak until the envelope has fallen 60 dB, measured on a
// short-window running peak so noise does not make it jitter.
inline float decay60Ms(const std::vector<float>& x, float rate) {
    const int win = 64;
    std::vector<float> env;
    env.reserve(x.size() / win + 1);
    for (size_t i = 0; i + win <= x.size(); i += win) {
        float p = 0.0f;
        for (int k = 0; k < win; ++k) p = fmaxf(p, fabsf(x[i + k]));
        env.push_back(p);
    }
    if (env.empty()) return 0.0f;
    size_t at = 0; float p = 0.0f;
    for (size_t i = 0; i < env.size(); ++i) if (env[i] > p) { p = env[i]; at = i; }
    const float target = p * 0.001f;
    for (size_t i = at; i < env.size(); ++i)
        if (env[i] <= target)
            return (float)((i - at) * win) * 1000.0f / rate;
    return (float)((env.size() - at) * win) * 1000.0f / rate;
}

// Energy that is not near a harmonic of f0 - the signature of aliasing.
inline float aliasEnergy(const Spectrum& s, float f0) {
    if (f0 <= 0.0f) return 0.0f;
    double bad = 0.0, total = 0.0;
    const float tol = fmaxf(s.binHz * 2.0f, f0 * 0.03f);
    for (size_t i = 1; i < s.mag.size(); ++i) {
        const float hz = i * s.binHz;
        const double e = (double)s.mag[i] * s.mag[i];
        total += e;
        const float nearest = roundf(hz / f0) * f0;
        if (nearest < 1.0f || fabsf(hz - nearest) > tol) bad += e;
    }
    return total > 1e-12 ? (float)(bad / total) : 0.0f;
}

}  // namespace wav
