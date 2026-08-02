// Core visual package operator: Petals — a radiating flower of filled petals drawn as REAL
// vertex geometry (triangle-lens petals from a vertex buffer, not a fullscreen SDF field).
// A SOURCE op like ShapeGrid/Lines/Mesh: it renders its geometry and is composited over the
// rest. `size`/`glow` are uniforms (no VBO rebuild), so wiring them to a note makes each petal
// bloom + flash smoothly. `count`/`spread` change the geometry, so they rebuild the buffer.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

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
struct GVert { float x, y; };   // petal-space offset from the flower center (unit)
const char* kPetalsWGSL = R"(
struct U { res: vec2f, time: f32, size: f32, rotation: f32, glow: f32, cx: f32, cy: f32, fill: vec4f };
@group(0) @binding(0) var<uniform> u: U;
struct VIn { @location(0) pos: vec2f };
@vertex fn vs_main(v: VIn) -> @builtin(position) vec4f {
    let a = u.rotation * 6.2831853;
    var o = v.pos * (0.15 + u.size * 0.85);
    o = vec2f(o.x * cos(a) - o.y * sin(a), o.x * sin(a) + o.y * cos(a));
    o.x = o.x * (u.res.y / max(u.res.x, 1.0));   // aspect-correct so the flower stays round
    let center = vec2f(u.cx * 2.0 - 1.0, 1.0 - u.cy * 2.0);
    return vec4f(center + o, 0.0, 1.0);
}
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(u.fill.rgb * (0.4 + u.glow * 1.3), 1.0); }
)";
}  // namespace

struct PetalsOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Petals";
    static constexpr const char* kDisplayName = "Petals";
    static constexpr const char* kSummary = "A radiating flower of filled petals drawn as REAL vertex geometry; blooms per note.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "geometry", "petals"};
    vivid::Param<float> count{"count", 0.4f, 0.f, 1.f};
    vivid::Param<float> size{"size", 0.45f, 0.f, 1.f};
    vivid::Param<float> spread{"spread", 0.5f, 0.f, 1.f};
    vivid::Param<float> rotation{"rotation", 0.f, 0.f, 1.f};
    vivid::Param<float> glow{"glow", 0.5f, 0.f, 1.f};
    vivid::Param<float> cx{"cx", 0.5f, 0.f, 1.f}, cy{"cy", 0.5f, 0.f, 1.f};
    vivid::Param<float> r{"r", 1.0f, 0.f, 1.f}, g{"g", 0.5f, 0.f, 1.f}, b{"b", 0.7f, 0.f, 1.f};
    vivid::Param<float> bg_r{"bg_r", 0.f, 0.f, 1.f}, bg_g{"bg_g", 0.f, 0.f, 1.f}, bg_b{"bg_b", 0.f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer vbo_ = nullptr; uint32_t vbo_cap_ = 0, vcount_ = 0;
    int n_ = -1; float spread_ = -1.f;
    ~PetalsOp() override {
        if (vbo_) wgpuBufferRelease(vbo_); if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR; bg_r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&count); o.push_back(&size); o.push_back(&spread); o.push_back(&rotation); o.push_back(&glow);
        o.push_back(&cx); o.push_back(&cy);
        o.push_back(&r); o.push_back(&g); o.push_back(&b);
        o.push_back(&bg_r); o.push_back(&bg_g); o.push_back(&bg_b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    void rebuild_geometry(const VividGpuContext* ctx, int n, float spread) {
        std::vector<GVert> v; v.reserve(static_cast<size_t>(n) * 12 * 6);
        const int STEPS = 12;
        const float W = 0.06f + spread * 0.22f;   // petal half-width
        const float L = 0.85f;                     // petal length in unit space (scaled by `size` in vs)
        for (int i = 0; i < n; ++i) {
            const float th = 6.2831853f * i / n;
            const float dx = std::cos(th), dy = std::sin(th);
            const float px = -dy, py = dx;         // petal centerline perpendicular
            for (int s = 0; s < STEPS; ++s) {
                const float t0 = float(s) / STEPS, t1 = float(s + 1) / STEPS;
                const float w0 = W * std::sin(3.14159265f * t0);   // lens profile: 0 at base/tip, fat middle
                const float w1 = W * std::sin(3.14159265f * t1);
                const float c0x = dx * L * t0, c0y = dy * L * t0;
                const float c1x = dx * L * t1, c1y = dy * L * t1;
                const GVert l0{c0x - px * w0, c0y - py * w0}, r0{c0x + px * w0, c0y + py * w0};
                const GVert l1{c1x - px * w1, c1y - py * w1}, r1{c1x + px * w1, c1y + py * w1};
                v.push_back(l0); v.push_back(r0); v.push_back(l1);   // quad -> 2 triangles
                v.push_back(r0); v.push_back(r1); v.push_back(l1);
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
        n_ = n; spread_ = spread;
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kPetalsWGSL, "Petals", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 48, "Petals U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 48;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        WGPUVertexAttribute attr{}; attr.format = WGPUVertexFormat_Float32x2; attr.offset = 0; attr.shaderLocation = 0;
        WGPUVertexBufferLayout vbl{}; vbl.arrayStride = sizeof(GVert); vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = 1; vbl.attributes = &attr;
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
        const int n = 3 + static_cast<int>(std::lround(pv(0, count.value) * 13.f));   // 3..16 petals
        const float sp = pv(2, spread.value);
        if (n != n_ || std::fabs(sp - spread_) > 0.01f) rebuild_geometry(c, n, sp);
        float u[12] = { float(c->output_width), float(c->output_height), float(c->time),
                        pv(1, size.value), pv(3, rotation.value), pv(4, glow.value), pv(5, cx.value), pv(6, cy.value),
                        pv(7, r.value), pv(8, g.value), pv(9, b.value), 1.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        WGPURenderPassColorAttachment att{};
        att.view = c->output_texture_view; att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        att.loadOp = WGPULoadOp_Clear; att.storeOp = WGPUStoreOp_Store;
        att.clearValue = { pv(10, bg_r.value), pv(11, bg_g.value), pv(12, bg_b.value), 1.0 };
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

VIVID_REGISTER(PetalsOp)
