#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include "operator_api/thumbnail_3d.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// =============================================================================
// Instancer3D — attach a per-instance bundle to a 3D scene fragment
// =============================================================================

/**
 * @brief Attach an InstanceArray3D bundle to a scene fragment for instanced draws.
 *
 * Takes a `scene` input (VividSceneFragment carrying mesh + material) plus an
 * `instances` input (InstanceArray3D carrying per-instance transforms +
 * colours) and emits a scene fragment whose `instance_buffer` / `instance_count`
 * are populated. MeshDraw / Render3D issue a single instanced draw from that
 * buffer.
 *
 * Layout generation is no longer done inside this operator — use InstanceGrid
 * for Grid / Circle / Line / Grid3D, InstanceNoise for jitter, and
 * InstancesFromLanes to migrate per-attribute lane-array signal sources into
 * the bundle. See `docs/plans/2d-pipeline-redesign.md` for the canonical
 * recipe and Phase B history.
 *
 * @tip Canonical 3-node pattern: Shape3D -> Instancer3D <- InstanceGrid.
 * @recipe Shape3D -> Instancer3D <- InstanceGrid -> SceneMerge -> Render3D
 * @common_companions InstanceGrid, InstanceNoise, InstancesFromLanes, MeshDraw, Render3D
 * @family 3D instancing
 */
struct Instancer3D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Instancer3D";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_TRANSFORM;   // ADR-0046
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_KERNEL;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::scene_port("scene", VIVID_PORT_INPUT));              // 0
        out.push_back(VIVID_CUSTOM_REF_PORT("instances", VIVID_PORT_INPUT,
                                            vivid::gpu::InstanceArray3D));             // 1
        out.push_back(vivid::gpu::scene_port("scene", VIVID_PORT_OUTPUT));
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override { /* TODO(ADR-0041 Phase 1): reimplement vs trunk 2D VividDrawAPI */ }

    void process_gpu(const VividGpuContext* ctx) override {
        // Require a scene input.
        if (ctx->custom_input_count == 0 || !vivid::gpu::scene_input(ctx, 0)) return;
        const auto* input = vivid::gpu::scene_input(ctx, 0);
        if (!input->vertex_buffer || input->index_count == 0) return;

        // Per-instance bundle — without it, we pass the scene through single-instance.
        const vivid::gpu::InstanceArray3D* bundle = nullptr;
        if (ctx->custom_input_count > 1 && ctx->custom_inputs && ctx->custom_inputs[1]) {
            bundle = static_cast<const vivid::gpu::InstanceArray3D*>(ctx->custom_inputs[1]);
        }
        if (!bundle || !bundle->data || bundle->count == 0) {
            fragment_ = *input;
            ctx->custom_outputs[0] = &fragment_;
            // Thumbnail: the un-instanced base mesh.
            float bmin[3], bmax[3];
            if (vivid::thumb3d::aabb_from_verts(input->cpu_vertices, input->cpu_vertex_count, bmin, bmax))
                vivid::thumb3d::render(ctx, thumb_, fragment_.vertex_buffer, fragment_.vertex_buf_size,
                                       fragment_.index_buffer, fragment_.index_count, bmin, bmax, fragment_.color);
            return;
        }

        uint32_t n = bundle->count;
        if (n > 4096) n = 4096;
        instances_.assign(bundle->data, bundle->data + n);

        uint32_t buf_size = n * sizeof(vivid::gpu::InstanceData3D);
        if (buf_size < 48) buf_size = 48;
        if (n != current_count_) {
            rebuild_storage(ctx, n, buf_size);
        }
        if (storage_buf_) {
            wgpuQueueWriteBuffer(ctx->queue, storage_buf_, 0,
                                 instances_.data(),
                                 n * sizeof(vivid::gpu::InstanceData3D));
        }

        fragment_ = *input;
        fragment_.instance_buffer = storage_buf_;
        fragment_.instance_count  = n;
        ctx->custom_outputs[0] = &fragment_;

        // Animated 3D thumbnail: the base mesh drawn once per instance transform.
        float mr = 0.6f, mb[3], mx[3];
        if (vivid::thumb3d::aabb_from_verts(input->cpu_vertices, input->cpu_vertex_count, mb, mx)) {
            mr = 0.5f * std::sqrt((mx[0]-mb[0])*(mx[0]-mb[0]) + (mx[1]-mb[1])*(mx[1]-mb[1]) + (mx[2]-mb[2])*(mx[2]-mb[2]));
            if (mr < 1e-3f) mr = 0.6f;
        }
        vivid::thumb3d::render_instanced(ctx, thumb_, fragment_.vertex_buffer, fragment_.vertex_buf_size,
                                         fragment_.index_buffer, fragment_.index_count, mr,
                                         instances_.data(), n, nullptr);
    }

    ~Instancer3D() override {
        vivid::gpu::release(storage_buf_);
        vivid::thumb3d::destroy(thumb_);
    }

private:
    vivid::gpu::VividSceneFragment fragment_{};
    vivid::thumb3d::State thumb_{};
    std::vector<vivid::gpu::InstanceData3D> instances_;
    WGPUBuffer storage_buf_   = nullptr;
    uint32_t   current_count_ = 0;

    void rebuild_storage(const VividGpuContext* ctx, uint32_t count, uint32_t buf_size) {
        vivid::gpu::release(storage_buf_);
        current_count_ = count;

        if (count == 0) return;

        WGPUBufferDescriptor desc{};
        desc.label = vivid_sv("Instancer3D Storage");
        desc.size  = buf_size;
        desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        storage_buf_ = wgpuDeviceCreateBuffer(ctx->device, &desc);
    }
};

VIVID_REGISTER(Instancer3D)
VIVID_THUMBNAIL(Instancer3D)

VIVID_DESCRIBE_REF_TYPES2(vivid::gpu::VividSceneFragment, vivid::gpu::InstanceArray3D)
