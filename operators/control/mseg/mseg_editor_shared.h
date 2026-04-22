#pragma once
// Pure-logic helpers shared by the MSEG inspector and the dedicated editor
// window. Keeps param-name encoding, clamp/hit-test math, and point
// add/remove sequencing in one place so both surfaces stay in sync and can
// be unit-tested without GLFW/WGPU.

#include "operator_api/types.h"

#include <cstdint>
#include <string>

namespace vivid_mseg_editor {

enum class PointField : std::uint8_t { Time = 0, Value = 1, Curve = 2 };

// Mirrors MSEG::kMaxPoints / kMaxCurves without including the operator
// header (keeps the dependency direction one-way: operator → helpers, not
// helpers → operator).
inline constexpr int kMaxPoints = 16;
inline constexpr int kMaxCurves = 15;

// Descriptor-order param indices in the MSEG layout.
//   0  : num_points
//   1  : total_time
//   2  : loop_enabled
//   3  : loop_start
//   4  : loop_end
//   5  : amplitude
//   6..21   : pt_time_0..15   (hidden)
//   22..37  : pt_value_0..15  (hidden)
//   38..52  : pt_curve_0..14  (hidden)
inline constexpr int kNumPointsIndex = 0;
inline constexpr int kPtTimeBase     = 6;
inline constexpr int kPtValueBase    = 22;
inline constexpr int kPtCurveBase    = 38;

// Canonical param name for (kind, index). e.g. Time/3 → "pt_time_3".
std::string param_name_for(PointField kind, int index);

// Descriptor-order index for (kind, index).
int param_index_for(PointField kind, int index);

struct PointArrays {
    float times [kMaxPoints]{};
    float values[kMaxPoints]{};
    float curves[kMaxCurves]{};
};

// Reads the MSEG point arrays from param_values into plain buffers. Returns
// the clamped np (>= 2, <= kMaxPoints). Defensive against short inputs —
// unavailable slots stay zero-initialized.
int read_points(const float* param_values, std::uint32_t param_count,
                PointArrays* out);

// Clamp a candidate time for the drag of point `index`. Endpoints pin to 0
// (index 0) and 1 (index np-1); middle points stay strictly between
// neighbours (± 0.001 margin).
float clamp_new_time(int index, int np, float new_time, const float* times);

// Pick the nearest breakpoint within hit_radius (Euclidean, plot-pixel
// coords). Returns -1 if none.
int pick_point(float mx, float my,
               const PointArrays& pts, int np,
               float plot_x, float plot_y,
               float plot_w, float plot_h,
               float hit_radius);

// Pick the curve handle (segment midpoint) nearest (mx, my). Returns the
// segment index `i` where 0 <= i < np-1, or -1 if no handle is within
// hit_radius. Handle y is evaluated using the live curve shape of segment i.
int pick_curve_handle(float mx, float my,
                      const PointArrays& pts, int np,
                      float plot_x, float plot_y,
                      float plot_w, float plot_h,
                      float hit_radius);

// Evaluate the envelope value at normalized time t ∈ [0, 1] using the same
// interpolation the runtime uses (exponential curve shaping).
float evaluate_at(float t, const PointArrays& pts, int np);

// Insert a new breakpoint at `insert_at` (must satisfy 0 < insert_at < np).
// Right-shifts later points / curves via a set_param sequence, then bumps
// num_points. No-op (returns false) if np >= kMaxPoints or insert_at out of
// range.
bool add_point(const VividInspectorCommandAPI& commands,
               const PointArrays& pts, int np, int insert_at,
               float new_time, float new_value, float new_curve);

// Remove the breakpoint at `remove_at`. Endpoints (0, np-1) and np == 2 are
// no-ops (returns false). Left-shifts later points / curves then decrements
// num_points.
bool remove_point(const VividInspectorCommandAPI& commands,
                  const PointArrays& pts, int np, int remove_at);

} // namespace vivid_mseg_editor
