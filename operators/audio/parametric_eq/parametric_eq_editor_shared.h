#pragma once
// Pure-logic helpers for the ParametricEQ editor — log-freq / dB axis
// mappings, biquad coefficient computation, single-band + composite
// magnitude response, and band node hit-testing. All free functions;
// no dependency on the operator class so tests can exercise without
// a live runtime.

#include <cstddef>
#include <cstdint>

namespace vivid::parametric_eq_editor {

// --- Param indices (must match ParametricEQ::collect_params order) --------

inline constexpr int kMaxBands            = 4;
inline constexpr int kBandCountParamIndex = 0;

// Per band N (0..3): params land at 1 + N*4 .. 4 + N*4.
inline constexpr int freq_param_index(int band) { return 1 + band * 4; }
inline constexpr int gain_param_index(int band) { return 2 + band * 4; }
inline constexpr int q_param_index   (int band) { return 3 + band * 4; }
inline constexpr int type_param_index(int band) { return 4 + band * 4; }

// --- Axis ranges (immutable across the editor lifetime) -------------------

inline constexpr float kMinFreqHz =    20.0f;
inline constexpr float kMaxFreqHz = 20000.0f;
inline constexpr float kMinGainDb = -24.0f;
inline constexpr float kMaxGainDb = +24.0f;

// --- Band type IDs --------------------------------------------------------

enum BandType : int {
    kPeak      = 0,
    kLowShelf  = 1,
    kHighShelf = 2,
    kLowPass   = 3,
    kHighPass  = 4,
};
inline constexpr int kBandTypeCount = 5;

const char* band_type_name(int type);

// --- Axis mapping ---------------------------------------------------------

// Log-frequency: hz ∈ [kMinFreqHz, kMaxFreqHz] → [0, 1] via log2.
// Values outside the range are clamped.
float freq_to_fraction(float hz);
float fraction_to_freq(float frac);

// Linear dB: db ∈ [kMinGainDb, kMaxGainDb] → [0, 1] with 0 dB at 0.5.
float db_to_fraction(float db);
float fraction_to_db(float frac);

// --- Biquad coefficients + magnitude response -----------------------------

struct BiquadCoeffs {
    float b0, b1, b2, a1, a2;  // normalized by a0
};

// Mirror of ParametricEQ::compute_coeffs — same formulas, lifted into
// free-function form so the editor shared module owns them.
BiquadCoeffs compute_coeffs(int type, float freq_hz, float gain_db,
                            float Q, float sample_rate);

// |H(e^jω)| in linear magnitude for a single biquad, evaluated at
// eval_hz. Returns 0 when the band is inactive (freq/sample-rate bad).
float band_magnitude(const BiquadCoeffs& c, float eval_hz, float sample_rate);

// Single-band magnitude in dB. Convenience: compute_coeffs + 20*log10.
// Returns kMinGainDb for magnitudes at or below 1e-6 (silence floor).
float band_magnitude_db(int type, float freq_hz, float gain_db,
                        float Q, float sample_rate, float eval_hz);

// Composite magnitude in dB across the first `active_band_count`
// bands. Cascaded biquads sum in dB.
float composite_magnitude_db(const int* types, const float* freqs,
                             const float* gains, const float* Qs,
                             int active_band_count,
                             float sample_rate, float eval_hz);

// --- Band node geometry ---------------------------------------------------

struct NodePoint { float x, y; };

// Where a band's node sits on the plane [plane_x..+w, plane_y..+h]
// given the band's freq + gain_db. Origin of the plane is top-left;
// lower-y = higher gain.
NodePoint band_node_position(float plane_x, float plane_y,
                             float plane_w, float plane_h,
                             float freq_hz, float gain_db);

// Hit-test: returns the nearest active band whose node is within
// `hit_radius_px` of (mouse_x, mouse_y), or -1 if none.
int hit_test_band(float plane_x, float plane_y,
                  float plane_w, float plane_h,
                  float mouse_x, float mouse_y,
                  const float* freqs, const float* gains,
                  int active_band_count, float hit_radius_px);

} // namespace vivid::parametric_eq_editor
