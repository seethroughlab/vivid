#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include "operator_api/thumbnail_3d.h"
#include <algorithm>
#include <cstdio>

// =============================================================================
// SceneMerge — N scene inputs → 1 combined scene output
// =============================================================================

/**
 * @brief Merges multiple 3D scene inputs into a single scene stream.
 *
 * SceneMerge is a structural utility for combining geometry, lights, and scene fragments before
 * they are passed into Render3D or downstream post-processing operators.
 */
struct SceneMerge : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "SceneMerge";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::scene_port("scene_a", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::scene_port("scene_b", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::scene_port("scene_c", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::scene_port("scene_d", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::scene_port("scene",   VIVID_PORT_OUTPUT));
    }

    void draw_thumbnail(const VividThumbnailContext*) override { /* TODO(ADR-0041 Phase 1): reimplement against trunk 2D VividDrawAPI */ }

    void process_gpu(const VividGpuContext* ctx) override {
        // Collect non-null scene inputs
        child_count_ = 0;
        for (uint32_t i = 0; i < ctx->custom_input_count && child_count_ < 4; ++i) {
            auto* s = vivid::gpu::scene_input(ctx, i);
            if (s) {
                children_[child_count_++] = s;
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

        ctx->custom_outputs[0] = &output_;

        // Animated 3D thumbnail: two overlapping proxy spheres evoke compositing.
        vivid::thumb3d::render_merge_proxy(ctx, thumb_, nullptr);
    }

    ~SceneMerge() override { vivid::thumb3d::destroy(thumb_); }

private:
    vivid::gpu::VividSceneFragment  output_{};
    vivid::gpu::VividSceneFragment* children_[4]{};
    uint32_t                        child_count_ = 0;
    vivid::thumb3d::State           thumb_{};
};

VIVID_REGISTER(SceneMerge)
VIVID_THUMBNAIL(SceneMerge)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividSceneFragment)
