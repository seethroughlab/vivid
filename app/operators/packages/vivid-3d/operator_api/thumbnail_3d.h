#pragma once
// ADR-0041: animated 3D thumbnails for the vivid-3d scene-graph ops.
//
// The trunk's node card blits each node's OUTPUT texture as its thumbnail, but the scene-graph content
// ops emit a VividSceneFragment (custom-ref), not a texture — so their output texture is empty and the
// card is blank. This helper lets such an op render a small, slowly-rotating 3D preview of its own
// geometry INTO its output texture (which it already owns via VividGpuContext), so the existing blit
// shows an animated 3D thumbnail. No thumbnail-ABI change: it reuses the op's live GPU context.
//
// Usage (in an op's process_gpu, AFTER it has built its vertex/index buffers + set its fragment):
//     vivid::thumb3d::render(ctx, thumb_, vertex_buffer_, vb_size, index_buffer_, index_count_,
//                            sizeof(vivid::gpu::Vertex3D), bmin, bmax, color3);
// where `thumb_` is a `vivid::thumb3d::State` member; call `vivid::thumb3d::destroy(thumb_)` in the dtor.
//
// The pipeline/shader are cached per (device, colour-format) — created once, not per frame. The per-op
// State owns its uniform buffer + depth texture (so concurrent ops don't clobber a shared uniform).

#include "operator_api/gpu_3d.h"
#include "linmath.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdio>

namespace vivid::thumb3d {

// Per-op thumbnail state: a uniform buffer + a depth texture sized to the op's output texture.
struct State {
    WGPUBuffer      ubo = nullptr;
    WGPUTexture     depth = nullptr;
    WGPUTextureView depth_view = nullptr;
    uint32_t        dw = 0, dh = 0;
};

struct Uniforms {   // std140-friendly: mat4 + three vec4
    float mvp[16];
    float center[4];
    float extent[4];
    float color[4];
};

// Cached, format-keyed pipeline (shader + layouts). Constant across frames and ops.
struct Pipe {
    WGPUDevice           dev = nullptr;
    WGPUTextureFormat    fmt = WGPUTextureFormat_Undefined;
    WGPUShaderModule     shader = nullptr;
    WGPUBindGroupLayout  bgl = nullptr;
    WGPUPipelineLayout   pl = nullptr;
    WGPURenderPipeline   pipe = nullptr;
};

inline Pipe& pipe_for(WGPUDevice dev, WGPUTextureFormat fmt) {
    static std::vector<Pipe> cache;
    for (Pipe& p : cache) if (p.dev == dev && p.fmt == fmt) return p;

    static const char* kWGSL = R"(
struct U { mvp: mat4x4f, center: vec4f, extent: vec4f, color: vec4f };
@group(0) @binding(0) var<uniform> u: U;
struct VOut { @builtin(position) pos: vec4f, @location(0) world: vec3f };
@vertex fn vs_main(@location(0) position: vec3f) -> VOut {
    var o: VOut;
    o.world = position;
    o.pos = u.mvp * vec4f(position, 1.0);
    return o;
}
@fragment fn fs_main(in: VOut) -> @location(0) vec4f {
    let safe = max(u.extent.xyz, vec3f(0.001));
    let n = normalize((in.world - u.center.xyz) / safe);
    let l = normalize(vec3f(0.45, 0.72, 0.55));
    let shade = 0.26 + 0.74 * max(dot(n, l), 0.0);
    return vec4f(u.color.rgb * shade, u.color.a);
}
)";
    Pipe p{}; p.dev = dev; p.fmt = fmt;
    // shader module (vs+fs in one WGSL — the trunk's create_shader prepends a 2D fullscreen VS, so build directly)
    WGPUShaderSourceWGSL src{}; src.chain.sType = WGPUSType_ShaderSourceWGSL; src.code = vivid_sv(kWGSL);
    WGPUShaderModuleDescriptor sd{}; sd.nextInChain = &src.chain; sd.label = vivid_sv("thumb3d shader");
    p.shader = wgpuDeviceCreateShaderModule(dev, &sd);
    // bind group layout: binding 0 = uniform (VS+FS)
    WGPUBindGroupLayoutEntry be{}; be.binding = 0;
    be.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    be.buffer.type = WGPUBufferBindingType_Uniform; be.buffer.minBindingSize = sizeof(Uniforms);
    WGPUBindGroupLayoutDescriptor bgld{}; bgld.entryCount = 1; bgld.entries = &be; bgld.label = vivid_sv("thumb3d bgl");
    p.bgl = wgpuDeviceCreateBindGroupLayout(dev, &bgld);
    WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &p.bgl; pld.label = vivid_sv("thumb3d pl");
    p.pl = wgpuDeviceCreatePipelineLayout(dev, &pld);
    // vertex layout: position at offset 0 (Vertex3D-compatible)
    static WGPUVertexAttribute attr{}; attr.format = WGPUVertexFormat_Float32x3; attr.offset = 0; attr.shaderLocation = 0;
    WGPUVertexBufferLayout vbl{}; vbl.arrayStride = sizeof(vivid::gpu::Vertex3D); vbl.stepMode = WGPUVertexStepMode_Vertex;
    vbl.attributeCount = 1; vbl.attributes = &attr;
    vivid::gpu::Pipeline3DDesc pd{}; pd.shader = p.shader; pd.layout = p.pl; pd.color_format = fmt;
    pd.vertex_layouts = &vbl; pd.vertex_layout_count = 1; pd.cull_mode = WGPUCullMode_None; pd.label = "thumb3d pipeline";
    p.pipe = vivid::gpu::create_3d_pipeline(dev, pd);
    cache.push_back(p);
    return cache.back();
}

