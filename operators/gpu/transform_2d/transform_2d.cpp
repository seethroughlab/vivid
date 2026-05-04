#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_2d.h"
#include <cstdint>
#include <vector>

// =============================================================================
// Transform2D — compose an affine transform onto a drawable
// =============================================================================

/**
 * @brief Apply a 2D translation + rotation + scale onto a drawable.
 *
 * Composes `(trs ∘ drawable.transform)` into the drawable's transform field.
 *
 * Semantics:
 *   - **Single-instance drawable:** the TRS wraps the drawable as a whole
 *     (group-level transform). Rotates / translates / scales the thing you
 *     see on screen exactly as expected.
 *   - **Instanced drawable:** the TRS applies in each instance's local frame,
 *     *before* the per-instance transform places it. Rotation and uniform
 *     scale still look correct for symmetric shapes; translation does NOT
 *     move the group as a whole — it shifts each instance's origin in its
 *     own local frame. To move the whole instanced group, translate the
 *     instance positions (e.g. adjust InstanceGrid2D's spacing/center) or
 *     wait for a future `outer_transform` field on VividDrawable2D.
 *
 * Known limitation to fix in a later sub-phase: add an `outer_transform`
 * field to VividDrawable2D so Transform2D can act as a true group-level
 * transform on instanced drawables.
 *
 * @param translate_x / translate_y  Additional translation in NDC units.
 * @param rotation                   Additional rotation in radians.
 * @param scale_x / scale_y          Additional non-uniform scale.
 *
 * @tip Composes a TRS onto an existing drawable's transform — stack multiple for cumulative motion.
 * @pitfall On an instanced drawable, translation currently acts per-instance-local rather than group-level.
 * @recipe Shape2D -> Transform2D -> Render2D -> video_out
 * @common_companions Shape2D, Sprite2D, Instancer2D, Render2D
 * @best_used_with Render2D
 * @family 2D drawable pipeline
 * @see Shape2D, Instancer2D, Render2D
 */
struct Transform2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName         = "Transform2D";
    static constexpr bool kTimeDependent       = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;

    vivid::Param<float> translate_x {"translate_x", 0.0f, -4.0f, 4.0f};
    vivid::Param<float> translate_y {"translate_y", 0.0f, -4.0f, 4.0f};
    vivid::Param<float> rotation    {"rotation",    0.0f, -6.2832f, 6.2832f};
    vivid::Param<float> scale_x     {"scale_x",     1.0f,  0.0f, 8.0f};
    vivid::Param<float> scale_y     {"scale_y",     1.0f,  0.0f, 8.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(translate_x, "Transform");
        vivid::param_group(translate_y, "Transform");
        vivid::param_group(rotation,    "Transform");
        vivid::param_group(scale_x,     "Transform");
        vivid::param_group(scale_y,     "Transform");
        out.push_back(&translate_x);
        out.push_back(&translate_y);
        out.push_back(&rotation);
        out.push_back(&scale_x);
        out.push_back(&scale_y);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::drawable_port("drawable", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::drawable_port("drawable", VIVID_PORT_OUTPUT));
    }

    void process_gpu(const VividGpuContext* ctx) override {
        const auto* in = vivid::gpu::drawable_input(ctx, 0);
        if (!in) return;

        // Build this operator's TRS as a column-major mat3x2.
        float trs[6];
        vivid::gpu::drawable_transform_trs(
            trs,
            translate_x.value, translate_y.value,
            rotation.value,
            scale_x.value, scale_y.value);

        // Shallow-copy and compose the TRS onto the drawable's transform.
        // See the docstring above for the semantic implications on instanced
        // drawables.
        output_ = *in;
        float composed[6];
        vivid::gpu::drawable_transform_compose(composed, trs, in->transform);
        for (int k = 0; k < 6; ++k) output_.transform[k] = composed[k];

        ctx->custom_outputs[0] = &output_;
    }

private:
    vivid::gpu::VividDrawable2D output_{};
};

VIVID_DEFINE_OP(Transform2D) {
}

VIVID_REGISTER(Transform2D)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividDrawable2D)
