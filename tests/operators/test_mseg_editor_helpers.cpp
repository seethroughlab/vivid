// Pure-logic unit tests for the MSEG editor helpers. No GLFW / no GPU.
#include "mseg_editor_shared.h"
#include "operator_api/types.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace me = ::vivid_mseg_editor;

struct CapturedSet {
    std::string name;
    float value;
};
struct CaptureCtx {
    std::vector<CapturedSet> calls;
};

static void capture_set_param(void* opaque, const char* name, float value) {
    auto* c = static_cast<CaptureCtx*>(opaque);
    c->calls.push_back({std::string(name ? name : ""), value});
}
static void capture_set_string_param(void*, const char*, const char*) {}

int main() {
    std::fprintf(stderr, "=== Test: MSEG editor helpers ===\n\n");

    // --- param_name_for ---
    {
        check(me::param_name_for(me::PointField::Time, 0) == "pt_time_0",
              "(Time, 0) → pt_time_0");
        check(me::param_name_for(me::PointField::Value, 15) == "pt_value_15",
              "(Value, 15) → pt_value_15");
        check(me::param_name_for(me::PointField::Curve, 14) == "pt_curve_14",
              "(Curve, 14) → pt_curve_14");
        check(me::param_name_for(me::PointField::Time, 3) == "pt_time_3",
              "(Time, 3) → pt_time_3");
    }

    // --- param_index_for ---
    {
        check(me::param_index_for(me::PointField::Time, 0) == me::kPtTimeBase,
              "Time/0 index = kPtTimeBase");
        check(me::param_index_for(me::PointField::Time, 15) == me::kPtTimeBase + 15,
              "Time/15 index");
        check(me::param_index_for(me::PointField::Value, 0) == me::kPtValueBase,
              "Value/0 index = kPtValueBase");
        check(me::param_index_for(me::PointField::Curve, 14) == me::kPtCurveBase + 14,
              "Curve/14 index");
    }

    // --- read_points: full param array ---
    {
        std::vector<float> params(me::kPtCurveBase + me::kMaxCurves, 0.0f);
        params[me::kNumPointsIndex] = 4;
        params[me::kPtTimeBase + 0] = 0.0f;
        params[me::kPtTimeBase + 1] = 0.333f;
        params[me::kPtTimeBase + 2] = 0.667f;
        params[me::kPtTimeBase + 3] = 1.0f;
        params[me::kPtValueBase + 1] = 1.0f;
        params[me::kPtValueBase + 2] = 0.5f;
        params[me::kPtCurveBase + 0] = 0.25f;

        me::PointArrays pts;
        const int np = me::read_points(params.data(),
            static_cast<uint32_t>(params.size()), &pts);
        check(np == 4, "read_points → np=4");
        check(std::fabs(pts.times[1] - 0.333f) < 1e-6f, "times[1] read");
        check(std::fabs(pts.values[1] - 1.0f) < 1e-6f, "values[1] read");
        check(std::fabs(pts.curves[0] - 0.25f) < 1e-6f, "curves[0] read");
    }

    // --- read_points: short input is defensive ---
    {
        float params[10] = {2, 0, 0, 0, 0, 0, 0.1f, 0.9f, 0, 0};
        me::PointArrays pts;
        const int np = me::read_points(params, 10, &pts);
        check(np == 2, "short input: np clamped to 2");
        check(std::fabs(pts.times[0] - 0.1f) < 1e-6f,
              "available slots populated");
        check(pts.times[10] == 0.0f, "unavailable slots zero-initialised");
    }

    // --- read_points: num_points clamped to [2, 16] ---
    {
        std::vector<float> params(me::kPtCurveBase + me::kMaxCurves, 0.0f);
        params[me::kNumPointsIndex] = 50.0f;  // out of range
        me::PointArrays pts;
        const int np = me::read_points(params.data(),
            static_cast<uint32_t>(params.size()), &pts);
        check(np == me::kMaxPoints, "np clamped to kMaxPoints from above");

        params[me::kNumPointsIndex] = 0.0f;
        const int np2 = me::read_points(params.data(),
            static_cast<uint32_t>(params.size()), &pts);
        check(np2 == 2, "np clamped to 2 from below");
    }

    // --- clamp_new_time ---
    {
        float times[4] = {0.0f, 0.3f, 0.7f, 1.0f};
        check(me::clamp_new_time(0, 4, 0.5f, times) == 0.0f,
              "index 0 pinned to 0.0");
        check(me::clamp_new_time(3, 4, 0.5f, times) == 1.0f,
              "index np-1 pinned to 1.0");
        check(std::fabs(me::clamp_new_time(1, 4, 0.5f, times) - 0.5f) < 1e-6f,
              "middle passthrough when inside range");
        check(std::fabs(me::clamp_new_time(1, 4, -0.2f, times) - 0.001f) < 1e-6f,
              "below prev clamps to prev + 0.001");
        check(std::fabs(me::clamp_new_time(1, 4, 0.9f, times) - (0.7f - 0.001f)) < 1e-6f,
              "above next clamps to next - 0.001");
    }

    // --- pick_point ---
    {
        me::PointArrays pts;
        pts.times[0] = 0.0f; pts.values[0] = 0.5f;
        pts.times[1] = 0.5f; pts.values[1] = 1.0f;
        pts.times[2] = 1.0f; pts.values[2] = 0.0f;
        const float px = 0.0f, py = 0.0f, pw = 100.0f, ph = 100.0f;

        // Click exactly on point 1: (0.5*100, (1-1.0)*100) = (50, 0)
        check(me::pick_point(50.0f, 0.0f, pts, 3, px, py, pw, ph, 8.0f) == 1,
              "exact handle hit returns 1");
        // Near point 0: (0, 50)
        check(me::pick_point(3.0f, 50.0f, pts, 3, px, py, pw, ph, 8.0f) == 0,
              "near handle hit within radius");
        // Far from any point
        check(me::pick_point(75.0f, 75.0f, pts, 3, px, py, pw, ph, 4.0f) == -1,
              "miss outside radius");
    }

    // --- pick_curve_handle ---
    {
        me::PointArrays pts;
        pts.times[0] = 0.0f; pts.values[0] = 0.0f;
        pts.times[1] = 1.0f; pts.values[1] = 1.0f;
        pts.curves[0] = 0.0f;  // linear
        const float px = 0.0f, py = 0.0f, pw = 100.0f, ph = 100.0f;

        // Midpoint of linear 0→1 is (0.5, 0.5) → pixel (50, 50)
        check(me::pick_curve_handle(50.0f, 50.0f, pts, 2, px, py, pw, ph, 8.0f) == 0,
              "linear midpoint handle hit");
        // Far away — no handle in range
        check(me::pick_curve_handle(10.0f, 90.0f, pts, 2, px, py, pw, ph, 4.0f) == -1,
              "miss outside radius");
    }

    // --- add_point: insert at 2 in a 4-point envelope ---
    {
        me::PointArrays pts;
        pts.times[0]  = 0.0f;   pts.values[0] = 0.0f;
        pts.times[1]  = 0.333f; pts.values[1] = 1.0f;
        pts.times[2]  = 0.667f; pts.values[2] = 0.5f;
        pts.times[3]  = 1.0f;   pts.values[3] = 0.0f;
        pts.curves[0] = 0.1f;
        pts.curves[1] = 0.2f;
        pts.curves[2] = 0.3f;

        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;

        const bool ok = me::add_point(api, pts, /*np=*/4, /*insert_at=*/2,
                                       /*time=*/0.5f, /*value=*/0.8f,
                                       /*curve=*/0.5f);
        check(ok, "add_point returns true at mid-position");

        // Verify num_points update is the last call.
        check(!cap.calls.empty() &&
              cap.calls.back().name == "num_points" &&
              cap.calls.back().value == 5.0f,
              "last call is num_points = 5");

        // Verify the new slot holds the requested values.
        bool found_time = false, found_value = false, found_curve = false;
        for (const auto& c : cap.calls) {
            if (c.name == "pt_time_2"  && std::fabs(c.value - 0.5f) < 1e-6f) found_time  = true;
            if (c.name == "pt_value_2" && std::fabs(c.value - 0.8f) < 1e-6f) found_value = true;
            if (c.name == "pt_curve_2" && std::fabs(c.value - 0.5f) < 1e-6f) found_curve = true;
        }
        check(found_time && found_value && found_curve,
              "new slot populated with passed-in values");

        // Points 2..4 (old 2..3 shifted) must be present.
        bool shifted_time_3 = false, shifted_value_3 = false;
        for (const auto& c : cap.calls) {
            if (c.name == "pt_time_3"  && std::fabs(c.value - 0.667f) < 1e-6f) shifted_time_3 = true;
            if (c.name == "pt_value_3" && std::fabs(c.value - 0.5f)   < 1e-6f) shifted_value_3 = true;
        }
        check(shifted_time_3 && shifted_value_3,
              "later points shifted right by one");

        // Order: times before values before curves before num_points.
        size_t last_time = 0, first_value = SIZE_MAX, last_value = 0;
        size_t first_curve = SIZE_MAX, num_points_idx = SIZE_MAX;
        for (size_t i = 0; i < cap.calls.size(); ++i) {
            const auto& n = cap.calls[i].name;
            if (n.rfind("pt_time_", 0) == 0) last_time = i;
            if (n.rfind("pt_value_", 0) == 0) {
                first_value = std::min(first_value, i);
                last_value = i;
            }
            if (n.rfind("pt_curve_", 0) == 0) first_curve = std::min(first_curve, i);
            if (n == "num_points") num_points_idx = i;
        }
        check(first_value != SIZE_MAX && last_time < first_value,
              "times emitted before values");
        check(first_curve != SIZE_MAX && last_value < first_curve,
              "values emitted before curves");
        check(num_points_idx == cap.calls.size() - 1,
              "num_points is the final call");
    }

    // --- add_point at capacity is a no-op ---
    {
        me::PointArrays pts;
        for (int i = 0; i < me::kMaxPoints; ++i) {
            pts.times[i]  = static_cast<float>(i) / (me::kMaxPoints - 1);
            pts.values[i] = 0.0f;
        }
        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;

        const bool ok = me::add_point(api, pts, me::kMaxPoints, 5,
                                       0.3f, 0.5f, 0.0f);
        check(!ok, "add_point at capacity returns false");
        check(cap.calls.empty(), "no set_param calls issued at capacity");
    }

    // --- add_point out-of-range index returns false ---
    {
        me::PointArrays pts;
        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;

        check(!me::add_point(api, pts, 4, 0, 0.5f, 0.5f, 0.0f),
              "add_point at index 0 refused");
        check(!me::add_point(api, pts, 4, 4, 0.5f, 0.5f, 0.0f),
              "add_point at index np refused");
        check(cap.calls.empty(), "no writes for refused adds");
    }

    // --- remove_point: remove middle index ---
    {
        me::PointArrays pts;
        pts.times[0] = 0.0f;   pts.values[0] = 0.0f;
        pts.times[1] = 0.333f; pts.values[1] = 1.0f;
        pts.times[2] = 0.667f; pts.values[2] = 0.5f;
        pts.times[3] = 1.0f;   pts.values[3] = 0.0f;
        pts.curves[0] = 0.1f;
        pts.curves[1] = 0.2f;
        pts.curves[2] = 0.3f;

        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;

        const bool ok = me::remove_point(api, pts, /*np=*/4, /*remove_at=*/1);
        check(ok, "remove_point middle returns true");

        // Point at old index 2 should now be at index 1.
        bool shifted_time = false, shifted_value = false;
        for (const auto& c : cap.calls) {
            if (c.name == "pt_time_1"  && std::fabs(c.value - 0.667f) < 1e-6f) shifted_time = true;
            if (c.name == "pt_value_1" && std::fabs(c.value - 0.5f)   < 1e-6f) shifted_value = true;
        }
        check(shifted_time && shifted_value,
              "later point shifted left into the removed slot");

        // Final call: num_points = 3
        check(cap.calls.back().name == "num_points" &&
              cap.calls.back().value == 3.0f,
              "num_points decremented to 3");
    }

    // --- remove_point endpoint refusal ---
    {
        me::PointArrays pts;
        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;

        check(!me::remove_point(api, pts, 4, 0),  "remove endpoint 0 refused");
        check(!me::remove_point(api, pts, 4, 3),  "remove endpoint np-1 refused");
        check(cap.calls.empty(), "no writes for refused endpoint removal");
    }

    // --- remove_point at minimum refuses ---
    {
        me::PointArrays pts;
        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;

        check(!me::remove_point(api, pts, 2, 0), "np == 2: cannot remove any");
        check(cap.calls.empty(), "no writes at minimum");
    }

    // --- null set_param safe no-ops ---
    {
        me::PointArrays pts;
        VividInspectorCommandAPI api{};
        api.set_param = nullptr;
        api.set_string_param = nullptr;
        check(!me::add_point(api, pts, 4, 2, 0.5f, 0.5f, 0.0f),
              "add_point with null set_param returns false");
        check(!me::remove_point(api, pts, 4, 1),
              "remove_point with null set_param returns false");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
