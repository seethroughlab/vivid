#pragma once
// A tiny self-contained real-signal spectrum helper for the audio→visual bridge: an iterative
// radix-2 Cooley-Tukey FFT + a reduction to log-spaced magnitude bands. Header-only, no allocation
// in the hot path beyond the caller's scratch, no external deps. Used FRAME-SIDE (UI thread) on a
// snapshot of a track's recent samples, so the audio thread only pays for a cheap ring copy.
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
    // average per band, then a gentle compression into a visual 0..1 (the raw magnitudes are small).
    for (int i = 0; i < nbands; ++i) {
        if (counts[i] > 0) out[i] /= static_cast<float>(counts[i]);
        out[i] = std::min(1.f, out[i] * 8.f);   // display gain (matches the *5/*8/*12 band scaling style)
    }
}

}  // namespace vivid::audio
