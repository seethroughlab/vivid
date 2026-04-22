#include "mseg_editor_shared.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid_mseg_editor {

namespace {
// Matches MSEG::curve_interp in mseg.h (same transfer curve).
float curve_interp(float t, float curve) {
    if (std::abs(curve) < 0.001f) return t;
    const float k = curve * 4.0f;
    return (std::exp(k * t) - 1.0f) / (std::exp(k) - 1.0f);
}
} // namespace

std::string param_name_for(PointField kind, int index) {
    char buf[32];
    switch (kind) {
        case PointField::Time:
            std::snprintf(buf, sizeof(buf), "pt_time_%d", index);
            break;
        case PointField::Value:
            std::snprintf(buf, sizeof(buf), "pt_value_%d", index);
            break;
        case PointField::Curve:
            std::snprintf(buf, sizeof(buf), "pt_curve_%d", index);
            break;
    }
    return std::string(buf);
}

int param_index_for(PointField kind, int index) {
    switch (kind) {
        case PointField::Time:  return kPtTimeBase  + index;
        case PointField::Value: return kPtValueBase + index;
        case PointField::Curve: return kPtCurveBase + index;
    }
    return 0;
}

int read_points(const float* param_values, std::uint32_t param_count,
                PointArrays* out) {
    if (!out) return 0;
    *out = PointArrays{};

    int np = 4;
    if (param_values && param_count > static_cast<std::uint32_t>(kNumPointsIndex)) {
        np = static_cast<int>(param_values[kNumPointsIndex]);
    }
    np = std::max(2, std::min(kMaxPoints, np));

    if (!param_values) return np;

    for (int i = 0; i < kMaxPoints; ++i) {
        const int tidx = kPtTimeBase + i;
        if (param_count > static_cast<std::uint32_t>(tidx))
            out->times[i] = param_values[tidx];
        const int vidx = kPtValueBase + i;
        if (param_count > static_cast<std::uint32_t>(vidx))
            out->values[i] = param_values[vidx];
    }
    for (int i = 0; i < kMaxCurves; ++i) {
        const int cidx = kPtCurveBase + i;
        if (param_count > static_cast<std::uint32_t>(cidx))
            out->curves[i] = param_values[cidx];
    }
    return np;
}

float clamp_new_time(int index, int np, float new_time, const float* times) {
    if (!times || np < 2) return 0.0f;
    if (index == 0)        return 0.0f;
    if (index == np - 1)   return 1.0f;
    if (index < 0 || index >= np) return new_time;
    const float prev_t = times[index - 1] + 0.001f;
    const float next_t = times[index + 1] - 0.001f;
    return std::max(prev_t, std::min(next_t, new_time));
}

