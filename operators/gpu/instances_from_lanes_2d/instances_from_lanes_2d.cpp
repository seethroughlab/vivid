#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_2d.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// =============================================================================
// InstancesFromLanes2D — pack per-attribute lane arrays into InstanceArray2D
// =============================================================================

/**
 * @brief Migration bridge from per-attribute lane arrays to an InstanceArray2D bundle.
 *
 * 2D analog of InstancesFromLanes (3D). Accepts up to 9 optional lane-array
 * inputs (position, scale per axis, rotation, color per component) and emits
 * a single InstanceArray2D bundle for consumption by Instancer2D's `instances`
 * input. Use this when driving instance attributes from independent lane
 * sources (e.g. SpreadNoise, FFT analysis, Repeat).
 *
 * Instance count = max length among connected inputs (capped at 4096).
 * Unconnected attributes fall back to sensible defaults: position/rotation 0,
 * scale 1, color 1.
 *
 * Per-instance transform: T(pos_x, pos_y) · R(rotation) · S(scale_x, scale_y).
 *
 * @tip Use this to wire SpreadNoise / FFT / sequencer lane outputs directly into per-instance transforms.
 * @tip Instance count is the MAX lane length among connected inputs; short lanes cycle.
 * @recipe SpreadNoise × 3 -> InstancesFromLanes2D -> Instancer2D ← Shape2D -> Render2D
 * @common_companions Instancer2D, SpreadNoise, Repeat, Shape2D
 * @best_used_with Instancer2D
 * @family 2D drawable pipeline
 * @see InstanceGrid2D, Instancer2D
 */
struct InstancesFromLanes2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName               = "InstancesFromLanes2D";
    static constexpr bool kTimeDependent             = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_KERNEL;

    void collect_params(std::vector<vivid::ParamBase*>& /*out*/) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // 9 optional lane-array inputs, fixed order:
        out.push_back({"pos_x",    VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});  // 0
        out.push_back({"pos_y",    VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});  // 1
        out.push_back({"scale_x",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});  // 2
        out.push_back({"scale_y",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});  // 3
        out.push_back({"rotation", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});  // 4
        out.push_back({"color_r",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});  // 5
        out.push_back({"color_g",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});  // 6
        out.push_back({"color_b",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});  // 7
        out.push_back({"color_a",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});  // 8

        out.push_back(VIVID_CUSTOM_REF_PORT("instances", VIVID_PORT_OUTPUT,
                                            vivid::gpu::InstanceArray2D));
    }

    void process_gpu(const VividGpuContext* ctx) override {
        const float* in_data[9]{};
        uint32_t     in_len [9]{};

        if (ctx->input_lanes) {
            for (int p = 0; p < 9; ++p) {
                if (ctx->input_lanes[p].length > 0) {
                    in_data[p] = ctx->input_lanes[p].data;
                    in_len [p] = ctx->input_lanes[p].length;
                }
            }
        }

        uint32_t n = 0;
        for (int p = 0; p < 9; ++p) n = std::max(n, in_len[p]);
        if (n == 0) n = 1;
        if (n > 4096) n = 4096;

        instances_.resize(n);

        auto sample = [](const float* data, uint32_t len, uint32_t i, float fallback) -> float {
            return (data && len > 0) ? data[i % len] : fallback;
        };

        for (uint32_t i = 0; i < n; ++i) {
            float px = sample(in_data[0], in_len[0], i, 0.0f);
            float py = sample(in_data[1], in_len[1], i, 0.0f);
            float sx = sample(in_data[2], in_len[2], i, 1.0f);
            float sy = sample(in_data[3], in_len[3], i, 1.0f);
            float th = sample(in_data[4], in_len[4], i, 0.0f);

            float cr = std::cos(th);
            float sr = std::sin(th);

            auto& d = instances_[i];
            // T · R · S, column-major mat3x2:
            // linear block = R · S = [[cr*sx, -sr*sy], [sr*sx, cr*sy]]
            d.transform[0] = cr * sx;   // col0.x (a)
            d.transform[1] = sr * sx;   // col0.y (c)
            d.transform[2] = -sr * sy;  // col1.x (b)
            d.transform[3] = cr * sy;   // col1.y (d)
            d.transform[4] = px;        // col2.x (tx)
            d.transform[5] = py;        // col2.y (ty)
            d._pad_xform[0] = 0.0f;
            d._pad_xform[1] = 0.0f;
            d.color[0] = sample(in_data[5], in_len[5], i, 1.0f);
            d.color[1] = sample(in_data[6], in_len[6], i, 1.0f);
            d.color[2] = sample(in_data[7], in_len[7], i, 1.0f);
            d.color[3] = sample(in_data[8], in_len[8], i, 1.0f);
        }

        bundle_.data  = instances_.data();
        bundle_.count = n;
        bundle_._pad0 = 0;
        ctx->custom_outputs[0] = &bundle_;
    }

private:
    std::vector<vivid::gpu::InstanceData2D> instances_;
    vivid::gpu::InstanceArray2D bundle_{};
};

VIVID_DEFINE_OP(InstancesFromLanes2D) {
}

