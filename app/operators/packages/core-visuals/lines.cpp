// Core visual package operator: Lines — migrated verbatim from the built-in LinesOp
// (builtin_ops.cpp); behaviour unchanged. Real vertex geometry via a raw-wgpu pipeline.
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
// The wireframe half of the geometry family. Static geometry in the vertex buffer; rotation +
// scale animate via the uniform. A generator (clears to bg). ---
struct LVert { float x, y; };
const char* kLinesWGSL = R"(
struct U { res: vec2f, time: f32, size: f32, rotation: f32, pad0: f32, pad1: vec2f, fill: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@vertex fn vs_main(@location(0) p: vec2f) -> @builtin(position) vec4f {
    let a = u.rotation * 6.2831853;
    var o = p * (0.4 + u.size * 0.7);
    o = vec2f(o.x * cos(a) - o.y * sin(a), o.x * sin(a) + o.y * cos(a));
    o.x = o.x / 1.7778;   // correct to a 16:9 DISPLAY (the FBO is a wide internal res, not the output aspect)
    return vec4f(o, 0.0, 1.0);
}
@fragment fn fs_main() -> @location(0) vec4f { return u.fill; }
)";
}  // namespace

struct LinesOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Lines";
    static constexpr const char* kDisplayName = "Lines";
    static constexpr const char* kSummary = "Real line geometry: a grid / radial burst / concentric rings (wireframe).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "lines", "wireframe"};
    vivid::Param<float> mode{"mode", 0.f, 0.f, 1.f};           // 0 grid, ~.4 radial, ~.8 rings
    vivid::Param<float> count{"count", 0.5f, 0.f, 1.f}, sides{"sides", 0.6f, 0.f, 1.f};
    vivid::Param<float> size{"size", 0.75f, 0.f, 1.f}, rotation{"rotation", 0.f, 0.f, 1.f};
    vivid::Param<float> r{"r", 0.3f, 0.f, 1.f}, g{"g", 1.f, 0.f, 1.f}, b{"b", 0.85f, 0.f, 1.f};
    vivid::Param<float> bg_r{"bg_r", 0.03f, 0.f, 1.f}, bg_g{"bg_g", 0.04f, 0.f, 1.f}, bg_b{"bg_b", 0.07f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer vbo_ = nullptr; uint32_t vbo_cap_ = 0, vcount_ = 0;
    int m_ = -99, cnt_ = -1, sd_ = -1;
    ~LinesOp() override {
        if (vbo_) wgpuBufferRelease(vbo_); if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR; bg_r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&mode); o.push_back(&count); o.push_back(&sides); o.push_back(&size); o.push_back(&rotation);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&bg_r); o.push_back(&bg_g); o.push_back(&bg_b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    void rebuild(const VividGpuContext* ctx, int mode_i, int cnt, int sd) {
        std::vector<LVert> v;
        const float ex = 1.0f;   // extent in the pre-scale space (the VS applies size)
        if (mode_i == 0) {                                   // grid: N vertical + N horizontal lines
            for (int i = 0; i < cnt; ++i) {
                const float t = (cnt <= 1) ? 0.f : -ex + 2.f * ex * i / (cnt - 1);
                v.push_back({ t, -ex }); v.push_back({ t, ex });     // vertical
                v.push_back({ -ex, t }); v.push_back({ ex, t });     // horizontal
            }
        } else if (mode_i == 1) {                            // radial burst from center
            for (int i = 0; i < cnt; ++i) {
                const float a = 6.2831853f * i / cnt;
                v.push_back({ 0.f, 0.f }); v.push_back({ ex * std::cos(a), ex * std::sin(a) });
            }
        } else {                                             // concentric n-gon rings
            for (int ring = 1; ring <= cnt; ++ring) {
                const float rr = ex * ring / cnt;
                for (int i = 0; i < sd; ++i) {
                    const float a0 = 6.2831853f * i / sd + 1.5707963f, a1 = 6.2831853f * (i + 1) / sd + 1.5707963f;
                    v.push_back({ rr * std::cos(a0), rr * std::sin(a0) });
                    v.push_back({ rr * std::cos(a1), rr * std::sin(a1) });
                }
            }
        }
        vcount_ = static_cast<uint32_t>(v.size());
        const uint32_t bytes = vcount_ * sizeof(LVert);
        if (bytes > vbo_cap_) {
            if (vbo_) wgpuBufferRelease(vbo_);
            WGPUBufferDescriptor bd{}; bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = bytes ? bytes : 16;
            vbo_ = wgpuDeviceCreateBuffer(ctx->device, &bd); vbo_cap_ = bd.size;
        }
        if (vbo_ && bytes) wgpuQueueWriteBuffer(ctx->queue, vbo_, 0, v.data(), bytes);
        m_ = mode_i; cnt_ = cnt; sd_ = sd;
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kLinesWGSL, "Lines", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 48, "Lines U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 48;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        WGPUVertexAttribute attr{}; attr.format = WGPUVertexFormat_Float32x2; attr.offset = 0; attr.shaderLocation = 0;
        WGPUVertexBufferLayout vbl{}; vbl.arrayStride = sizeof(LVert); vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = 1; vbl.attributes = &attr;
        WGPUColorTargetState ct{}; ct.format = c->output_format; ct.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs{}; fs.module = sh_; fs.entryPoint = vivid_sv("fs_main"); fs.targetCount = 1; fs.targets = &ct;
        WGPURenderPipelineDescriptor rp{}; rp.layout = pl_;
        rp.vertex.module = sh_; rp.vertex.entryPoint = vivid_sv("vs_main");
        rp.vertex.bufferCount = 1; rp.vertex.buffers = &vbl;
        rp.primitive.topology = WGPUPrimitiveTopology_LineList; rp.primitive.frontFace = WGPUFrontFace_CCW;
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
        const int mode_i = static_cast<int>(std::lround(pv(0, mode.value) * 2.f));      // 0/1/2
        const int cnt = 2 + static_cast<int>(std::lround(pv(1, count.value) * 22.f));   // 2..24
        const int sd = 3 + static_cast<int>(std::lround(pv(2, sides.value) * 9.f));     // 3..12
        if (mode_i != m_ || cnt != cnt_ || sd != sd_) rebuild(c, mode_i, cnt, sd);
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
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbo_, 0, vcount_ * sizeof(LVert));
            wgpuRenderPassEncoderDraw(pass, vcount_, 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
};

VIVID_REGISTER(LinesOp)
