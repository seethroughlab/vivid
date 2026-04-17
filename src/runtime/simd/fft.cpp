#include "runtime/simd/fft.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vivid::simd {
namespace {

inline int log2_size_of(uint32_t n) {
    int log2n = 0;
    while (n > 1) { n >>= 1u; ++log2n; }
    return log2n;
}

// Radix-2, in-place, split-complex Cooley-Tukey. Mirrors the previous
// per-module scalar implementations (spectral_freeze_dsp and
// convolution_reverb_dsp) with identical butterfly math. The inverse
// direction is expressed by flipping the twiddle-angle sign rather than via
// the conjugate trick; 1/N scaling is applied once at the end.
void fft_scalar_split(float* real, float* imag, uint32_t n, bool inverse) {
    const int log2n = log2_size_of(n);

    for (uint32_t i = 0; i < n; ++i) {
        uint32_t j = 0;
        for (int b = 0; b < log2n; ++b)
            j |= ((i >> b) & 1u) << (log2n - 1 - b);
        if (j > i) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
    }

    const float sign = inverse ? 2.0f : -2.0f;
    for (int s = 1; s <= log2n; ++s) {
        const uint32_t m = 1u << s;
        const float wm_re = std::cos(sign * static_cast<float>(M_PI) / static_cast<float>(m));
        const float wm_im = std::sin(sign * static_cast<float>(M_PI) / static_cast<float>(m));
        for (uint32_t k = 0; k < n; k += m) {
            float w_re = 1.0f;
            float w_im = 0.0f;
            for (uint32_t j = 0; j < m / 2; ++j) {
                const uint32_t t_idx = k + j + m / 2;
                const uint32_t u_idx = k + j;
                const float t_re = w_re * real[t_idx] - w_im * imag[t_idx];
                const float t_im = w_re * imag[t_idx] + w_im * real[t_idx];
                real[t_idx] = real[u_idx] - t_re;
                imag[t_idx] = imag[u_idx] - t_im;
                real[u_idx] += t_re;
                imag[u_idx] += t_im;
                const float new_w_re = w_re * wm_re - w_im * wm_im;
                const float new_w_im = w_re * wm_im + w_im * wm_re;
                w_re = new_w_re;
                w_im = new_w_im;
            }
        }
    }

    if (inverse) {
        const float inv_n = 1.0f / static_cast<float>(n);
        for (uint32_t i = 0; i < n; ++i) {
            real[i] *= inv_n;
            imag[i] *= inv_n;
        }
    }
}

} // namespace

FftPlanCache::~FftPlanCache() {
    clear();
}

void FftPlanCache::reserve(uint32_t fft_size) {
#if VIVID_ACCELERATE_ENABLED
    if (fft_size < 2) return;
    const int log2n = log2_size_of(fft_size);
    for (const auto& e : setups_) if (e.log2n == log2n) return;
    FFTSetup s = vDSP_create_fftsetup(static_cast<vDSP_Length>(log2n), kFFTRadix2);
    if (s) setups_.push_back(Entry{log2n, s});
#else
    (void)fft_size;
#endif
}

void FftPlanCache::clear() {
#if VIVID_ACCELERATE_ENABLED
    for (auto& e : setups_) {
        if (e.setup) vDSP_destroy_fftsetup(e.setup);
    }
    setups_.clear();
#endif
}

bool FftPlanCache::has(uint32_t fft_size) const {
#if VIVID_ACCELERATE_ENABLED
    if (fft_size < 2) return false;
    const int log2n = log2_size_of(fft_size);
    for (const auto& e : setups_) if (e.log2n == log2n) return true;
#else
    (void)fft_size;
#endif
    return false;
}

#if VIVID_ACCELERATE_ENABLED
FFTSetup FftPlanCache::get(int log2n) const {
    for (const auto& e : setups_) if (e.log2n == log2n) return e.setup;
    return nullptr;
}
#endif

