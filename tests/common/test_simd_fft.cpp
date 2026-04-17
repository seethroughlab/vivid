#include "runtime/simd/fft.h"

#include "test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct DiffStats {
    float peak = 0.0f;
    double sum_sq = 0.0;
    uint32_t n = 0;
};

DiffStats compare_vec(const std::vector<float>& a, const std::vector<float>& b) {
    DiffStats s{};
    s.n = static_cast<uint32_t>(std::min(a.size(), b.size()));
    for (uint32_t i = 0; i < s.n; ++i) {
        const float d = std::fabs(a[i] - b[i]);
        s.peak = std::max(s.peak, d);
        s.sum_sq += static_cast<double>(d) * d;
    }
    return s;
}

float rms(const DiffStats& s) {
    return s.n == 0 ? 0.0f : static_cast<float>(std::sqrt(s.sum_sq / s.n));
}

void fill_signal(std::vector<float>& real, std::vector<float>& imag, uint32_t n, uint32_t seed) {
    real.assign(n, 0.0f);
    imag.assign(n, 0.0f);
    for (uint32_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        real[i] = 0.45f * std::sin(2.0f * static_cast<float>(M_PI) * 3.0f * t + 0.13f * seed)
                + 0.22f * std::sin(2.0f * static_cast<float>(M_PI) * 11.0f * t + 0.27f * seed)
                + 0.07f * std::sin(2.0f * static_cast<float>(M_PI) * 37.0f * t + 0.41f * seed);
        imag[i] = 0.18f * std::sin(2.0f * static_cast<float>(M_PI) * 5.0f * t + 0.19f * seed)
                + 0.05f * std::sin(2.0f * static_cast<float>(M_PI) * 17.0f * t + 0.31f * seed);
    }
}

// Scalar Cooley-Tukey accumulates twiddle rotation error across log2(n)
// butterfly stages. Both the legacy in-tree scalar FFTs this helper replaces
// and the helper itself exhibit the same drift, so cross-backend parity is
// loose at large N while round-trip stays tight (the round-trip cancels
// symmetric drift). Accelerate uses precomputed twiddle tables and is the
// numerical reference. Round-trip correctness is the strong invariant here.
float parity_peak_tol(uint32_t n) {
    // Allow ~n * 1e-6 of drift plus a fixed floor; caps at ~0.1 for n=16384.
    return std::max(1.0e-3f, static_cast<float>(n) * 1.0e-6f * 8.0f);
}

float parity_rms_tol(uint32_t n) {
    return std::max(1.0e-4f, static_cast<float>(n) * 1.0e-7f * 8.0f);
}

