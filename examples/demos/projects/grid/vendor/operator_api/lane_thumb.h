#pragma once
// 2D lane-preview thumbnails for the DATA ops (AudioSpectrum / LaneRamp / LanePalette). Those ops emit
// value lanes, not a texture, so their node card would be blank. This renders a little bar chart or
// colour gradient of the lane INTO the op's output texture (which the card blits) — same trick as
// thumbnail_3d.h but flat 2D (no depth). Pipeline cached per (device, colour-format); the per-op State
// owns a growable vertex buffer.

#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"   // create_vertex_buffer / release / kColorAttachmentClear helpers
#include <algorithm>
#include <cstdint>
#include <vector>

namespace vivid::lanethumb {

struct Vtx { float pos[2]; float col[3]; };   // NDC position + RGB
struct State { WGPUBuffer vb = nullptr; uint32_t cap = 0; };

struct Pipe {
    WGPUDevice dev = nullptr; WGPUTextureFormat fmt = WGPUTextureFormat_Undefined;
    WGPUShaderModule shader = nullptr; WGPUPipelineLayout pl = nullptr; WGPURenderPipeline pipe = nullptr;
};

inline Pipe& pipe_for(WGPUDevice dev, WGPUTextureFormat fmt) {
    static std::vector<Pipe> cache;
    for (Pipe& p : cache) if (p.dev == dev && p.fmt == fmt) return p;
    static const char* kWGSL = R"(
struct VOut { @builtin(position) pos: vec4f, @location(0) col: vec3f };
@vertex fn vs_main(@location(0) p: vec2f, @location(1) c: vec3f) -> VOut {
    var o: VOut; o.pos = vec4f(p, 0.0, 1.0); o.col = c; return o;
}
@fragment fn fs_main(in: VOut) -> @location(0) vec4f { return vec4f(in.col, 1.0); }
)";
    Pipe p{}; p.dev = dev; p.fmt = fmt;
    WGPUShaderSourceWGSL src{}; src.chain.sType = WGPUSType_ShaderSourceWGSL; src.code = vivid_sv(kWGSL);
    WGPUShaderModuleDescriptor sd{}; sd.nextInChain = &src.chain; sd.label = vivid_sv("lanethumb shader");
    p.shader = wgpuDeviceCreateShaderModule(dev, &sd);
    WGPUPipelineLayoutDescriptor pld{}; pld.label = vivid_sv("lanethumb pl");
    p.pl = wgpuDeviceCreatePipelineLayout(dev, &pld);
    WGPUVertexAttribute attrs[2]{};
    attrs[0].format = WGPUVertexFormat_Float32x2; attrs[0].offset = 0;               attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x3; attrs[1].offset = 2 * sizeof(float); attrs[1].shaderLocation = 1;
    WGPUVertexBufferLayout vbl{}; vbl.arrayStride = sizeof(Vtx); vbl.stepMode = WGPUVertexStepMode_Vertex;
    vbl.attributeCount = 2; vbl.attributes = attrs;
    WGPURenderPipelineDescriptor rp{}; rp.label = vivid_sv("lanethumb pipeline"); rp.layout = p.pl;
    rp.vertex.module = p.shader; rp.vertex.entryPoint = vivid_sv("vs_main");
    rp.vertex.bufferCount = 1; rp.vertex.buffers = &vbl;
    rp.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    WGPUColorTargetState ct{}; ct.format = fmt; ct.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs{}; fs.module = p.shader; fs.entryPoint = vivid_sv("fs_main"); fs.targetCount = 1; fs.targets = &ct;
    rp.fragment = &fs;
    rp.multisample.count = 1; rp.multisample.mask = 0xFFFFFFFFu;
    p.pipe = wgpuDeviceCreateRenderPipeline(dev, &rp);
    cache.push_back(p); return cache.back();
}

inline bool ok(const VividGpuContext* ctx) {
    return ctx && ctx->device && ctx->queue && ctx->command_encoder && ctx->output_texture_view;
}

// Upload `verts` and draw them into the op's output texture, clearing to a dark ground first.
inline void draw(const VividGpuContext* ctx, State& st, const std::vector<Vtx>& verts) {
    if (!ok(ctx) || verts.empty()) return;
    Pipe& p = pipe_for(ctx->device, ctx->output_format);
    if (!p.pipe) return;
    const uint32_t need = static_cast<uint32_t>(verts.size());
    if (st.cap < need) {
        if (st.vb) vivid::gpu::release(st.vb);
        st.cap = need + need / 2 + 64;
        st.vb = vivid::gpu::create_vertex_buffer(ctx->device, ctx->queue, nullptr,
                    static_cast<uint64_t>(st.cap) * sizeof(Vtx), "lanethumb vb");
    }
    if (!st.vb) return;
    wgpuQueueWriteBuffer(ctx->queue, st.vb, 0, verts.data(), verts.size() * sizeof(Vtx));
    WGPURenderPassColorAttachment ca{}; ca.view = ctx->output_texture_view;
    ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = WGPUColor{0.06, 0.07, 0.09, 1.0}; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    WGPURenderPassDescriptor rpd{}; rpd.label = vivid_sv("lanethumb pass"); rpd.colorAttachmentCount = 1; rpd.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(ctx->command_encoder, &rpd);
    wgpuRenderPassEncoderSetPipeline(pass, p.pipe);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, st.vb, 0, verts.size() * sizeof(Vtx));
    wgpuRenderPassEncoderDraw(pass, need, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

inline void quad(std::vector<Vtx>& v, float x0, float y0, float x1, float y1, const float c[3]) {
    const Vtx a{{x0, y0}, {c[0], c[1], c[2]}}, b{{x1, y0}, {c[0], c[1], c[2]}},
              d{{x1, y1}, {c[0], c[1], c[2]}}, e{{x0, y1}, {c[0], c[1], c[2]}};
    v.push_back(a); v.push_back(b); v.push_back(d); v.push_back(a); v.push_back(d); v.push_back(e);
}

// A bar chart of `n` values (0..1) growing up from the bottom, in colour `col`.
inline void render_bars(const VividGpuContext* ctx, State& st, const float* vals, uint32_t n, const float col[3]) {
    if (!ok(ctx) || !vals || n == 0) return;
    std::vector<Vtx> v; v.reserve(n * 6);
    const float pad = 0.04f, span = 2.f - 2.f * pad, gap = span / n * 0.25f;
    const float bw = span / n - gap;
    for (uint32_t i = 0; i < n; ++i) {
        const float x0 = -1.f + pad + i * (span / n);
        const float h = std::clamp(vals[i], 0.f, 1.f) * 1.7f;   // up to near the top
        quad(v, x0, -0.9f, x0 + bw, -0.9f + h, col);
    }
    draw(ctx, st, v);
}

// A horizontal gradient of `n` colours (per-band r/g/b), full height — for LanePalette.
inline void render_gradient(const VividGpuContext* ctx, State& st,
                            const float* r, const float* g, const float* b, uint32_t n) {
    if (!ok(ctx) || n == 0) return;
    std::vector<Vtx> v; v.reserve(n * 6);
    const float w = 2.f / n;
    for (uint32_t i = 0; i < n; ++i) {
        const float col[3] = { r ? r[i] : 1.f, g ? g[i] : 1.f, b ? b[i] : 1.f };
        quad(v, -1.f + i * w, -0.85f, -1.f + (i + 1) * w, 0.85f, col);
    }
    draw(ctx, st, v);
}

inline void destroy(State& st) { if (st.vb) vivid::gpu::release(st.vb); st = State{}; }

}  // namespace vivid::lanethumb
