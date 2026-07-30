#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/value_view.h"
#include "operator_api/lane_thumb.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// =============================================================================
// LaneRamp — a generic FLOAT-MANY value lane of `count` evenly-spaced values
// =============================================================================
//
// Emits `count` values spanning [lo, hi] (Linear) or a symmetric spread about 0 (Centered). A generic
// building block for the lane transport: feed it into InstancesFromLanes' pos_x to lay a row of
// instances out in space (an equaliser's bar positions), or any per-instance attribute that should
// vary smoothly across the set. Pairs with AudioSpectrum (heights) so layout and reactivity are
// separate, composable nodes.

struct LaneRamp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName         = "LaneRamp";
    static constexpr bool kTimeDependent       = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE;

    vivid::Param<int>   count {"count", 48, 1, 4096};
    vivid::Param<float> lo    {"lo",    -13.0f, -1000.0f, 1000.0f};
    vivid::Param<float> hi    {"hi",     13.0f, -1000.0f, 1000.0f};
    vivid::Param<int>   mode  {"mode",   0, {"Linear", "Centered"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(count, "Ramp");
        vivid::param_group(lo,    "Ramp");
        vivid::param_group(hi,    "Ramp");
        vivid::param_group(mode,  "Ramp");
        out.push_back(&count); out.push_back(&lo); out.push_back(&hi); out.push_back(&mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor p{};
        p.name = "values"; p.type = VIVID_PORT_SCALAR;
        p.direction = VIVID_PORT_OUTPUT; p.multiplicity = VIVID_MULTIPLICITY_MANY;
        p.semantic_shape = "lane_array";
        out.push_back(p);
    }

    void draw_thumbnail(const VividThumbnailContext*) override {}

    void process_gpu(const VividGpuContext* ctx) override {
        const uint32_t n = static_cast<uint32_t>(std::max(1, count.int_value()));
        out_.resize(n);
        const float a = lo.value, b = hi.value;
        const bool centered = mode.int_value() == 1;
        for (uint32_t i = 0; i < n; ++i) {
            const float t = (n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.5f;
            out_[i] = centered ? (t - 0.5f) * (b - a) : (a + (b - a) * t);
        }
        if (ctx->value_outputs) {
            if (float* buf = vivid_value_output_floats(&ctx->value_outputs[0], n)) {
                std::copy(out_.begin(), out_.end(), buf);
                vivid_value_output_commit(&ctx->value_outputs[0], n);
            }
        }
        // Node thumbnail: the ramp normalised to its own min..max, as bars.
        float mn = out_[0], mx = out_[0];
        for (float x : out_) { mn = std::min(mn, x); mx = std::max(mx, x); }
        const float inv = (mx > mn) ? 1.f / (mx - mn) : 0.f;
        disp_.resize(n);
        for (uint32_t i = 0; i < n; ++i) disp_[i] = (out_[i] - mn) * inv;
        const float col[3] = { 0.6f, 0.7f, 0.85f };
        vivid::lanethumb::render_bars(ctx, thumb_, disp_.data(), n, col);
    }

    ~LaneRamp() override { vivid::lanethumb::destroy(thumb_); }

private:
    std::vector<float> out_, disp_;
    vivid::lanethumb::State thumb_{};
};

VIVID_REGISTER(LaneRamp)
VIVID_THUMBNAIL(LaneRamp)
