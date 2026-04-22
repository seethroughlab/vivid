// Pure-logic tests for parametric_eq_editor_shared: axis mappings,
// biquad coefficient + magnitude math, band-node geometry, hit-test.
// These power the editor's plane rendering and input handling.

#include "parametric_eq_editor_shared.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "test_helpers.h"

namespace pe = ::vivid::parametric_eq_editor;

namespace {

bool approx(float a, float b, float tol = 1e-3f) {
    return std::fabs(a - b) < tol;
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: ParametricEQ editor helpers ===\n\n");

    // --- Param index constants match ParametricEQ::collect_params order ---
    {
        check(pe::kBandCountParamIndex == 0, "band_count at index 0");
        check(pe::freq_param_index(0) == 1, "freq_1 at index 1");
        check(pe::gain_param_index(0) == 2, "gain_1 at index 2");
        check(pe::q_param_index(0)    == 3, "q_1    at index 3");
        check(pe::type_param_index(0) == 4, "type_1 at index 4");
        check(pe::freq_param_index(3) == 13, "freq_4 at index 13");
        check(pe::type_param_index(3) == 16, "type_4 at index 16");
    }

    // --- Axis ranges and endpoints -----------------------------------------
    {
        check(approx(pe::freq_to_fraction(pe::kMinFreqHz), 0.0f),
              "freq_to_fraction(20Hz) == 0");
        check(approx(pe::freq_to_fraction(pe::kMaxFreqHz), 1.0f),
              "freq_to_fraction(20kHz) == 1");
        check(approx(pe::fraction_to_freq(0.0f), pe::kMinFreqHz),
              "fraction_to_freq(0) == 20Hz");
        check(std::fabs(pe::fraction_to_freq(1.0f) - pe::kMaxFreqHz) <
                  pe::kMaxFreqHz * 1e-3f,
              "fraction_to_freq(1) ≈ 20kHz (relative)");

        // Clamping outside the range.
        check(approx(pe::freq_to_fraction(5.0f),     0.0f),
              "freq_to_fraction below range clamps to 0");
        check(approx(pe::freq_to_fraction(100000.0f), 1.0f),
              "freq_to_fraction above range clamps to 1");

        // dB mapping: 0 dB is midpoint because range is symmetric.
        check(approx(pe::db_to_fraction(-24.0f), 0.0f),
              "db_to_fraction(-24) == 0");
        check(approx(pe::db_to_fraction(0.0f),   0.5f),
              "db_to_fraction(0) == 0.5");
        check(approx(pe::db_to_fraction(+24.0f), 1.0f),
              "db_to_fraction(+24) == 1");
        check(approx(pe::fraction_to_db(0.5f), 0.0f),
              "fraction_to_db(0.5) == 0 dB");
    }

    // --- Log frequency: a full octave above 20Hz halfway to 40Hz ----------
    {
        // log2(40) - log2(20) = 1 octave out of log2(20000)-log2(20) ≈ 9.966
        const float expected = 1.0f / (std::log2(pe::kMaxFreqHz) -
                                       std::log2(pe::kMinFreqHz));
        check(approx(pe::freq_to_fraction(40.0f), expected),
              "one octave above min maps to 1/log2-range fraction");
    }

    // --- compute_coeffs: Peak at 1 kHz, 0 dB, Q=1, 48kHz is identity-ish ---
    {
        auto c = pe::compute_coeffs(pe::kPeak, 1000.0f, 0.0f, 1.0f, 48000.0f);
        // For gain_db == 0, A==1 → b == a (modulo norm), so |H| = 1 everywhere.
        const float mag = pe::band_magnitude(c, 1000.0f, 48000.0f);
        check(approx(mag, 1.0f, 1e-3f),
              "peak band at 0 dB has unity magnitude (identity biquad)");

        const float db = pe::band_magnitude_db(pe::kPeak, 1000.0f, 0.0f, 1.0f,
                                               48000.0f, 1000.0f);
        check(std::fabs(db) < 0.1f,
              "peak band at 0 dB has ~0 dB response");
    }

    // --- A +6 dB peak at 1 kHz peaks at ~+6 dB at 1 kHz -------------------
    {
        const float db = pe::band_magnitude_db(pe::kPeak, 1000.0f, 6.0f, 1.0f,
                                               48000.0f, 1000.0f);
        check(std::fabs(db - 6.0f) < 0.5f,
              "peak +6 dB at center frequency ≈ +6 dB");
    }

    // --- Highpass has ~-3 dB near corner and -inf below ------------------
    {
        const float db_at_corner = pe::band_magnitude_db(
            pe::kHighPass, 1000.0f, 0.0f, 0.707f, 48000.0f, 1000.0f);
        check(std::fabs(db_at_corner - (-3.0f)) < 1.5f,
              "highpass Butterworth ≈ -3 dB at corner");

        const float db_deep = pe::band_magnitude_db(
            pe::kHighPass, 1000.0f, 0.0f, 0.707f, 48000.0f, 50.0f);
        check(db_deep < -24.0f,
              "highpass 2 decades below corner strongly attenuated");
    }

    // --- Composite response: inactive bands contribute zero dB ------------
    {
        int types[]   = {pe::kPeak, pe::kPeak, pe::kPeak, pe::kPeak};
        float freqs[] = {1000.0f, 1000.0f, 1000.0f, 1000.0f};
        float gains[] = {6.0f, 6.0f, 6.0f, 6.0f};
        float Qs[]    = {1.0f, 1.0f, 1.0f, 1.0f};

        const float db_one = pe::composite_magnitude_db(
            types, freqs, gains, Qs, /*active=*/1, 48000.0f, 1000.0f);
        const float db_four = pe::composite_magnitude_db(
            types, freqs, gains, Qs, /*active=*/4, 48000.0f, 1000.0f);

        check(std::fabs(db_one - 6.0f) < 0.5f,
              "1 active band with +6 dB peak gives +6 dB at center");
        check(std::fabs(db_four - 24.0f) < 2.0f,
              "4 active stacked +6 dB peaks give ≈ +24 dB at center");
    }

    // --- Node geometry: log-x, inverted-y dB -----------------------------
    {
        // Plane 100x200 anchored at (10, 20). 1 kHz at 0 dB:
        auto p = pe::band_node_position(10.0f, 20.0f, 100.0f, 200.0f,
                                        1000.0f, 0.0f);
        const float expect_x = 10.0f + pe::freq_to_fraction(1000.0f) * 100.0f;
        const float expect_y = 20.0f + 0.5f * 200.0f;  // 0 dB at midpoint
        check(approx(p.x, expect_x, 0.5f), "node x at 1 kHz matches log mapping");
        check(approx(p.y, expect_y, 0.5f), "node y at 0 dB is plane midline");
    }

    // --- Hit test: finds the nearest band within radius -----------------
    {
        float freqs[] = {1000.0f, 2000.0f, 3000.0f, 4000.0f};
        float gains[] = {0.0f,    0.0f,    0.0f,    0.0f};
        const float px = 0.0f, py = 0.0f, pw = 1000.0f, ph = 400.0f;

        auto n0 = pe::band_node_position(px, py, pw, ph, freqs[0], gains[0]);
        const int hit = pe::hit_test_band(px, py, pw, ph,
                                          n0.x + 2.0f, n0.y - 3.0f,
                                          freqs, gains, 4, 14.0f);
        check(hit == 0, "hit-test near band 0 returns 0");

        const int miss = pe::hit_test_band(px, py, pw, ph,
                                           px - 50.0f, py - 50.0f,
                                           freqs, gains, 4, 14.0f);
        check(miss == -1, "hit-test far from any node returns -1");

        // Inactive bands aren't hit.
        const int inactive_miss = pe::hit_test_band(
            px, py, pw, ph,
            pe::band_node_position(px, py, pw, ph, freqs[3], gains[3]).x,
            pe::band_node_position(px, py, pw, ph, freqs[3], gains[3]).y,
            freqs, gains, /*active=*/2, 14.0f);
        check(inactive_miss == -1,
              "hit-test skips bands beyond active_band_count");
    }

    // --- Band type naming ----------------------------------------------------
    {
        check(std::strcmp(pe::band_type_name(pe::kPeak),      "Peak")       == 0,
              "band_type_name(Peak)");
        check(std::strcmp(pe::band_type_name(pe::kHighShelf), "High Shelf") == 0,
              "band_type_name(High Shelf)");
        check(std::strcmp(pe::band_type_name(-1), "?") == 0,
              "band_type_name guards out-of-range");
    }

    return failures == 0 ? 0 : 1;
}
