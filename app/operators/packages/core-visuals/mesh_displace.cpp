// Core visual package operator: MeshDisplace — the custom GEOMETRY MODIFIER. Takes a mesh (custom-
// ref input) AND a 2D texture (the displace map), runs a GPU COMPUTE pass that pushes every vertex
// along its normal by the map's luminance, and outputs a new mesh. It is the node the geometry-
// operator upgrade exists to make possible: a custom node that MODIFIES geometry mid-pipeline
// (MeshLoad -> MeshDisplace -> MeshRender) without re-implementing loading or rendering, and does
// the heavy per-vertex work on the GPU (first user of gpu_common.h's compute helpers).
//
// Mapping: a mid-pipeline modifier has no camera (that lives in MeshRender), so screen-exact mapping
// isn't available here. mode 0 (default) = object-space XY projection, which reads like a screen-
// space field when the fixed camera faces the model; mode 1 = the mesh's own UVs (a surface height
// map). `amount` is audio-mappable. The output vbo is owned by this op; the index buffer (topology)
// passes through from the source unchanged.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/geom.h"          // input_mesh / publish_mesh
#include "operator_api/mesh_render.h"   // mesh_vertex_attributes + MeshVertex stride

#include <array>
#include <cstdint>
#include <string>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

const char* kDisplaceWGSL = R"(
struct U { amount: f32, scale: f32, mode: f32, _pad0: f32, count: u32, _pad1: u32, _pad2: u32, _pad3: u32 };
@group(0) @binding(0) var<storage, read>       src:     array<f32>;   // 8 floats/vertex: pos,nrm,uv
@group(0) @binding(1) var<storage, read_write> dst:     array<f32>;
@group(0) @binding(2) var mapTex:  texture_2d<f32>;
@group(0) @binding(3) var mapSamp: sampler;
@group(0) @binding(4) var<uniform> u: U;
@compute @workgroup_size(64)
fn cs_main(@builtin(global_invocation_id) gid: vec3u) {
    let i = gid.x;
    if (i >= u.count) { return; }
    let base = i * 8u;
    let pos = vec3f(src[base+0u], src[base+1u], src[base+2u]);
    let nrm = vec3f(src[base+3u], src[base+4u], src[base+5u]);
    let uv  = vec2f(src[base+6u], src[base+7u]);
    // mode 0: object-space XY projection (reads like a screen-space field for the fixed camera);
    // mode 1: the mesh's own UVs (surface height map). `scale` tiles/zooms the projected field.
    var suv = pos.xy * (0.5 + u.scale) + 0.5;
    if (u.mode > 0.5) { suv = uv; }
    let s = textureSampleLevel(mapTex, mapSamp, suv, 0.0).rgb;
    let lum = dot(s, vec3f(0.299, 0.587, 0.114));
    let d = (lum - 0.5) * 2.0 * u.amount;      // signed push along the normal
    let disp = pos + nrm * d;
    dst[base+0u] = disp.x; dst[base+1u] = disp.y; dst[base+2u] = disp.z;
    dst[base+3u] = nrm.x;  dst[base+4u] = nrm.y;  dst[base+5u] = nrm.z;
    dst[base+6u] = uv.x;   dst[base+7u] = uv.y;
}
)";
}  // namespace

