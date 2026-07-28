#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

// =============================================================================
// Light3D — light source as a scene element
// =============================================================================

/**
 * @brief Defines a configurable 3D light source for the scene pipeline.
 *
 * Light3D creates directional, point, or spot lights with adjustable color, placement, and
 * intensity so scenes can be lit without embedding light logic inside geometry operators.
 *
 * @param type Light type: directional, point, or spot.
 * @param intensity Overall light intensity.
 * @param radius Influence radius for local lights.
 * @param pos_x Light position along the X axis.
 * @param dir_y Y component of the light direction.
 * @param spot_angle Cone angle for spot lights.
 */
struct Light3D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Light3D";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   type      {"type",      0, {"Directional", "Point", "Spot"}};
    vivid::Param<float> intensity {"intensity", 1.0f, 0.0f, 10.0f};
    vivid::Param<float> r         {"r",         1.0f, 0.0f, 1.0f};
    vivid::Param<float> g         {"g",         1.0f, 0.0f, 1.0f};
    vivid::Param<float> b         {"b",         1.0f, 0.0f, 1.0f};
    vivid::Param<float> radius    {"radius",   10.0f, 0.1f, 100.0f};
    vivid::Param<float> pos_x     {"pos_x",     0.5f, -50.0f, 50.0f};
    vivid::Param<float> pos_y     {"pos_y",     1.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_z     {"pos_z",     0.8f, -50.0f, 50.0f};

    // Spot light direction
    vivid::Param<float> dir_x     {"dir_x",     0.0f, -1.0f, 1.0f};
    vivid::Param<float> dir_y     {"dir_y",    -1.0f, -1.0f, 1.0f};
    vivid::Param<float> dir_z     {"dir_z",     0.0f, -1.0f, 1.0f};

    // Spot light cone params
    vivid::Param<float> spot_angle {"spot_angle", 45.0f, 5.0f, 90.0f};
    vivid::Param<float> spot_blend {"spot_blend", 0.1f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(type, "Light");
        vivid::param_group(intensity, "Light");
        vivid::param_group(radius, "Light");

        vivid::param_group(r, "Color");
        vivid::param_group(g, "Color");
        vivid::param_group(b, "Color");
        vivid::display_hint(r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(b, VIVID_DISPLAY_COLOR);

        vivid::param_group(pos_x, "Position");
        vivid::param_group(pos_y, "Position");
        vivid::param_group(pos_z, "Position");

        vivid::param_group(dir_x, "Direction");
        vivid::param_group(dir_y, "Direction");
        vivid::param_group(dir_z, "Direction");

        vivid::param_group(spot_angle, "Spot");
        vivid::param_group(spot_blend, "Spot");

        out.push_back(&type);
        out.push_back(&intensity);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&radius);
        out.push_back(&pos_x);
        out.push_back(&pos_y);
        out.push_back(&pos_z);
        out.push_back(&dir_x);
        out.push_back(&dir_y);
        out.push_back(&dir_z);
        out.push_back(&spot_angle);
        out.push_back(&spot_blend);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::scene_port("scene", VIVID_PORT_OUTPUT));
    }

    void draw_thumbnail(const VividThumbnailContext*) override { /* TODO(ADR-0041 Phase 1): reimplement against trunk 2D VividDrawAPI */ }

    void process_gpu(const VividGpuContext* ctx) override {
        fragment_.fragment_type   = vivid::gpu::VividSceneFragment::LIGHT;
        fragment_.light_type      = static_cast<float>(type.int_value());
        fragment_.light_color[0]  = r.value;
        fragment_.light_color[1]  = g.value;
        fragment_.light_color[2]  = b.value;
        fragment_.light_intensity = intensity.value;
        fragment_.light_radius    = radius.value;

        // Spot light params
        fragment_.light_direction[0] = dir_x.value;
        fragment_.light_direction[1] = dir_y.value;
        fragment_.light_direction[2] = dir_z.value;
        fragment_.light_spot_angle   = spot_angle.value;
        fragment_.light_spot_blend   = spot_blend.value;

        // Position/direction encoded in model_matrix translation
        mat4x4_translate(fragment_.model_matrix, pos_x.value, pos_y.value, pos_z.value);

        // No geometry
        fragment_.vertex_buffer   = nullptr;
        fragment_.vertex_buf_size = 0;
        fragment_.index_buffer    = nullptr;
        fragment_.index_count     = 0;
        fragment_.pipeline        = nullptr;
        fragment_.material_binds  = nullptr;
        fragment_.children        = nullptr;
        fragment_.child_count     = 0;

        ctx->custom_outputs[0] = &fragment_;
    }

private:
    vivid::gpu::VividSceneFragment fragment_{};
};

VIVID_REGISTER(Light3D)
VIVID_THUMBNAIL(Light3D)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividSceneFragment)
