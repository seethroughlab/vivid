#pragma once
// A tiny self-contained real-signal spectrum helper for the audio→visual bridge: an iterative
// radix-2 Cooley-Tukey FFT + a reduction to log-spaced magnitude bands. Header-only, no allocation
// in the hot path beyond the caller's scratch, no external deps. Used FRAME-SIDE (UI thread) on a
// snapshot of a track's recent samples, so the audio thread only pays for a cheap ring copy.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vivid::audio {

// In-place iterative radix-2 FFT. `re`/`im` are length n (a power of two). Forward transform.
inline void fft_radix2(float* re, float* im, int n) {
    // bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = -6.28318530717958648f / static_cast<float>(len);
        const float wr = std::cos(ang), wi = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float cr = 1.f, ci = 0.f;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k, b = i + k + len / 2;
                const float tr = cr * re[b] - ci * im[b];
                const float ti = cr * im[b] + ci * re[b];
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
                const float ncr = cr * wr - ci * wi;   // advance the twiddle
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

// Hann-window `in[n]` (n a power of two), FFT it, and reduce the magnitude spectrum to `nbands`
// log-spaced bands (≈40 Hz .. nyquist), each normalized to a roughly 0..1 visual range. `out[nbands]`.
// `scratch_re`/`scratch_im` are caller-owned length-n buffers (avoids per-call allocation).
inline void spectrum_log_bands(const float* in, int n, float sample_rate,
                               float* out, int nbands,
                               float* scratch_re, float* scratch_im) {
    for (int i = 0; i < nbands; ++i) out[i] = 0.f;
    if (n <= 1 || sample_rate <= 0.f) return;
    const float twoPiOverN = 6.28318530717958648f / static_cast<float>(n - 1);
    for (int i = 0; i < n; ++i) {
        const float w = 0.5f * (1.f - std::cos(twoPiOverN * i));   // Hann
        scratch_re[i] = in[i] * w; scratch_im[i] = 0.f;
    }
    fft_radix2(scratch_re, scratch_im, n);
    const float nyq = sample_rate * 0.5f;
    const float f_lo = 40.f, f_hi = std::min(nyq, 16000.f);
    const float log_lo = std::log2(f_lo), log_hi = std::log2(std::max(f_hi, f_lo * 2.f));
    const float binHz = sample_rate / static_cast<float>(n);
    int counts[64] = {0};   // nbands is small (≤ this)
    if (nbands > 64) nbands = 64;
    for (int k = 1; k < n / 2; ++k) {          // skip DC; use the positive-frequency half
        const float hz = k * binHz;
        if (hz < f_lo || hz > f_hi) continue;
        int band = static_cast<int>((std::log2(hz) - log_lo) / (log_hi - log_lo) * nbands);
        if (band < 0) band = 0; if (band >= nbands) band = nbands - 1;
        const float mag = std::sqrt(scratch_re[k] * scratch_re[k] + scratch_im[k] * scratch_im[k]);
        out[band] += mag; counts[band]++;
    }
    // Average per band, then map to a visual 0..1 through a PERCEPTUAL (dB) curve instead of a linear
    // gain + hard clamp. A flat `*8` pinned every moderately-loud band to 1.0 (no visible variation on
    // real material); mapping a ~54 dB window above a reference magnitude into 0..1 keeps loud bands off
    // the ceiling and lets quiet detail register, matching how loudness is actually perceived.
    constexpr float kRefMag  = 0.12f;    // magnitude that reaches full scale (was the old *8 knee, 1/8)
    constexpr float kRangeDb = 54.f;     // visible dynamic window below the reference
    for (int i = 0; i < nbands; ++i) {
        if (counts[i] > 0) out[i] /= static_cast<float>(counts[i]);
        const float db = 20.f * std::log10(std::max(out[i], 1e-6f) / kRefMag);   // 0 dB at kRefMag
        out[i] = std::clamp(1.f + db / kRangeDb, 0.f, 1.f);                       // -kRangeDb..0 dB -> 0..1
    }
    // Fill bands that caught NO FFT bin. At high band counts the log bands are finer than the ~sr/n bin
    // spacing (≈47 Hz at n=1024), so many bands — especially low ones — are structurally empty and would
    // sit dead at 0 (a visual equaliser then has bars that never move). Linearly interpolate each empty
    // run from its filled neighbours, and hold the nearest filled value at the ends. Filled bands
    // (counts>0) are authoritative and untouched; a coarse spectrum (few bands, all filled) is a no-op.
    int prev = -1;
    for (int i = 0; i < nbands; ++i) {
        if (counts[i] == 0) continue;
        if (prev >= 0 && i - prev > 1)
            for (int j = prev + 1; j < i; ++j) {
                const float t = static_cast<float>(j - prev) / static_cast<float>(i - prev);
                out[j] = out[prev] * (1.f - t) + out[i] * t;
            }
        else if (prev < 0)
            for (int j = 0; j < i; ++j) out[j] = out[i];   // leading empties: hold the first filled band
        prev = i;
    }
    if (prev >= 0)
        for (int j = prev + 1; j < nbands; ++j) out[j] = out[prev];   // trailing empties: hold the last
}

}  // namespace vivid::audio