// Render a rotating 3D preview of the given mesh into `target` (the op's output texture view).
inline void render(const VividGpuContext* ctx, State& st,
                   WGPUBuffer vb, uint64_t vb_size, WGPUBuffer ib, uint32_t index_count,
                   const float bmin[3], const float bmax[3], const float color3[3] = nullptr) {
    if (!ctx || !ctx->device || !ctx->queue || !ctx->command_encoder || !ctx->output_texture_view
        || !vb || !ib || vb_size == 0 || index_count == 0) return;
    const uint32_t w = ctx->output_width, h = ctx->output_height;
    if (w == 0 || h == 0) return;
    Pipe& p = pipe_for(ctx->device, ctx->output_format);
    if (!p.pipe) return;

    if (!st.ubo)
        st.ubo = vivid::gpu::create_uniform_buffer(ctx->device, sizeof(Uniforms), "thumb3d ubo");
    if (st.dw != w || st.dh != h) {
        if (st.depth_view) vivid::gpu::release(st.depth_view);
        if (st.depth)      vivid::gpu::release(st.depth);
        st.depth = vivid::gpu::create_depth_texture(ctx->device, w, h, "thumb3d depth");
        st.depth_view = vivid::gpu::create_depth_view(st.depth, "thumb3d depth view");
        st.dw = w; st.dh = h;
    }

    // Fit + slowly orbit the camera around the mesh AABB (animated off ctx->time).
    const float cx = (bmin[0]+bmax[0])*0.5f, cy = (bmin[1]+bmax[1])*0.5f, cz = (bmin[2]+bmax[2])*0.5f;
    float rad = 0.5f * std::sqrt((bmax[0]-bmin[0])*(bmax[0]-bmin[0]) + (bmax[1]-bmin[1])*(bmax[1]-bmin[1])
                                 + (bmax[2]-bmin[2])*(bmax[2]-bmin[2]));
    if (rad < 1e-4f) rad = 1.0f;
    const float dist = rad * 2.6f;
    const float ang = static_cast<float>(ctx->time) * 0.6f;
    vec3 eye = { cx + dist * 0.72f * std::cos(ang), cy + dist * 0.42f, cz + dist * 0.72f * std::sin(ang) };
    vec3 tgt = { cx, cy, cz }, up = { 0.f, 1.f, 0.f };
    mat4x4 view, proj, mvp;
    mat4x4_look_at(view, eye, tgt, up);
    mat4x4_perspective(proj, 0.6f, static_cast<float>(w)/static_cast<float>(h), dist*0.01f, dist*4.0f);
    mat4x4_mul(mvp, proj, view);

    Uniforms u{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) u.mvp[r*4+c] = mvp[r][c];
    u.center[0]=cx; u.center[1]=cy; u.center[2]=cz; u.center[3]=1.f;
    u.extent[0]=std::max(0.001f,(bmax[0]-bmin[0])*0.5f); u.extent[1]=std::max(0.001f,(bmax[1]-bmin[1])*0.5f);
    u.extent[2]=std::max(0.001f,(bmax[2]-bmin[2])*0.5f); u.extent[3]=1.f;
    u.color[0]=color3?color3[0]:0.66f; u.color[1]=color3?color3[1]:0.72f; u.color[2]=color3?color3[2]:0.80f; u.color[3]=1.f;
    wgpuQueueWriteBuffer(ctx->queue, st.ubo, 0, &u, sizeof(u));

    WGPUBindGroupEntry bge{}; bge.binding = 0; bge.buffer = st.ubo; bge.size = sizeof(Uniforms);
    WGPUBindGroupDescriptor bgd{}; bgd.layout = p.bgl; bgd.entryCount = 1; bgd.entries = &bge; bgd.label = vivid_sv("thumb3d bg");
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(ctx->device, &bgd);

    WGPURenderPassEncoder pass = vivid::gpu::begin_3d_pass(
        ctx->command_encoder, ctx->output_texture_view, st.depth_view, "thumb3d pass",
        WGPUColor{0.07, 0.078, 0.09, 1.0});
    wgpuRenderPassEncoderSetPipeline(pass, p.pipe);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, vb_size);
    wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, static_cast<uint64_t>(index_count)*sizeof(uint32_t));
    wgpuRenderPassEncoderDrawIndexed(pass, index_count, 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bg);
}

inline void destroy(State& st) {
    if (st.depth_view) vivid::gpu::release(st.depth_view);
    if (st.depth)      vivid::gpu::release(st.depth);
    if (st.ubo)        vivid::gpu::release(st.ubo);
    st = State{};
}

}  // namespace vivid::thumb3d
