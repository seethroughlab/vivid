#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_2d.h"
#include "operator_api/instance_algorithms.h"
#include <cstdint>
#include <vector>

// =============================================================================
// InstanceGrid2D — emit InstanceArray2D for Grid / Circle / Line layouts
// =============================================================================

/**
 * @brief Generate N per-instance 2D transforms in a geometric layout.
 *
 * 2D analog of InstanceGrid (3D). Emits an InstanceArray2D bundle suitable
 * for wiring into Instancer2D's `instances` input. Each instance carries
 * a position (as mat3x2 affine translation) + scale; rotation defaults to 0
 * and color to white.
 *
 * @param count    Number of instances to generate (1–4096).
 * @param layout   Placement pattern. 0=Grid, 1=Circle, 2=Line.
 * @param spacing  Distance between generated instances (NDC units).
 * @param scale    Per-instance uniform scale applied to each placement.
 *
 * @tip Feed into Instancer2D's `instances` input; a separate Shape2D supplies the `drawable`.
 * @tip Add InstanceNoise2D between this and Instancer2D for organic jitter on the static layout.
 * @recipe InstanceGrid2D -> Instancer2D ← Shape2D -> Render2D
 * @recipe InstanceGrid2D -> InstanceNoise2D -> Instancer2D ← Shape2D -> Render2D
 * @common_companions Instancer2D, InstanceNoise2D, Shape2D
 * @best_used_with Instancer2D
 * @family 2D drawable pipeline
 * @see Instancer2D, InstanceNoise2D, InstancesFromLanes2D
 */
struct InstanceGrid2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName         = "InstanceGrid2D";
    static constexpr bool kTimeDependent       = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE;

    vivid::Param<int>   count   {"count",   16,    1,   4096};
    vivid::Param<int>   layout  {"layout",  0,     {"Grid", "Circle", "Line"}};
    vivid::Param<float> spacing {"spacing", 0.25f, 0.0f, 2.0f};
    vivid::Param<float> scale   {"scale",   0.1f,  0.0f, 2.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(count,   "Layout");
        vivid::param_group(layout,  "Layout");
        vivid::param_group(spacing, "Layout");
        vivid::param_group(scale,   "Layout");
        out.push_back(&count);
        out.push_back(&layout);
        out.push_back(&spacing);
        out.push_back(&scale);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::instance_array_port("instances", VIVID_PORT_OUTPUT));
    }

    void process_gpu(const VividGpuContext* ctx) override {
        uint32_t n = static_cast<uint32_t>(count.int_value());
        if (n == 0) n = 1;
        if (n > 4096) n = 4096;

        instances_.resize(n);
        const int   mode = layout.int_value();
        const float sp   = spacing.value;
        const float sc   = scale.value;

        for (uint32_t i = 0; i < n; ++i) {
            vivid::instancing::Vec2 p;
            switch (mode) {
                case 1:  p = vivid::instancing::circle_2d(i, n, sp); break;
                case 2:  p = vivid::instancing::line_2d  (i, n, sp); break;
                default: p = vivid::instancing::grid_2d  (i, n, sp); break;
            }
            const float x = p.x;
            const float y = p.y;

            auto& d = instances_[i];
            // Column-major mat3x2: scale × translation
            d.transform[0] = sc;   // col0.x (a)
            d.transform[1] = 0.0f; // col0.y (c)
            d.transform[2] = 0.0f; // col1.x (b)
            d.transform[3] = sc;   // col1.y (d)
            d.transform[4] = x;    // col2.x (tx)
            d.transform[5] = y;    // col2.y (ty)
            d._pad_xform[0] = 0.0f;
            d._pad_xform[1] = 0.0f;
            d.color[0] = 1.0f;
            d.color[1] = 1.0f;
            d.color[2] = 1.0f;
            d.color[3] = 1.0f;
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

VIVID_DEFINE_OP(InstanceGrid2D) {
}


VIVID_DESCRIBE_REF_TYPE(vivid::gpu::InstanceArray2D)