struct MeshDisplaceOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "MeshDisplace";
    static constexpr const char* kDisplayName = "Mesh Displace";
    static constexpr const char* kSummary = "Displace a mesh's vertices by a 2D texture (GPU compute); mesh in + map in, mesh out.";
    static constexpr std::array<const char*, 3> kKeywords = {"geometry", "3d", "distort"};

    vivid::Param<float> amount{"amount", 0.25f, 0.f, 1.f};   // displacement strength (map an instrument here)
    vivid::Param<float> scale{"scale", 0.f, 0.f, 1.f};       // projected-field tiling/zoom (mode 0)
    vivid::Param<float> mode{"mode", 0.f, 0.f, 1.f};         // 0 = object-space XY, 1 = mesh UVs

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&amount); o.push_back(&scale); o.push_back(&mode);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(VIVID_CUSTOM_REF_PORT("mesh", VIVID_PORT_INPUT, VividMesh));
        o.push_back(tex_port("map", VIVID_PORT_INPUT));
        o.push_back(VIVID_CUSTOM_REF_PORT("mesh", VIVID_PORT_OUTPUT, VividMesh));
    }

    ~MeshDisplaceOp() override { release(); }

    void process_gpu(const VividGpuContext* c) override {
        if (init_failed_) { vivid_report_gpu_error(c, err_.c_str()); return; }
        const VividMesh* src = vivid::geom::input_mesh(c, 0);
        if (!src || !src->vertex_buffer || src->vertex_count == 0) return;   // nothing wired in -> publish nothing
        if (!pipe_ && !lazy_init(c)) { init_failed_ = true; vivid_report_gpu_error(c, err_.c_str()); return; }

        // The displace map: input_texture_views[0] (fallback = black when unwired -> no displacement).
        WGPUTextureView map = (c->input_texture_count > 0 && c->input_texture_views) ? c->input_texture_views[0] : nullptr;
        ensure_dst(c, src->vertex_count);
        rebuild_bind_group(c, src, map);

        const float* p = c->param_values; auto pv = [&](int i, float d){ return p ? p[i] : d; };
        struct { float amount, scale, mode, pad0; uint32_t count, pad1, pad2, pad3; } u{};
        u.amount = pv(0, amount.value) * 0.5f;   // 0..1 -> up to ~0.5 unit-radius push
        u.scale  = pv(1, scale.value) * 1.5f;
        u.mode   = pv(2, mode.value);
        u.count  = src->vertex_count;
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, &u, sizeof(u));

        const uint32_t groups = (src->vertex_count + 63u) / 64u;
        vivid::gpu::dispatch_compute(c->command_encoder, pipe_, bg_, groups, "MeshDisplace");

        // Publish a mesh over our displaced vbo, carrying the source's topology/indices unchanged.
        uint32_t nattr = 0; const VividVertexAttribute* attrs = vivid::geom::mesh_vertex_attributes(nattr);
        out_ = VividMesh{};
        out_.vertex_buffer = dst_; out_.vertex_buffer_offset = 0;
        out_.vertex_count = src->vertex_count; out_.vertex_stride = (uint32_t)sizeof(vivid::geom::MeshVertex);
        out_.index_buffer = src->index_buffer; out_.index_format = src->index_format; out_.index_count = src->index_count;
        out_.topology = src->topology; out_.attributes = attrs; out_.attribute_count = nattr;
        out_.base_color = src->base_color;   // UVs are preserved, so the material passes through unchanged
        vivid::geom::publish_mesh(c, 0, &out_);
    }

private:
    WGPUShaderModule    sh_  = nullptr;  WGPUBindGroupLayout bgl_ = nullptr;  WGPUPipelineLayout pl_ = nullptr;
    WGPUComputePipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer dst_ = nullptr; uint32_t dst_verts_ = 0;
    WGPUBuffer   bound_src_ = nullptr; WGPUTextureView bound_map_ = nullptr;   // what bg_ currently references
    VividMesh out_{};
    bool init_failed_ = false; std::string err_;

    void release() {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (dst_) wgpuBufferRelease(dst_);
        if (pipe_) wgpuComputePipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }

    void ensure_dst(const VividGpuContext* c, uint32_t verts) {
        if (dst_ && dst_verts_ == verts) return;
        if (dst_) { wgpuBufferRelease(dst_); dst_ = nullptr; }
        WGPUBufferDescriptor bd{};
        bd.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        bd.size = (uint64_t)verts * sizeof(vivid::geom::MeshVertex);
        dst_ = wgpuDeviceCreateBuffer(c->device, &bd);
        dst_verts_ = verts;
        bound_src_ = nullptr;   // force a bind-group rebuild (dst changed)
    }
    void rebuild_bind_group(const VividGpuContext* c, const VividMesh* src, WGPUTextureView map) {
        if (bg_ && bound_src_ == src->vertex_buffer && bound_map_ == map) return;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[5]{};
        be[0].binding = 0; be[0].buffer = src->vertex_buffer; be[0].offset = src->vertex_buffer_offset;
        be[0].size = (uint64_t)src->vertex_count * sizeof(vivid::geom::MeshVertex);
        be[1].binding = 1; be[1].buffer = dst_; be[1].offset = 0; be[1].size = (uint64_t)dst_verts_ * sizeof(vivid::geom::MeshVertex);
        be[2].binding = 2; be[2].textureView = map;
        be[3].binding = 3; be[3].sampler = samp_;
        be[4].binding = 4; be[4].buffer = ubo_; be[4].size = 32;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 5; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        bound_src_ = src->vertex_buffer; bound_map_ = map;
    }
    bool lazy_init(const VividGpuContext* c) {
        sh_ = vivid::gpu::create_compute_shader(c->device, kDisplaceWGSL, "MeshDisplace");
        if (!sh_) { err_ = "compute shader null"; return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "MeshDisplace U");
        WGPUBindGroupLayoutEntry e[5]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Compute; e[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Compute; e[1].buffer.type = WGPUBufferBindingType_Storage;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Compute;
        e[2].texture.sampleType = WGPUTextureSampleType_Float; e[2].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[3].binding = 3; e[3].visibility = WGPUShaderStage_Compute; e[3].sampler.type = WGPUSamplerBindingType_Filtering;
        e[4].binding = 4; e[4].visibility = WGPUShaderStage_Compute; e[4].buffer.type = WGPUBufferBindingType_Uniform; e[4].buffer.minBindingSize = 32;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 5; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_compute_pipeline(c->device, sh_, pl_, "MeshDisplace");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        if (!pipe_) { err_ = "compute pipeline null"; return false; }
        return true;
    }
};

VIVID_REGISTER(MeshDisplaceOp)
