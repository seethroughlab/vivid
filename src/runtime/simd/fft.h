#pragma once

// Shared power-of-two in-place FFT.
// Runtime-internal — MUST NOT appear in src/operator_api/.
//
// Split-complex is the native layout (matches Accelerate's DSPSplitComplex).
// Callers that store data as interleaved {re, im} use the *_interleaved
// adapters, which deinterleave into the provided FftScratch, run the split
// FFT, and reinterleave.
//
// Scaling convention matches the prior in-tree implementations:
//   fft_forward — no scaling
//   fft_inverse — applies 1/N to both real and imag
//
// Dispatch: if Accelerate is enabled AND the cache has a plan for the given
// size, vDSP_fft_zip is used; otherwise a shared scalar Cooley-Tukey runs.
// Callers should FftPlanCache::reserve(n) for every FFT size they'll hit,
// during plan rebuild (not on the audio thread).

#include "runtime/simd/simd_config.h"

#include <cstdint>
#include <vector>

namespace vivid::simd {

class FftPlanCache {
public:
    FftPlanCache() = default;
    ~FftPlanCache();

    FftPlanCache(const FftPlanCache&) = delete;
    FftPlanCache& operator=(const FftPlanCache&) = delete;

    // Ensure a plan exists for the given power-of-two size. No-op on
    // non-Apple or if the plan already exists. Idempotent.
    void reserve(uint32_t fft_size);

    // Release every cached plan. Called by the destructor and by reset().
    void clear();

    // Whether a plan is cached for the given size. Non-Apple builds always
    // return false.
    bool has(uint32_t fft_size) const;

#if VIVID_ACCELERATE_ENABLED
    // Internal accessor for the dispatchers in fft.cpp. Returns nullptr if
    // no plan exists for this log2n.
    FFTSetup get(int log2n) const;
#endif

private:
#if VIVID_ACCELERATE_ENABLED
    struct Entry {
        int log2n;
        FFTSetup setup;
    };
    std::vector<Entry> setups_;
#endif
};

// Working buffers for the interleaved adapter (and any caller that wants a
// reusable split-complex scratch pair). Resized on demand; allocations happen
// the first time a size is seen.
struct FftScratch {
    std::vector<float> real;
    std::vector<float> imag;

    void ensure(uint32_t n);
};

// Split-complex in-place FFT. real/imag must each point to at least `n`
// floats. `n` must be a power of two >= 2. `cache` may be empty — the call
// still succeeds via the scalar fallback, just without Accelerate's speed.
void fft_forward(float* real, float* imag, uint32_t n, const FftPlanCache& cache);
void fft_inverse(float* real, float* imag, uint32_t n, const FftPlanCache& cache);

// Always-scalar variants. Used by backend-parity tests and by callers that
// need the scalar reference explicitly (e.g. Engine::process with
// Backend::Scalar forced by the caller).
void fft_forward_scalar(float* real, float* imag, uint32_t n);
void fft_inverse_scalar(float* real, float* imag, uint32_t n);

// Interleaved {re, im} adapter. Used by callers (e.g. convolution_reverb)
// that store frequency-domain data as an array of {re, im} structs rather
// than split arrays. The scratch buffers are resized on demand.
struct ComplexPair {
    float re;
    float im;
};

void fft_forward_interleaved(ComplexPair* data, uint32_t n,
                             const FftPlanCache& cache, FftScratch& scratch);
void fft_inverse_interleaved(ComplexPair* data, uint32_t n,
                             const FftPlanCache& cache, FftScratch& scratch);

// Always-scalar interleaved variants. Same role as the split-complex scalar
// overloads above.
void fft_forward_interleaved_scalar(ComplexPair* data, uint32_t n, FftScratch& scratch);
void fft_inverse_interleaved_scalar(ComplexPair* data, uint32_t n, FftScratch& scratch);

} // namespace vivid::simd
