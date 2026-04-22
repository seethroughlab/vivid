#pragma once
// PatternSeq editor helpers: param-index encoding, range constants,
// and quick-fill generators. Pure-logic — no VividEditorContext.

#include <cstdint>
#include <string>

namespace vivid::pattern_seq_editor {

inline constexpr int   kMaxSteps = 16;
inline constexpr float kValueMin = -10000.0f;
inline constexpr float kValueMax =  10000.0f;

// Descriptor-order indices must stay in sync with
// PatternSeqCore::collect_params(): steps at 0, val_N at 1+N,
// rate at 17, gate_length at 18, probability at 19.
inline constexpr int kStepsIndex       = 0;
inline constexpr int kValueBase        = 1;   // val_0 .. val_15 at 1..16
inline constexpr int kRateIndex        = 17;
inline constexpr int kGateLengthIndex  = 18;
inline constexpr int kProbabilityIndex = 19;
inline constexpr int kStepOutputIndex  = 3;   // output_values[3] = step

inline int param_index_for(int step) { return kValueBase + step; }

// Canonical param name ("val_0" .. "val_15").
std::string param_name_for(int step);

// --- Quick-fill generators ---
//
// Write `num_steps` values starting at out[0]. Callers pass
// out[kMaxSteps]. Values past `num_steps` are left untouched so
// callers can leave inactive cells alone.

void fill_ramp_up   (float* out, int num_steps);   // -max .. +max
void fill_ramp_down (float* out, int num_steps);   // +max .. -max
void fill_zero      (float* out, int num_steps);   // all 0
void fill_random    (float* out, int num_steps,
                     std::uint32_t seed);          // deterministic noise

// --- Bipolar rendering helpers ---

// Clamp a value to [kValueMin, kValueMax].
inline float clamp_value(float v) {
    if (v < kValueMin) return kValueMin;
    if (v > kValueMax) return kValueMax;
    return v;
}

// Convert a mouse-y-fraction (0 = cell top, 1 = cell bottom) into a
// bipolar value in [kValueMin, kValueMax]. Top of cell → max, bottom
// → min, middle → 0.
inline float value_from_cell_y(float y_in_cell) {
    if (y_in_cell < 0.0f) y_in_cell = 0.0f;
    if (y_in_cell > 1.0f) y_in_cell = 1.0f;
    const float norm = 1.0f - 2.0f * y_in_cell;  // [-1, +1]
    return norm * kValueMax;
}

} // namespace vivid::pattern_seq_editor
