#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_2d.h"
#include <vector>

// =============================================================================
// Shape2D — emit a single SDF shape drawable
// =============================================================================

/**
 * @brief Emit one SDF shape (circle, polygon, or star) as a 2D drawable.
 *
 * First real consumer of the new drawable pipeline. Produces a VividDrawable2D
 * of type SHAPE with the configured geometry + transform + color. Render2D
 * rasterises the SDF within a bounded quad.
 *
 * Composable with Instancer2D to draw N copies, DrawableMerge to combine with
 * other drawables, and Render2D to rasterise to a texture.
 *
 * @param sides        Shape complexity. 0 = circle, 3 = triangle, 4 = square,
 *                     5 = pentagon, 6 = hexagon, ... (any positive N = N-gon).
 * @param star_factor  0 = regular polygon. >0 = N-point star with inner-vertex
 *                     inset proportional to this value.
 * @param softness     Edge softness for anti-aliasing / glow.
 * @param position_x / position_y  Center position in NDC (-1..1).
 * @param rotation     Rotation in radians.
 * @param scale_x / scale_y  Non-uniform scale.
 * @param r / g / b / a  RGBA color.
 *
 * @tip The starter source operator — pair with Render2D for one shape on screen.
 * @tip Feed into Instancer2D (with an InstanceArray2D generator) to draw N copies.
 * @recipe Shape2D -> Render2D -> video_out
 * @recipe Shape2D -> Instancer2D ← InstanceGrid2D -> Render2D
 * @common_companions Render2D, Instancer2D, InstanceGrid2D, DrawableMerge, Transform2D
 * @best_used_with Render2D, Instancer2D
 * @family 2D drawable pipeline
 * @see Render2D, Instancer2D, Sprite2D
 */
struct Shape2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Shape2D";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   sides       {"sides",       0,      0,   32};
    vivid::Param<float> star_factor {"star_factor", 0.0f,   0.0f, 1.0f};
    vivid::Param<float> softness    {"softness",    0.005f, 0.0f, 0.1f};

    vivid::Param<float> position_x  {"position_x",  0.0f, -2.0f, 2.0f};
    vivid::Param<float> position_y  {"position_y",  0.0f, -2.0f, 2.0f};
    vivid::Param<float> rotation    {"rotation",    0.0f, -6.2832f, 6.2832f};
    vivid::Param<float> scale_x     {"scale_x",     0.25f, 0.0f, 4.0f};
    vivid::Param<float> scale_y     {"scale_y",     0.25f, 0.0f, 4.0f};

    vivid::Param<float> r {"r", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> g {"g", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> b {"b", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> a {"a", 1.0f, 0.0f, 1.0f};

    Shape2D() {
        vivid::display_hint(r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(b, VIVID_DISPLAY_COLOR);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(sides,       "Shape");
        vivid::param_group(star_factor, "Shape");
        vivid::param_group(softness,    "Shape");
        vivid::param_group(position_x,  "Transform");
        vivid::param_group(position_y,  "Transform");
        vivid::param_group(rotation,    "Transform");
        vivid::param_group(scale_x,     "Transform");
        vivid::param_group(scale_y,     "Transform");
        vivid::param_group(r, "Color");
        vivid::param_group(g, "Color");
        vivid::param_group(b, "Color");
        vivid::param_group(a, "Color");

        out.push_back(&sides);
        out.push_back(&star_factor);
        out.push_back(&softness);
        out.push_back(&position_x);
        out.push_back(&position_y);
        out.push_back(&rotation);
        out.push_back(&scale_x);
        out.push_back(&scale_y);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&a);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::drawable_port("drawable", VIVID_PORT_OUTPUT));
    }

    void process_gpu(const VividGpuContext* ctx) override {
        vivid::gpu::drawable_identity(output_);
        output_.type = vivid::gpu::VIVID_DRAWABLE2D_SHAPE;

        vivid::gpu::drawable_transform_trs(
            output_.transform,
            position_x.value, position_y.value,
            rotation.value,
            scale_x.value, scale_y.value);

        output_.color[0] = r.value;
        output_.color[1] = g.value;
        output_.color[2] = b.value;
        output_.color[3] = a.value;

        output_.shape_sides       = static_cast<uint32_t>(sides.int_value());
        output_.shape_star_factor = star_factor.value;
        output_.shape_softness    = softness.value;

        ctx->custom_outputs[0] = &output_;
    }

private:
    vivid::gpu::VividDrawable2D output_{};
};

VIVID_REGISTER(Shape2D)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividDrawable2D)
