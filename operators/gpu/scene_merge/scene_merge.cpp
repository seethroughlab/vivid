#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"

// =============================================================================
// SceneMerge — N scene inputs → 1 combined scene output
// =============================================================================

struct SceneMerge : vivid::OperatorBase {
    static constexpr const char* kName   = "SceneMerge";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"scene_a", VIVID_PORT_GPU_SCENE, VIVID_PORT_INPUT});
        out.push_back({"scene_b", VIVID_PORT_GPU_SCENE, VIVID_PORT_INPUT});
        out.push_back({"scene_c", VIVID_PORT_GPU_SCENE, VIVID_PORT_INPUT});
        out.push_back({"scene_d", VIVID_PORT_GPU_SCENE, VIVID_PORT_INPUT});
        out.push_back({"scene",   VIVID_PORT_GPU_SCENE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        // Collect non-null scene inputs
        child_count_ = 0;
        for (uint32_t i = 0; i < gpu->input_scene_count && child_count_ < 4; ++i) {
            if (gpu->input_scenes[i]) {
                children_[child_count_++] = gpu->input_scenes[i];
            }
        }

        if (child_count_ == 0) return;

        // Output fragment: identity transform, no geometry, children = collected inputs
        vivid::gpu::scene_fragment_identity(output_);
        output_.vertex_buffer   = nullptr;
        output_.vertex_buf_size = 0;
        output_.index_buffer    = nullptr;
        output_.index_count     = 0;
        output_.pipeline        = nullptr;
        output_.material_binds  = nullptr;
        output_.fragment_type   = vivid::gpu::VividSceneFragment::GEOMETRY;
        output_.children        = children_;
        output_.child_count     = child_count_;

        gpu->output_scene = &output_;
    }

private:
    vivid::gpu::VividSceneFragment  output_{};
    vivid::gpu::VividSceneFragment* children_[4]{};
    uint32_t                        child_count_ = 0;
};

VIVID_REGISTER(SceneMerge)
