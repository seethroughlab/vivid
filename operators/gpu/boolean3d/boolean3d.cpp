#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include <manifold/manifold.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <unordered_map>

using namespace vivid::gpu;

// =============================================================================
// Merge vector computation
//
// Manifold requires topologically valid (manifold) input. Meshes with split
// vertices (e.g. a cube with 24 verts for flat normals, but only 8 unique
// positions) need merge vectors telling Manifold which split vertices are
// topologically identical.
//
// We group vertices by spatial position (within epsilon), pick a canonical
// representative for each group, and emit mergeFromVert/mergeToVert pairs
// mapping non-canonical duplicates to their canonical vertex.
// =============================================================================

struct MergeVectors {
    std::vector<uint32_t> from;
    std::vector<uint32_t> to;
};

static MergeVectors compute_merge_vectors(const float* positions, uint32_t vert_count,
                                           uint32_t stride_floats, float epsilon = 1e-5f) {
    MergeVectors mv;
    if (vert_count == 0) return mv;

    // Spatial hashing for fast grouping
    float inv_cell = 1.0f / (epsilon * 4.0f);

    auto hash_pos = [&](float x, float y, float z) -> uint64_t {
        auto ix = static_cast<int64_t>(std::floor(x * inv_cell));
        auto iy = static_cast<int64_t>(std::floor(y * inv_cell));
        auto iz = static_cast<int64_t>(std::floor(z * inv_cell));
        uint64_t h = static_cast<uint64_t>(ix * 73856093LL ^ iy * 19349663LL ^ iz * 83492791LL);
        return h;
    };

    // Map from hash -> list of vertex indices in that cell
    std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
    cells.reserve(vert_count);

    for (uint32_t i = 0; i < vert_count; ++i) {
        const float* p = positions + i * stride_floats;
        uint64_t h = hash_pos(p[0], p[1], p[2]);
        cells[h].push_back(i);
    }

    // For each cell, find groups of matching vertices
    float eps2 = epsilon * epsilon;
    // canonical[i] = the canonical vertex for vertex i (initially itself)
    std::vector<uint32_t> canonical(vert_count);
    for (uint32_t i = 0; i < vert_count; ++i) canonical[i] = i;

    for (auto& [h, bucket] : cells) {
        for (size_t a = 0; a < bucket.size(); ++a) {
            uint32_t ia = bucket[a];
            if (canonical[ia] != ia) continue;  // already merged
            const float* pa = positions + ia * stride_floats;
            for (size_t b = a + 1; b < bucket.size(); ++b) {
                uint32_t ib = bucket[b];
                if (canonical[ib] != ib) continue;  // already merged
                const float* pb = positions + ib * stride_floats;
                float dx = pa[0] - pb[0], dy = pa[1] - pb[1], dz = pa[2] - pb[2];
                if (dx*dx + dy*dy + dz*dz < eps2) {
                    canonical[ib] = ia;
                }
            }
        }
    }

    // Emit merge pairs
    for (uint32_t i = 0; i < vert_count; ++i) {
        if (canonical[i] != i) {
            mv.from.push_back(i);
            mv.to.push_back(canonical[i]);
        }
    }
    return mv;
}

// =============================================================================
// Helper: transform vertex positions by a 4x4 matrix
// =============================================================================

static void transform_positions(std::vector<float>& positions, uint32_t count,
                                 uint32_t stride, const mat4x4 m) {
    for (uint32_t i = 0; i < count; ++i) {
        float* p = positions.data() + i * stride;
        float x = p[0], y = p[1], z = p[2];
        p[0] = m[0][0]*x + m[1][0]*y + m[2][0]*z + m[3][0];
        p[1] = m[0][1]*x + m[1][1]*y + m[2][1]*z + m[3][1];
        p[2] = m[0][2]*x + m[1][2]*y + m[2][2]*z + m[3][2];
    }
}

// =============================================================================
// Helper: build MeshGL from a scene fragment
// =============================================================================

static manifold::MeshGL build_meshgl(const VividSceneFragment* frag) {
    manifold::MeshGL mesh;
    mesh.numProp = 3;  // xyz positions only for boolean input

    uint32_t vc = frag->cpu_vertex_count;
    uint32_t ic = frag->cpu_index_count;

    // Pack positions into vertProperties (applying model_matrix to world space)
    mesh.vertProperties.resize(static_cast<size_t>(vc) * 3);
    for (uint32_t i = 0; i < vc; ++i) {
        const Vertex3D& v = frag->cpu_vertices[i];
        float x = v.position[0], y = v.position[1], z = v.position[2];
        const auto& m = frag->model_matrix;
        mesh.vertProperties[i*3 + 0] = m[0][0]*x + m[1][0]*y + m[2][0]*z + m[3][0];
        mesh.vertProperties[i*3 + 1] = m[0][1]*x + m[1][1]*y + m[2][1]*z + m[3][1];
        mesh.vertProperties[i*3 + 2] = m[0][2]*x + m[1][2]*y + m[2][2]*z + m[3][2];
    }

    // Copy triangle indices
    mesh.triVerts.assign(frag->cpu_indices, frag->cpu_indices + ic);

    // Compute merge vectors
    auto mv = compute_merge_vectors(mesh.vertProperties.data(), vc, 3);
    mesh.mergeFromVert = std::move(mv.from);
    mesh.mergeToVert = std::move(mv.to);

    return mesh;
}