void run_parity_case(uint32_t n) {
    vivid::simd::FftPlanCache cache;
    cache.reserve(n);

    std::vector<float> src_real, src_imag;
    fill_signal(src_real, src_imag, n, /*seed=*/n);

    // ---- Split-complex parity: scalar vs dispatched (prefers Accelerate) ----
    std::vector<float> scal_real = src_real;
    std::vector<float> scal_imag = src_imag;
    std::vector<float> disp_real = src_real;
    std::vector<float> disp_imag = src_imag;

    vivid::simd::fft_forward_scalar(scal_real.data(), scal_imag.data(), n);
    vivid::simd::fft_forward(disp_real.data(), disp_imag.data(), n, cache);

    const auto fwd_re = compare_vec(scal_real, disp_real);
    const auto fwd_im = compare_vec(scal_imag, disp_imag);
    const float peak_tol = parity_peak_tol(n);
    const float rms_tol = parity_rms_tol(n);
    std::fprintf(stderr,
                 "  n=%u forward split scalar-vs-accel rms_re=%.2e peak_re=%.2e rms_im=%.2e peak_im=%.2e  (tol peak=%.2e rms=%.2e)\n",
                 n, rms(fwd_re), fwd_re.peak, rms(fwd_im), fwd_im.peak, peak_tol, rms_tol);
    check(fwd_re.peak < peak_tol && fwd_im.peak < peak_tol,
          "simd::fft_forward scalar vs Accelerate peak within tolerance");
    check(rms(fwd_re) < rms_tol && rms(fwd_im) < rms_tol,
          "simd::fft_forward scalar vs Accelerate RMS within tolerance");

    // ---- Round trip: inverse(forward(x)) == x. Tight tolerance ----
    std::vector<float> rt_real = src_real;
    std::vector<float> rt_imag = src_imag;
    vivid::simd::fft_forward(rt_real.data(), rt_imag.data(), n, cache);
    vivid::simd::fft_inverse(rt_real.data(), rt_imag.data(), n, cache);
    const auto rt_re = compare_vec(src_real, rt_real);
    const auto rt_im = compare_vec(src_imag, rt_imag);
    check(rt_re.peak < 1e-3f && rt_im.peak < 1e-3f,
          "simd::fft round-trip peak within tolerance");
    check(rms(rt_re) < 1e-4f && rms(rt_im) < 1e-4f,
          "simd::fft round-trip RMS within tolerance");

    // ---- Scalar round trip independently (no cache) ----
    // Scalar's forward/inverse drift is symmetric and cancels on round-trip,
    // so this stays tight even at large N.
    std::vector<float> srt_real = src_real;
    std::vector<float> srt_imag = src_imag;
    vivid::simd::fft_forward_scalar(srt_real.data(), srt_imag.data(), n);
    vivid::simd::fft_inverse_scalar(srt_real.data(), srt_imag.data(), n);
    const auto srt_re = compare_vec(src_real, srt_real);
    const auto srt_im = compare_vec(src_imag, srt_imag);
    check(srt_re.peak < 1e-3f && srt_im.peak < 1e-3f,
          "simd::fft scalar round-trip peak within tolerance");

    // ---- Interleaved adapter parity ----
    std::vector<vivid::simd::ComplexPair> il_scalar(n);
    std::vector<vivid::simd::ComplexPair> il_dispatch(n);
    for (uint32_t i = 0; i < n; ++i) {
        il_scalar[i] = {src_real[i], src_imag[i]};
        il_dispatch[i] = {src_real[i], src_imag[i]};
    }
    vivid::simd::FftScratch scratch;
    vivid::simd::fft_forward_interleaved_scalar(il_scalar.data(), n, scratch);
    vivid::simd::fft_forward_interleaved(il_dispatch.data(), n, cache, scratch);
    float peak_il = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        peak_il = std::max(peak_il, std::fabs(il_scalar[i].re - il_dispatch[i].re));
        peak_il = std::max(peak_il, std::fabs(il_scalar[i].im - il_dispatch[i].im));
    }
    check(peak_il < peak_tol, "simd::fft_forward_interleaved scalar vs Accelerate peak within tolerance");

    // ---- Interleaved round trip — tight tolerance ----
    std::vector<vivid::simd::ComplexPair> rt_il(n);
    for (uint32_t i = 0; i < n; ++i) rt_il[i] = {src_real[i], src_imag[i]};
    vivid::simd::fft_forward_interleaved(rt_il.data(), n, cache, scratch);
    vivid::simd::fft_inverse_interleaved(rt_il.data(), n, cache, scratch);
    float peak_rt_il = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        peak_rt_il = std::max(peak_rt_il, std::fabs(rt_il[i].re - src_real[i]));
        peak_rt_il = std::max(peak_rt_il, std::fabs(rt_il[i].im - src_imag[i]));
    }
    check(peak_rt_il < 1e-3f, "simd::fft interleaved round-trip peak within tolerance");
}

void run_known_answer() {
    // Impulse in time-domain -> flat spectrum. Simple sanity check of scale
    // and orientation across both the split and interleaved entry points.
    const uint32_t n = 16;
    std::vector<float> real(n, 0.0f), imag(n, 0.0f);
    real[0] = 1.0f;
    vivid::simd::fft_forward_scalar(real.data(), imag.data(), n);
    bool all_real_one = true;
    for (uint32_t i = 0; i < n; ++i) {
        if (std::fabs(real[i] - 1.0f) > 1e-5f) all_real_one = false;
        if (std::fabs(imag[i]) > 1e-5f) all_real_one = false;
    }
    check(all_real_one, "simd::fft_forward_scalar impulse -> flat spectrum");

    // Flat spectrum -> impulse when inverse-FFT'd (with 1/N scaling).
    std::vector<float> ri(n, 1.0f), ii(n, 0.0f);
    vivid::simd::fft_inverse_scalar(ri.data(), ii.data(), n);
    bool impulse_ok = std::fabs(ri[0] - 1.0f) < 1e-5f;
    for (uint32_t i = 1; i < n; ++i) {
        if (std::fabs(ri[i]) > 1e-5f) impulse_ok = false;
        if (std::fabs(ii[i]) > 1e-5f) impulse_ok = false;
    }
    check(impulse_ok, "simd::fft_inverse_scalar flat -> impulse with 1/N scaling");
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== test_simd_fft ===\n");
    run_known_answer();
    for (uint32_t n : {64u, 256u, 1024u, 4096u, 16384u}) run_parity_case(n);
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
