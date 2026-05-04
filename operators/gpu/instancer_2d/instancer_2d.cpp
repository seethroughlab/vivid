#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_2d.h"
#include <cstdint>
#include <cstdio>

// =============================================================================
// Instancer2D — drawable template + InstanceArray2D bundle → instanced drawable
// =============================================================================

/**
 * @brief Attach a per-instance transform bundle to a drawable template.
 *
 * Consumes a VividDrawable2D (the visual content — shape params, color,
 * texture) and an InstanceArray2D (per-instance transforms) and emits a
 * VividDrawable2D with `instance_buffer` and `instance_count` set. Render2D
 * detects the buffer and issues a single DrawIndexed(6, N) call, pulling
 * per-instance data from the storage buffer in the vertex shader.
 *
 * When the instances input is unconnected, the template drawable passes
 * through unchanged (single-instance fallback).
 *
 * @tip The terminal instancing node — two inputs (drawable template + instances).
 * @tip If you only need to move a single drawable, skip Instancer2D and use Transform2D directly.
 * @recipe InstanceGrid2D -> Instancer2D ← Shape2D -> Render2D
 * @common_companions InstanceGrid2D, InstanceNoise2D, InstancesFromLanes2D, Shape2D, Render2D
 * @best_used_with Render2D, InstanceGrid2D
 * @family 2D drawable pipeline
 * @see InstanceGrid2D, InstanceNoise2D, InstancesFromLanes2D, Transform2D
 */
struct Instancer2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName         = "Instancer2D";
    static constexpr bool kTimeDependent       = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_KERNEL;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::drawable_port("drawable",       VIVID_PORT_INPUT));   // 0 (custom 0)
        out.push_back(vivid::gpu::instance_array_port("instances", VIVID_PORT_INPUT));  // 1 (custom 1)
        out.push_back(vivid::gpu::drawable_port("drawable",       VIVID_PORT_OUTPUT));
    }

    ~Instancer2D() override {
        vivid::gpu::release(storage_buf_);
    }

    void process_gpu(const VividGpuContext* ctx) override {
        const auto* tmpl = vivid::gpu::drawable_input(ctx, 0);
        if (!tmpl) return;

        // Fall-through when no instance bundle is connected — pass the template straight out.
        const auto* bundle = vivid::gpu::instance_array_input(ctx, 1);
        if (!bundle || !bundle->data || bundle->count == 0) {
            output_ = *tmpl;
            ctx->custom_outputs[0] = &output_;
            return;
        }

        // Upload the per-instance records to a storage buffer.
        uint32_t n = bundle->count;
        if (n > 4096) n = 4096;
        const uint64_t bytes = static_cast<uint64_t>(n) * sizeof(vivid::gpu::InstanceData2D);

        if (n != current_count_) {
            rebuild_storage(ctx, n);
        }
        if (storage_buf_ && bytes > 0) {
            wgpuQueueWriteBuffer(ctx->queue, storage_buf_, 0, bundle->data, bytes);
        }

        // Shallow copy of the template, then override instance fields.
        output_ = *tmpl;
        output_.instance_buffer = storage_buf_;
        output_.instance_count  = n;
        ctx->custom_outputs[0]  = &output_;
    }

private:
    vivid::gpu::VividDrawable2D output_{};
    WGPUBuffer                  storage_buf_    = nullptr;
    uint32_t                    current_count_  = 0;

    void rebuild_storage(const VividGpuContext* ctx, uint32_t count) {
        vivid::gpu::release(storage_buf_);
        current_count_ = count;
        if (count == 0) return;
        uint64_t bytes = static_cast<uint64_t>(count) * sizeof(vivid::gpu::InstanceData2D);
        if (bytes < 48) bytes = 48;

        WGPUBufferDescriptor bd{};
        bd.label = vivid_sv("Instancer2D Storage");
        bd.size  = bytes;
        bd.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        storage_buf_ = wgpuDeviceCreateBuffer(ctx->device, &bd);
    }
};

VIVID_DEFINE_OP(Instancer2D) {
}

VIVID_REGISTER(Instancer2D)

VIVID_DESCRIBE_REF_TYPES2(vivid::gpu::VividDrawable2D, vivid::gpu::InstanceArray2D)
