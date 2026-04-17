#pragma once

// Relaxed-correctness "is this operator broken?" floor for audio output.
// Used across optimization passes where we intentionally relax tight
// scalar/Accelerate bit-parity in favor of "sounds like it's not broken."
//
// Header-only and dependency-free: no runtime-lib includes, no
// OperatorLoader, just <cmath>. Drop into any test that needs to assert the
// output isn't NaN/Inf, isn't silent when it shouldn't be, isn't clipping
// insanely, and doesn't have a DC drift.
//
// Typical usage:
//
//   vivid::audio_smoke::Spec spec{};               // defaults
//   spec.max_peak = 4.0f;                          // tighten for effects
//   const auto result = vivid::audio_smoke::check(
//       output.data(), frames, channels, spec);
//   if (!result.ok) {
//       std::fprintf(stderr, "smoke failed: %s\n", result.reason);
//   }
//
// For multi-block operators, use check_block() in a loop and call
// finish(accum) once to get aggregate stats.

#include <cmath>
#include <cstdint>

namespace vivid::audio_smoke {

struct Spec {
    // Minimum RMS across the inspected samples. A processor that's meant to
    // produce audible output must clear this. Set allow_silent=true for
    // operators that can legitimately go silent (ADSR in release, Sampler
    // with no voice triggered, etc).
    float min_rms = 1.0e-4f;

    // Absolute peak ceiling. Anything above this is considered blown up. The
    // default 8.0 is generous — it catches clear instability (blown reverb
    // feedback, integer overflow, runaway filter) without flagging a modest
    // 2x amplitude gain from an effect.
    float max_peak = 8.0f;

    // DC component ceiling, expressed as |mean| / max(peak, eps). Catches a
    // broken accumulator or asymmetric nonlinearity; the default is
    // deliberately generous (0.3) because short-window audio — a partial
    // sine period, a transient decay — can legitimately show 10-20% DC.
    float max_dc_ratio = 0.3f;

    // If true, silent output (RMS below min_rms) is allowed. Does not
    // disable the finite / peak / DC checks.
    bool allow_silent = false;
};

struct Result {
    bool ok = true;
    float rms = 0.0f;
    float peak = 0.0f;
    float dc = 0.0f;          // |mean|
    float dc_ratio = 0.0f;    // |mean| / max(peak, eps)
    uint32_t nan_count = 0;
    uint32_t inf_count = 0;
    const char* reason = nullptr;
};

// Incremental accumulator — lets callers walk many blocks without copying
// everything to one big buffer first.
struct Accum {
    double sum_sq = 0.0;
    double sum = 0.0;
    float peak = 0.0f;
    uint32_t n = 0;
    uint32_t nan_count = 0;
    uint32_t inf_count = 0;
};

inline void accumulate(Accum& a, const float* samples, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        const float x = samples[i];
        if (std::isnan(x)) { ++a.nan_count; continue; }
        if (std::isinf(x)) { ++a.inf_count; continue; }
        const float ax = std::fabs(x);
        if (ax > a.peak) a.peak = ax;
        a.sum += static_cast<double>(x);
        a.sum_sq += static_cast<double>(x) * static_cast<double>(x);
        ++a.n;
    }
}

inline Result finalize(const Accum& a, const Spec& spec) {
    Result r{};
    r.nan_count = a.nan_count;
    r.inf_count = a.inf_count;
    r.peak = a.peak;
    if (a.n > 0) {
        r.rms = static_cast<float>(std::sqrt(a.sum_sq / static_cast<double>(a.n)));
        r.dc = static_cast<float>(std::fabs(a.sum / static_cast<double>(a.n)));
        const float denom = a.peak > 1.0e-6f ? a.peak : 1.0e-6f;
        r.dc_ratio = r.dc / denom;
    }

    if (r.nan_count > 0) { r.ok = false; r.reason = "NaN sample(s) in output"; return r; }
    if (r.inf_count > 0) { r.ok = false; r.reason = "Inf sample(s) in output"; return r; }
    if (r.peak > spec.max_peak) { r.ok = false; r.reason = "peak exceeded ceiling"; return r; }
    if (r.dc_ratio > spec.max_dc_ratio) { r.ok = false; r.reason = "DC offset too large"; return r; }
    if (!spec.allow_silent && r.rms < spec.min_rms) { r.ok = false; r.reason = "RMS below silence floor"; return r; }
    return r;
}

// One-shot check over an interleaved (or planar, doesn't matter) buffer.
inline Result check(const float* samples, uint32_t frames, uint32_t channels, const Spec& spec = {}) {
    Accum a{};
    accumulate(a, samples, frames * channels);
    return finalize(a, spec);
}

// Block-at-a-time variant. Returns after every accumulation; call finalize()
// on the same Accum once all blocks are fed.
inline void check_block(Accum& a, const float* samples, uint32_t frames, uint32_t channels) {
    accumulate(a, samples, frames * channels);
}

} // namespace vivid::audio_smoke