void FftScratch::ensure(uint32_t n) {
    if (real.size() < n) real.assign(n, 0.0f);
    if (imag.size() < n) imag.assign(n, 0.0f);
}

void fft_forward(float* real, float* imag, uint32_t n, const FftPlanCache& cache) {
#if VIVID_ACCELERATE_ENABLED
    const int log2n = log2_size_of(n);
    if (FFTSetup setup = cache.get(log2n)) {
        DSPSplitComplex split{real, imag};
        vDSP_fft_zip(setup, &split, 1, static_cast<vDSP_Length>(log2n), FFT_FORWARD);
        return;
    }
#else
    (void)cache;
#endif
    fft_scalar_split(real, imag, n, false);
}

void fft_inverse(float* real, float* imag, uint32_t n, const FftPlanCache& cache) {
#if VIVID_ACCELERATE_ENABLED
    const int log2n = log2_size_of(n);
    if (FFTSetup setup = cache.get(log2n)) {
        DSPSplitComplex split{real, imag};
        vDSP_fft_zip(setup, &split, 1, static_cast<vDSP_Length>(log2n), FFT_INVERSE);
        const float inv_n = 1.0f / static_cast<float>(n);
        vDSP_vsmul(real, 1, &inv_n, real, 1, n);
        vDSP_vsmul(imag, 1, &inv_n, imag, 1, n);
        return;
    }
#else
    (void)cache;
#endif
    fft_scalar_split(real, imag, n, true);
}

void fft_forward_scalar(float* real, float* imag, uint32_t n) {
    fft_scalar_split(real, imag, n, false);
}

void fft_inverse_scalar(float* real, float* imag, uint32_t n) {
    fft_scalar_split(real, imag, n, true);
}

void fft_forward_interleaved(ComplexPair* data, uint32_t n,
                             const FftPlanCache& cache, FftScratch& scratch) {
    scratch.ensure(n);
    for (uint32_t i = 0; i < n; ++i) {
        scratch.real[i] = data[i].re;
        scratch.imag[i] = data[i].im;
    }
    fft_forward(scratch.real.data(), scratch.imag.data(), n, cache);
    for (uint32_t i = 0; i < n; ++i) {
        data[i].re = scratch.real[i];
        data[i].im = scratch.imag[i];
    }
}

void fft_inverse_interleaved(ComplexPair* data, uint32_t n,
                             const FftPlanCache& cache, FftScratch& scratch) {
    scratch.ensure(n);
    for (uint32_t i = 0; i < n; ++i) {
        scratch.real[i] = data[i].re;
        scratch.imag[i] = data[i].im;
    }
    fft_inverse(scratch.real.data(), scratch.imag.data(), n, cache);
    for (uint32_t i = 0; i < n; ++i) {
        data[i].re = scratch.real[i];
        data[i].im = scratch.imag[i];
    }
}

void fft_forward_interleaved_scalar(ComplexPair* data, uint32_t n, FftScratch& scratch) {
    scratch.ensure(n);
    for (uint32_t i = 0; i < n; ++i) {
        scratch.real[i] = data[i].re;
        scratch.imag[i] = data[i].im;
    }
    fft_scalar_split(scratch.real.data(), scratch.imag.data(), n, false);
    for (uint32_t i = 0; i < n; ++i) {
        data[i].re = scratch.real[i];
        data[i].im = scratch.imag[i];
    }
}

void fft_inverse_interleaved_scalar(ComplexPair* data, uint32_t n, FftScratch& scratch) {
    scratch.ensure(n);
    for (uint32_t i = 0; i < n; ++i) {
        scratch.real[i] = data[i].re;
        scratch.imag[i] = data[i].im;
    }
    fft_scalar_split(scratch.real.data(), scratch.imag.data(), n, true);
    for (uint32_t i = 0; i < n; ++i) {
        data[i].re = scratch.real[i];
        data[i].im = scratch.imag[i];
    }
}

} // namespace vivid::simd