// =============================================================================
// Boolean3D Operator
// =============================================================================

struct Boolean3D : vivid::OperatorBase {
    static constexpr const char* kName   = "Boolean3D";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   operation    {"operation", 0, {"Union", "Subtract", "Intersect"}};
    vivid::Param<float> smooth_angle {"smooth_angle", 60.0f, 0.0f, 180.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(operation, "Boolean");
        vivid::param_group(smooth_angle, "Boolean");
        out.push_back(&operation);
        out.push_back(&smooth_angle);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"scene_a", VIVID_PORT_GPU_SCENE, VIVID_PORT_INPUT});
        out.push_back({"scene_b", VIVID_PORT_GPU_SCENE, VIVID_PORT_INPUT});
        out.push_back({"scene",   VIVID_PORT_GPU_SCENE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        // Resolve inputs: scene_a = port 0, scene_b = port 1
        const VividSceneFragment* a = (gpu->input_scene_count > 0) ? gpu->input_scenes[0] : nullptr;
        const VividSceneFragment* b = (gpu->input_scene_count > 1) ? gpu->input_scenes[1] : nullptr;

        // Validate: need both inputs with CPU geometry
        if (!a || !a->cpu_vertices || !a->cpu_indices ||
            a->cpu_vertex_count == 0 || a->cpu_index_count == 0) {
            // No valid input A — nothing to output
            return;
        }

        if (!b || !b->cpu_vertices || !b->cpu_indices ||
            b->cpu_vertex_count == 0 || b->cpu_index_count == 0) {
            // No valid input B — pass through A
            fragment_ = *a;
            gpu->output_scene = &fragment_;
            return;
        }

        // Check if inputs changed (simple pointer comparison for cache invalidation)
        int op = operation.int_value();
        float angle = smooth_angle.value;
        bool inputs_changed = (a != cached_a_ || b != cached_b_ ||
                               op != cached_op_ || angle != cached_angle_);

        if (!inputs_changed && vertex_buffer_ && index_buffer_) {
            // Reuse cached result
            fragment_.vertex_buffer   = vertex_buffer_;
            fragment_.vertex_buf_size = vertex_buf_size_;
            fragment_.index_buffer    = index_buffer_;
            fragment_.index_count     = index_count_;
            fragment_.cpu_vertices    = cpu_verts_.data();
            fragment_.cpu_vertex_count = static_cast<uint32_t>(cpu_verts_.size());
            fragment_.cpu_indices     = cpu_indices_.data();
            fragment_.cpu_index_count = static_cast<uint32_t>(cpu_indices_.size());
            scene_fragment_identity(fragment_);
            fragment_.color[0] = a->color[0];
            fragment_.color[1] = a->color[1];
            fragment_.color[2] = a->color[2];
            fragment_.color[3] = a->color[3];
            fragment_.roughness = a->roughness;
            fragment_.metallic  = a->metallic;
            fragment_.emission  = a->emission;
            fragment_.unlit     = a->unlit;
            gpu->output_scene = &fragment_;
            return;
        }

        // Build MeshGL for each input
        manifold::MeshGL mesh_a = build_meshgl(a);
        manifold::MeshGL mesh_b = build_meshgl(b);

        // Construct Manifold objects
        manifold::Manifold man_a(mesh_a);
        if (man_a.Status() != manifold::Manifold::Error::NoError) {
            // Non-manifold input A — pass through unchanged
            fragment_ = *a;
            gpu->output_scene = &fragment_;
            return;
        }

        manifold::Manifold man_b(mesh_b);
        if (man_b.Status() != manifold::Manifold::Error::NoError) {
            // Non-manifold input B — pass through A
            fragment_ = *a;
            gpu->output_scene = &fragment_;
            return;
        }

        // Perform boolean operation
        manifold::Manifold result;
        switch (op) {
            case 0: result = man_a + man_b; break;  // Union
            case 1: result = man_a - man_b; break;  // Subtract
            case 2: result = man_a ^ man_b; break;  // Intersect
            default: result = man_a + man_b; break;
        }

        if (result.Status() != manifold::Manifold::Error::NoError) {
            // Boolean failed — pass through A
            fragment_ = *a;
            gpu->output_scene = &fragment_;
            return;
        }

        // Compute normals — bakes into properties 3–5
        result = result.CalculateNormals(3, static_cast<double>(angle));

        // Extract result mesh (now has 6 properties: xyz + normal xyz)
        manifold::MeshGL out_mesh = result.GetMeshGL();

        if (out_mesh.triVerts.empty()) {
            fragment_ = *a;
            gpu->output_scene = &fragment_;
            return;
        }

        // Rebuild Vertex3D array from Manifold output
        uint32_t num_props = static_cast<uint32_t>(out_mesh.numProp);
        uint32_t num_verts = static_cast<uint32_t>(out_mesh.vertProperties.size()) / num_props;
        uint32_t num_indices = static_cast<uint32_t>(out_mesh.triVerts.size());

        cpu_verts_.resize(num_verts);
        cpu_indices_.assign(out_mesh.triVerts.begin(), out_mesh.triVerts.end());

        for (uint32_t i = 0; i < num_verts; ++i) {
            Vertex3D& v = cpu_verts_[i];
            const float* props = out_mesh.vertProperties.data() + i * num_props;

            // Positions
            v.position[0] = props[0];
            v.position[1] = props[1];
            v.position[2] = props[2];

            // Normals (from CalculateNormals at indices 3-5)
            if (num_props >= 6) {
                v.normal[0] = props[3];
                v.normal[1] = props[4];
                v.normal[2] = props[5];
            } else {
                v.normal[0] = 0.0f;
                v.normal[1] = 1.0f;
                v.normal[2] = 0.0f;
            }

            // Tangent via cross-product with preferred axis
            // Choose axis least aligned with normal to avoid degenerate cross products
            float nx = v.normal[0], ny = v.normal[1], nz = v.normal[2];
            float ax, ay, az;
            if (std::fabs(ny) < 0.9f) {
                ax = 0.0f; ay = 1.0f; az = 0.0f;  // up
            } else {
                ax = 1.0f; ay = 0.0f; az = 0.0f;  // right
            }
            // tangent = normalize(cross(normal, axis))
            float tx = ny * az - nz * ay;
            float ty = nz * ax - nx * az;
            float tz = nx * ay - ny * ax;
            float tlen = std::sqrt(tx*tx + ty*ty + tz*tz);
            if (tlen > 1e-8f) { tx /= tlen; ty /= tlen; tz /= tlen; }
            else { tx = 1.0f; ty = 0.0f; tz = 0.0f; }
            v.tangent[0] = tx;
            v.tangent[1] = ty;
            v.tangent[2] = tz;
            v.tangent[3] = 1.0f;

            // UV via box projection (dominant axis selection)
            float anx = std::fabs(nx), any = std::fabs(ny), anz = std::fabs(nz);
            if (anx >= any && anx >= anz) {
                // project onto YZ
                v.uv[0] = v.position[2];
                v.uv[1] = v.position[1];
            } else if (any >= anz) {
                // project onto XZ
                v.uv[0] = v.position[0];
                v.uv[1] = v.position[2];
            } else {
                // project onto XY
                v.uv[0] = v.position[0];
                v.uv[1] = v.position[1];
            }
        }

        // Upload GPU buffers (recreate if size changed)
        vertex_buf_size_ = cpu_verts_.size() * sizeof(Vertex3D);
        index_count_ = num_indices;

        vivid::gpu::release(vertex_buffer_);
        vivid::gpu::release(index_buffer_);

        vertex_buffer_ = create_vertex_buffer(
            gpu->device, gpu->queue, cpu_verts_.data(), vertex_buf_size_, "Boolean3D VB");
        index_buffer_ = create_index_buffer(
            gpu->device, gpu->queue, cpu_indices_.data(), index_count_, "Boolean3D IB");

        // Cache state
        cached_a_     = a;
        cached_b_     = b;
        cached_op_    = op;
        cached_angle_ = angle;

        // Build output fragment — identity transform (positions already in world space)
        scene_fragment_identity(fragment_);
        fragment_.vertex_buffer    = vertex_buffer_;
        fragment_.vertex_buf_size  = vertex_buf_size_;
        fragment_.index_buffer     = index_buffer_;
        fragment_.index_count      = index_count_;
        fragment_.cpu_vertices     = cpu_verts_.data();
        fragment_.cpu_vertex_count = static_cast<uint32_t>(cpu_verts_.size());
        fragment_.cpu_indices      = cpu_indices_.data();
        fragment_.cpu_index_count  = static_cast<uint32_t>(cpu_indices_.size());

        // Material from input A
        fragment_.color[0]  = a->color[0];
        fragment_.color[1]  = a->color[1];
        fragment_.color[2]  = a->color[2];
        fragment_.color[3]  = a->color[3];
        fragment_.roughness = a->roughness;
        fragment_.metallic  = a->metallic;
        fragment_.emission  = a->emission;
        fragment_.unlit     = a->unlit;

        fragment_.pipeline       = nullptr;
        fragment_.material_binds = nullptr;

        gpu->output_scene = &fragment_;
    }

    ~Boolean3D() override {
        vivid::gpu::release(vertex_buffer_);
        vivid::gpu::release(index_buffer_);
    }

private:
    VividSceneFragment fragment_{};
    WGPUBuffer   vertex_buffer_  = nullptr;
    WGPUBuffer   index_buffer_   = nullptr;
    uint64_t     vertex_buf_size_ = 0;
    uint32_t     index_count_     = 0;
    std::vector<Vertex3D> cpu_verts_;
    std::vector<uint32_t> cpu_indices_;

    // Cache invalidation
    const VividSceneFragment* cached_a_ = nullptr;
    const VividSceneFragment* cached_b_ = nullptr;
    int   cached_op_    = -1;
    float cached_angle_ = -1.0f;
};

VIVID_REGISTER(Boolean3D)
