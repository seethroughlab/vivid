#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include <cstring>
#include <cmath>

// =============================================================================
// Transform3D — scene-in, scene-out with TRS transform
// =============================================================================

struct Transform3D : vivid::OperatorBase {
    static constexpr const char* kName   = "Transform3D";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> pos_x   {"pos_x",   0.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_y   {"pos_y",   0.0f, -50.0f, 50.0f};
    vivid::Param<float> pos_z   {"pos_z",   0.0f, -50.0f, 50.0f};
    vivid::Param<float> rot_x   {"rot_x",   0.0f, -6.283f, 6.283f};
    vivid::Param<float> rot_y   {"rot_y",   0.0f, -6.283f, 6.283f};
    vivid::Param<float> rot_z   {"rot_z",   0.0f, -6.283f, 6.283f};
    vivid::Param<float> scale_x {"scale_x", 1.0f,  0.01f, 50.0f};
    vivid::Param<float> scale_y {"scale_y", 1.0f,  0.01f, 50.0f};
    vivid::Param<float> scale_z {"scale_z", 1.0f,  0.01f, 50.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(pos_x, "Transform");
        vivid::param_group(pos_y, "Transform");
        vivid::param_group(pos_z, "Transform");
        vivid::param_group(rot_x, "Transform");
        vivid::param_group(rot_y, "Transform");
        vivid::param_group(rot_z, "Transform");
        vivid::param_group(scale_x, "Transform");
        vivid::param_group(scale_y, "Transform");
        vivid::param_group(scale_z, "Transform");

        out.push_back(&pos_x);
        out.push_back(&pos_y);
        out.push_back(&pos_z);
        out.push_back(&rot_x);
        out.push_back(&rot_y);
        out.push_back(&rot_z);
        out.push_back(&scale_x);
        out.push_back(&scale_y);
        out.push_back(&scale_z);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"scene", VIVID_PORT_GPU_SCENE, VIVID_PORT_INPUT});
        out.push_back({"scene", VIVID_PORT_GPU_SCENE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        // No input scene → no output
        bool has_input = gpu->input_scene_count > 0 && gpu->input_scenes &&
                         gpu->input_scenes[0] != nullptr;
        if (!has_input) return;

        // Build TRS matrix: T * Rz * Ry * Rx * S (same order as Shape3D)
        mat4x4 S, tmp;
        mat4x4_identity(S);
        mat4x4_scale_aniso(S, S, scale_x.value, scale_y.value, scale_z.value);
        mat4x4_rotate_X(tmp, S, rot_x.value);
        mat4x4_rotate_Y(S, tmp, rot_y.value);
        mat4x4_rotate_Z(tmp, S, rot_z.value);

        mat4x4 T;
        mat4x4_translate(T, pos_x.value, pos_y.value, pos_z.value);
        mat4x4_mul(output_.model_matrix, T, tmp);

        // No geometry on this fragment — just a transform wrapper
        output_.vertex_buffer   = nullptr;
        output_.vertex_buf_size = 0;
        output_.index_buffer    = nullptr;
        output_.index_count     = 0;
        output_.pipeline        = nullptr;
        output_.material_binds  = nullptr;
        output_.fragment_type   = vivid::gpu::VividSceneFragment::GEOMETRY;

        // Wrap input as child
        child_ = gpu->input_scenes[0];
        output_.children    = &child_;
        output_.child_count = 1;

        gpu->output_scene = &output_;
    }

private:
    vivid::gpu::VividSceneFragment  output_{};
    vivid::gpu::VividSceneFragment* child_ = nullptr;
};

VIVID_REGISTER(Transform3D)
