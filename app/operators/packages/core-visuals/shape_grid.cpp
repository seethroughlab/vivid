// Core visual package operator: ShapeGrid — a grid of filled polygons drawn as REAL
// vertex geometry (triangle fans from a vertex buffer, not a fullscreen SDF). Migrated
// verbatim from the built-in ShapeGridOp (builtin_ops.cpp); behaviour unchanged.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
struct GVert { float cx, cy, ox, oy; };   // cell center (NDC) + n-gon offset (unit)
const char* kShapeGridWGSL = R"(
struct U { res: vec2f, time: f32, size: f32, rotation: f32, pad0: f32, pad1: vec2f, fill: vec4f };
@group(0) @binding(0) var<uniform> u: U;
struct VIn { @location(0) center: vec2f, @location(1) offset: vec2f };
@vertex fn vs_main(v: VIn) -> @builtin(position) vec4f {
    let a = u.rotation * 6.2831853;
    var o = v.offset * u.size;
    o = vec2f(o.x * cos(a) - o.y * sin(a), o.x * sin(a) + o.y * cos(a));
    o.x = o.x / 1.7778;   // correct to a 16:9 DISPLAY (the FBO is a wide internal res)
    return vec4f(v.center + o, 0.0, 1.0);
}
@fragment fn fs_main() -> @location(0) vec4f { return u.fill; }
)";
}  // namespace

struct ShapeGridOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "ShapeGrid";
    static constexpr const char* kDisplayName = "Shape Grid";
    static constexpr const char* kSummary = "A grid of filled polygons drawn as REAL vertex geometry (not a shader field).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "geometry", "grid"};
    vivid::Param<float> sides{"sides", 0.2f, 0.f, 1.f};
    vivid::Param<float> cols{"cols", 0.55f, 0.f, 1.f}, rows{"rows", 0.55f, 0.f, 1.f};
    vivid::Param<float> size{"size", 0.62f, 0.f, 1.f}, rotation{"rotation", 0.f, 0.f, 1.f};
    vivid::Param<float> r{"r", 0.95f, 0.f, 1.f}, g{"g", 0.95f, 0.f, 1.f}, b{"b", 0.9f, 0.f, 1.f};
    vivid::Param<float> bg_r{"bg_r", 0.05f, 0.f, 1.f}, bg_g{"bg_g", 0.05f, 0.f, 1.f}, bg_b{"bg_b", 0.08f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer vbo_ = nullptr; uint32_t vbo_cap_ = 0, vcount_ = 0;
    int n_ = -1, c_ = -1, rw_ = -1;
    ~ShapeGridOp() override {
        if (vbo_) wgpuBufferRelease(vbo_); if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR; bg_r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&sides); o.push_back(&cols); o.push_back(&rows); o.push_back(&size); o.push_back(&rotation);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&bg_r); o.push_back(&bg_g); o.push_back(&bg_b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    void rebuild_geometry(const VividGpuContext* ctx, int n, int C, int R) {
        std::vector<GVert> v; v.reserve(static_cast<size_t>(C) * R * n * 3);
        const float sx = 1.8f / C, sy = 1.8f / R;
        const float rad = 0.5f * std::min(sx, sy);
        for (int rr = 0; rr < R; ++rr) for (int cc = 0; cc < C; ++cc) {
            const float cx = -0.9f + (cc + 0.5f) * sx, cy = -0.9f + (rr + 0.5f) * sy;
            for (int i = 0; i < n; ++i) {
                const float a0 = 6.2831853f * i / n + 1.5707963f, a1 = 6.2831853f * (i + 1) / n + 1.5707963f;
                v.push_back({ cx, cy, 0.f, 0.f });
                v.push_back({ cx, cy, rad * std::cos(a0), rad * std::sin(a0) });
                v.push_back({ cx, cy, rad * std::cos(a1), rad * std::sin(a1) });
            }
        }
        vcount_ = static_cast<uint32_t>(v.size());
        const uint32_t bytes = vcount_ * sizeof(GVert);
        if (bytes > vbo_cap_) {
            if (vbo_) wgpuBufferRelease(vbo_);
            WGPUBufferDescriptor bd{}; bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = bytes;
            vbo_ = wgpuDeviceCreateBuffer(ctx->device, &bd); vbo_cap_ = bytes;
        }
        if (vbo_ && bytes) wgpuQueueWriteBuffer(ctx->queue, vbo_, 0, v.data(), bytes);
        n_ = n; c_ = C; rw_ = R;
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kShapeGridWGSL, "ShapeGrid", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 48, "ShapeGrid U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 48;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        WGPUVertexAttribute attrs[2]{};
        attrs[0].format = WGPUVertexFormat_Float32x2; attrs[0].offset = offsetof(GVert, cx); attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x2; attrs[1].offset = offsetof(GVert, ox); attrs[1].shaderLocation = 1;
        WGPUVertexBufferLayout vbl{}; vbl.arrayStride = sizeof(GVert); vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = 2; vbl.attributes = attrs;
        WGPUColorTargetState ct{}; ct.format = c->output_format; ct.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs{}; fs.module = sh_; fs.entryPoint = vivid_sv("fs_main"); fs.targetCount = 1; fs.targets = &ct;
        WGPURenderPipelineDescriptor rp{}; rp.layout = pl_;
        rp.vertex.module = sh_; rp.vertex.entryPoint = vivid_sv("vs_main");
        rp.vertex.bufferCount = 1; rp.vertex.buffers = &vbl;
        rp.primitive.topology = WGPUPrimitiveTopology_TriangleList; rp.primitive.frontFace = WGPUFrontFace_CCW;
        rp.primitive.cullMode = WGPUCullMode_None;
        rp.multisample.count = 1; rp.multisample.mask = 0xFFFFFFFF;
        rp.fragment = &fs;
        pipe_ = wgpuDeviceCreateRenderPipeline(c->device, &rp);
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 48;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        const int n = 3 + static_cast<int>(std::lround(pv(0, sides.value) * 5.f));
        const int C = 1 + static_cast<int>(std::lround(pv(1, cols.value) * 11.f));
        const int R = 1 + static_cast<int>(std::lround(pv(2, rows.value) * 11.f));
        if (n != n_ || C != c_ || R != rw_) rebuild_geometry(c, n, C, R);
        float u[12] = { float(c->output_width), float(c->output_height), float(c->time),
                        pv(3, size.value), pv(4, rotation.value), 0.f, 0.f, 0.f,
                        pv(5, r.value), pv(6, g.value), pv(7, b.value), 1.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        WGPURenderPassColorAttachment att{};
        att.view = c->output_texture_view; att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        att.loadOp = WGPULoadOp_Clear; att.storeOp = WGPUStoreOp_Store;
        att.clearValue = { pv(8, bg_r.value), pv(9, bg_g.value), pv(10, bg_b.value), 1.0 };
        WGPURenderPassDescriptor rpd{}; rpd.colorAttachmentCount = 1; rpd.colorAttachments = &att;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rpd);
        wgpuRenderPassEncoderSetPipeline(pass, pipe_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
        if (vbo_ && vcount_) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbo_, 0, vcount_ * sizeof(GVert));
            wgpuRenderPassEncoderDraw(pass, vcount_, 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
};

VIVID_REGISTER(ShapeGridOp)
