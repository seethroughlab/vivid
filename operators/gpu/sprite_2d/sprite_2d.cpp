#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_2d.h"
#include <vector>

// =============================================================================
// Sprite2D — emit a single textured-quad drawable
// =============================================================================

/**
 * @brief Emit one textured quad as a 2D drawable.
 *
 * Consumes a texture input (from TextureLoader, Shape, Noise, Movie, etc.)
 * and emits a VividDrawable2D of type SPRITE carrying that texture handle.
 * Render2D rasterises it as a textured quad; downstream Instancer2D can
 * multiply it by per-instance transforms to draw N copies.
 *
 * @param position_x / position_y  Center position in NDC (-1..1).
 * @param rotation                 Rotation in radians.
 * @param scale_x / scale_y        Non-uniform scale (quad size in NDC).
 * @param tint_r / tint_g / tint_b / tint_a  Multiplicative tint on the sample.
 *
 * @tip The textured counterpart to Shape2D. Connect a texture input to draw a sprite.
 * @tip Emits a drawable with blend_mode ALPHA; for additive sprites post-compose with Transform2D.
 * @recipe WebcamIn -> Sprite2D -> Render2D -> video_out
 * @common_companions Render2D, Instancer2D, Transform2D, MovieFile
 * @best_used_with Render2D
 * @family 2D drawable pipeline
 * @see Shape2D, Render2D, Instancer2D
 */
struct Sprite2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Sprite2D";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> position_x {"position_x", 0.0f, -2.0f, 2.0f};
    vivid::Param<float> position_y {"position_y", 0.0f, -2.0f, 2.0f};
    vivid::Param<float> rotation   {"rotation",   0.0f, -6.2832f, 6.2832f};
    vivid::Param<float> scale_x    {"scale_x",    0.5f, 0.0f, 4.0f};
    vivid::Param<float> scale_y    {"scale_y",    0.5f, 0.0f, 4.0f};

    vivid::Param<float> tint_r {"tint_r", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> tint_g {"tint_g", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> tint_b {"tint_b", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> tint_a {"tint_a", 1.0f, 0.0f, 1.0f};

    Sprite2D() {
        vivid::display_hint(tint_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(tint_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(tint_b, VIVID_DISPLAY_COLOR);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(position_x, "Transform");
        vivid::param_group(position_y, "Transform");
        vivid::param_group(rotation,   "Transform");
        vivid::param_group(scale_x,    "Transform");
        vivid::param_group(scale_y,    "Transform");
        vivid::param_group(tint_r, "Tint");
        vivid::param_group(tint_g, "Tint");
        vivid::param_group(tint_b, "Tint");
        vivid::param_group(tint_a, "Tint");
        out.push_back(&position_x);
        out.push_back(&position_y);
        out.push_back(&rotation);
        out.push_back(&scale_x);
        out.push_back(&scale_y);
        out.push_back(&tint_r);
        out.push_back(&tint_g);
        out.push_back(&tint_b);
        out.push_back(&tint_a);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture",  VIVID_PORT_TEXTURE,   VIVID_PORT_INPUT});
        out.push_back(vivid::gpu::drawable_port("drawable", VIVID_PORT_OUTPUT));
    }

    ~Sprite2D() override {
        vivid::gpu::release(fallback_sampler_);
    }

    void process_gpu(const VividGpuContext* ctx) override {
        // Require a connected texture input; otherwise emit nothing.
        if (ctx->input_texture_count == 0 || !ctx->input_texture_views ||
            !ctx->input_texture_views[0]) {
            return;
        }

        // Lazy-init a sampler shared across frames.
        if (!fallback_sampler_) {
            fallback_sampler_ = vivid::gpu::create_linear_sampler(ctx->device,
                                                                  "Sprite2D Sampler");
        }

        vivid::gpu::drawable_identity(output_);
        output_.type = vivid::gpu::VIVID_DRAWABLE2D_SPRITE;

        vivid::gpu::drawable_transform_trs(
            output_.transform,
            position_x.value, position_y.value,
            rotation.value,
            scale_x.value, scale_y.value);

        output_.color[0] = tint_r.value;
        output_.color[1] = tint_g.value;
        output_.color[2] = tint_b.value;
        output_.color[3] = tint_a.value;

        output_.texture_view    = ctx->input_texture_views[0];
        output_.texture_sampler = fallback_sampler_;
        output_.uv_rect[0] = 0.0f;
        output_.uv_rect[1] = 0.0f;
        output_.uv_rect[2] = 1.0f;
        output_.uv_rect[3] = 1.0f;

        ctx->custom_outputs[0] = &output_;
    }

private:
    vivid::gpu::VividDrawable2D output_{};
    WGPUSampler                 fallback_sampler_ = nullptr;
};

VIVID_DEFINE_OP(Sprite2D) {
}


VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividDrawable2D)
