#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_2d.h"

// =============================================================================
// DrawableMerge — N drawable inputs → 1 composite drawable output
// =============================================================================

/**
 * @brief Merge multiple 2D drawables into a single composition tree.
 *
 * DrawableMerge is the 2D analog of SceneMerge: a trivial pass-through
 * structural utility that collects non-null drawable inputs into a single
 * output drawable's `children` array. Render2D walks the tree recursively.
 *
 * Connect up to 4 drawables into scene_a..scene_d; unconnected inputs are
 * ignored. Useful when composing separate emitters (e.g. a sprite stack
 * plus a text overlay) before feeding Render2D.
 *
 * @tip Use when you need two or more drawable sources composited together (e.g. shapes + sprites).
 * @tip Child draw order follows input index (scene_a first, then b, c, d).
 * @recipe Shape2D, Sprite2D -> DrawableMerge -> Render2D -> video_out
 * @common_companions Render2D, Shape2D, Sprite2D, Instancer2D
 * @best_used_with Render2D
 * @family 2D drawable pipeline
 * @see Render2D, Shape2D
 */
struct DrawableMerge : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "DrawableMerge";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::drawable_port("drawable_a", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::drawable_port("drawable_b", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::drawable_port("drawable_c", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::drawable_port("drawable_d", VIVID_PORT_INPUT));
        out.push_back(vivid::gpu::drawable_port("drawable",   VIVID_PORT_OUTPUT));
    }

    void process_gpu(const VividGpuContext* ctx) override {
        child_count_ = 0;
        for (uint32_t i = 0; i < ctx->custom_input_count && child_count_ < 4; ++i) {
            auto* d = vivid::gpu::drawable_input(ctx, i);
            if (d) {
                children_[child_count_++] = d;
            }
        }

        if (child_count_ == 0) return;

        vivid::gpu::drawable_identity(output_);
        output_.type         = vivid::gpu::VIVID_DRAWABLE2D_SPRITE;  // arbitrary; children-only node
        output_.children     = children_;
        output_.child_count  = child_count_;

        ctx->custom_outputs[0] = &output_;
    }

private:
    vivid::gpu::VividDrawable2D  output_{};
    vivid::gpu::VividDrawable2D* children_[4]{};
    uint32_t                     child_count_ = 0;
};

VIVID_DEFINE_OP(DrawableMerge) {
}

VIVID_REGISTER(DrawableMerge)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividDrawable2D)