int pick_point(float mx, float my,
               const PointArrays& pts, int np,
               float plot_x, float plot_y,
               float plot_w, float plot_h,
               float hit_radius) {
    int best = -1;
    float best_d2 = hit_radius * hit_radius;
    for (int i = 0; i < np; ++i) {
        const float hx = plot_x + pts.times[i] * plot_w;
        const float hy = plot_y + (1.0f - pts.values[i]) * plot_h;
        const float dx = mx - hx;
        const float dy = my - hy;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

float evaluate_at(float t, const PointArrays& pts, int np) {
    if (np < 2) return 0.0f;
    for (int i = 0; i < np - 1; ++i) {
        const float t0 = pts.times[i];
        const float t1 = pts.times[i + 1];
        if (t <= t1 || i == np - 2) {
            const float seg_len = t1 - t0;
            float local = (seg_len > 0.0001f) ? (t - t0) / seg_len : 0.0f;
            local = std::max(0.0f, std::min(1.0f, local));
            const float shaped = curve_interp(local, pts.curves[i]);
            return pts.values[i] + (pts.values[i + 1] - pts.values[i]) * shaped;
        }
    }
    return pts.values[np - 1];
}

int pick_curve_handle(float mx, float my,
                      const PointArrays& pts, int np,
                      float plot_x, float plot_y,
                      float plot_w, float plot_h,
                      float hit_radius) {
    int best = -1;
    float best_d2 = hit_radius * hit_radius;
    for (int i = 0; i < np - 1; ++i) {
        const float mid_t = 0.5f * (pts.times[i] + pts.times[i + 1]);
        const float mid_v = evaluate_at(mid_t, pts, np);
        const float hx = plot_x + mid_t * plot_w;
        const float hy = plot_y + (1.0f - mid_v) * plot_h;
        const float dx = mx - hx;
        const float dy = my - hy;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best_d2) {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

bool add_point(const VividInspectorCommandAPI& commands,
               const PointArrays& pts, int np, int insert_at,
               float new_time, float new_value, float new_curve) {
    if (!commands.set_param) return false;
    if (np >= kMaxPoints) return false;
    if (insert_at <= 0 || insert_at >= np) return false;

    // Shift later points right. Issue calls in a deterministic order so
    // listeners (and tests) see a predictable sequence: all affected times,
    // then values, then curves, then num_points.
    float new_times [kMaxPoints]{};
    float new_values[kMaxPoints]{};
    float new_curves[kMaxCurves]{};
    for (int i = 0; i < insert_at; ++i) {
        new_times[i]  = pts.times[i];
        new_values[i] = pts.values[i];
    }
    new_times[insert_at]  = new_time;
    new_values[insert_at] = new_value;
    for (int i = insert_at; i < np; ++i) {
        new_times[i + 1]  = pts.times[i];
        new_values[i + 1] = pts.values[i];
    }
    // Curves: segment i sits between points i and i+1. Insert splits segment
    // (insert_at - 1) into two: the left half keeps the old curve value, the
    // new segment (at index insert_at) gets new_curve; later segments shift.
    for (int i = 0; i < insert_at - 1; ++i) {
        new_curves[i] = pts.curves[i];
    }
    if (insert_at - 1 >= 0 && insert_at - 1 < kMaxCurves) {
        new_curves[insert_at - 1] = pts.curves[insert_at - 1];
    }
    if (insert_at < kMaxCurves) {
        new_curves[insert_at] = new_curve;
    }
    for (int i = insert_at; i < np - 1; ++i) {
        if (i + 1 < kMaxCurves)
            new_curves[i + 1] = pts.curves[i];
    }

    // Emit.
    const int new_np = np + 1;
    for (int i = insert_at; i < new_np; ++i) {
        const std::string n = param_name_for(PointField::Time, i);
        commands.set_param(commands.opaque, n.c_str(), new_times[i]);
    }
    for (int i = insert_at; i < new_np; ++i) {
        const std::string n = param_name_for(PointField::Value, i);
        commands.set_param(commands.opaque, n.c_str(), new_values[i]);
    }
    for (int i = insert_at - 1; i < new_np - 1 && i < kMaxCurves; ++i) {
        if (i < 0) continue;
        const std::string n = param_name_for(PointField::Curve, i);
        commands.set_param(commands.opaque, n.c_str(), new_curves[i]);
    }
    commands.set_param(commands.opaque, "num_points",
                       static_cast<float>(new_np));
    return true;
}

bool remove_point(const VividInspectorCommandAPI& commands,
                  const PointArrays& pts, int np, int remove_at) {
    if (!commands.set_param) return false;
    if (np <= 2) return false;
    if (remove_at <= 0 || remove_at >= np - 1) return false;

    float new_times [kMaxPoints]{};
    float new_values[kMaxPoints]{};
    float new_curves[kMaxCurves]{};
    for (int i = 0; i < remove_at; ++i) {
        new_times[i]  = pts.times[i];
        new_values[i] = pts.values[i];
    }
    for (int i = remove_at + 1; i < np; ++i) {
        new_times[i - 1]  = pts.times[i];
        new_values[i - 1] = pts.values[i];
    }
    // Curves: segment (remove_at - 1) absorbs (remove_at)'s role; keep its
    // original value. Shift everything after remove_at left.
    for (int i = 0; i < remove_at - 1; ++i) {
        new_curves[i] = pts.curves[i];
    }
    if (remove_at - 1 >= 0 && remove_at - 1 < kMaxCurves)
        new_curves[remove_at - 1] = pts.curves[remove_at - 1];
    for (int i = remove_at + 1; i < np - 1; ++i) {
        if (i - 1 < kMaxCurves)
            new_curves[i - 1] = pts.curves[i];
    }

    const int new_np = np - 1;
    for (int i = remove_at; i < new_np; ++i) {
        const std::string n = param_name_for(PointField::Time, i);
        commands.set_param(commands.opaque, n.c_str(), new_times[i]);
    }
    for (int i = remove_at; i < new_np; ++i) {
        const std::string n = param_name_for(PointField::Value, i);
        commands.set_param(commands.opaque, n.c_str(), new_values[i]);
    }
    for (int i = remove_at - 1; i < new_np - 1 && i < kMaxCurves; ++i) {
        if (i < 0) continue;
        const std::string n = param_name_for(PointField::Curve, i);
        commands.set_param(commands.opaque, n.c_str(), new_curves[i]);
    }
    commands.set_param(commands.opaque, "num_points",
                       static_cast<float>(new_np));
    return true;
}

} // namespace vivid_mseg_editor
