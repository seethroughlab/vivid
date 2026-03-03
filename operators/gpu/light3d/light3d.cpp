#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"

// =============================================================================
// Light3D — light source as a scene element
// =============================================================================

struct Light3D : vivid::OperatorBase {
    static constexpr const char* kName   = "Light3D";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   type      {"type",      0, {"Directional", "Point"}};
    vivid::Param<float> intensity {"intensity", 1.0f, 0.0f, 10.0f};
    vivid::Param<float> r         {"r",         1.0f, 0.0f, 1.0f};
    vivid::Param<float> g         {"g",         1.0f, 0.0f, 1.0f};
    vivid::Param<float> b         {"b",         1.0f, 0.0f, 1.0f};
    vivid::Param<float> radius    {"radius",   10.0f, 0.1f, 100.0f};
    vivid::Param<float> pos_x     {"pos_x",     0.5f, -50.0f, 50.0f};
    vivid::Param<float> pos_y     {"pos_y",     1.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_z     {"pos_z",     0.8f, -50.0f, 50.0f};

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

        out.push_back(&type);
        out.push_back(&intensity);
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&radius);
        out.push_back(&pos_x);
        out.push_back(&pos_y);
        out.push_back(&pos_z);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"scene", VIVID_PORT_GPU_SCENE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        fragment_.fragment_type   = vivid::gpu::VividSceneFragment::LIGHT;
        fragment_.light_type      = static_cast<float>(type.int_value());
        fragment_.light_color[0]  = r.value;
        fragment_.light_color[1]  = g.value;
        fragment_.light_color[2]  = b.value;
        fragment_.light_intensity = intensity.value;
        fragment_.light_radius    = radius.value;

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

        gpu->output_scene = &fragment_;
    }

private:
    vivid::gpu::VividSceneFragment fragment_{};
};

VIVID_REGISTER(Light3D)
