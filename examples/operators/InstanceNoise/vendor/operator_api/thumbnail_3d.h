#pragma once
// ADR-0041: animated 3D thumbnails for the vivid-3d scene-graph ops.
//
// The trunk's node card blits each node's OUTPUT texture as its thumbnail, but the scene-graph content
// ops emit a VividSceneFragment (custom-ref), not a texture — so their output texture is empty and the
// card is blank. This helper lets such an op render a small, slowly-rotating 3D preview of its own
// geometry INTO its output texture (which it already owns via VividGpuContext), so the existing blit
// shows an animated 3D thumbnail. No thumbnail-ABI change: it reuses the op's live GPU context.
//
// Three entry points cover every op shape:
//   render()            — a single indexed mesh (Shape3D, Deformer).
//   render_instanced()  — one base mesh drawn per CPU InstanceData3D (the instancer ops, Particles proxy).
//   render_proxy_sphere() / render_particles_proxy() / render_merge_proxy() — generated stand-in geometry
//                          for ops with no CPU mesh (SDF3D, Light3D, Particles3D, SceneMerge).
//
// `thumb_` is a per-op `vivid::thumb3d::State` member; call `vivid::thumb3d::destroy(thumb_)` in the dtor.
// Pipelines/shaders are cached per (device, colour-format); the per-op State owns its uniform + depth +
// (optional) instance buffer so concurrent ops never clobber a shared resource.

#include "operator_api/gpu_3d.h"
#include "linmath.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdint>

namespace vivid::thumb3d {

// Per-op thumbnail state: a uniform buffer, a depth texture sized to the output, and a growable
// instance buffer (only allocated for the instanced path).
struct State {
    WGPUBuffer      ubo = nullptr;
    WGPUTexture     depth = nullptr;
    WGPUTextureView depth_view = nullptr;
    uint32_t        dw = 0, dh = 0;
    WGPUBuffer      inst_buf = nullptr;   // per-instance data (Vertex usage, stepMode Instance)
    uint32_t        inst_cap = 0;         // capacity in InstanceData3D units
};

struct Uniforms {   // std140-friendly: mat4 + three vec4
    float mvp[16];
    float center[4];
    float extent[4];
    float color[4];
};

// Cached, format-keyed pipeline (shader + layouts). `instanced` selects the per-instance variant.
struct Pipe {
    WGPUDevice           dev = nullptr;
    WGPUTextureFormat    fmt = WGPUTextureFormat_Undefined;
    bool                 instanced = false;
    WGPUShaderModule     shader = nullptr;
    WGPUBindGroupLayout  bgl = nullptr;
    WGPUPipelineLayout   pl = nullptr;
    WGPURenderPipeline   pipe = nullptr;
};

inline Pipe& pipe_for(WGPUDevice dev, WGPUTextureFormat fmt, bool instanced) {
    static std::vector<Pipe> cache;
    for (Pipe& p : cache) if (p.dev == dev && p.fmt == fmt && p.instanced == instanced) return p;

    // Single-mesh shader: pseudo-normal lighting from the AABB centre.
    static const char* kMESH = R"(
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
    // Instanced shader: model matrix composed per-instance from InstanceData3D; lit by the base-mesh
    // local normal so each instance reads as a rounded solid. u.mvp is view*proj (no model).
    static const char* kINST = R"(
struct U { mvp: mat4x4f, center: vec4f, extent: vec4f, color: vec4f };
@group(0) @binding(0) var<uniform> u: U;
struct VOut { @builtin(position) pos: vec4f, @location(0) local: vec3f, @location(1) col: vec4f };
@vertex fn vs_main(@location(0) position: vec3f,
                   @location(1) ipos: vec3f, @location(2) iscale: vec3f,
                   @location(3) icol: vec4f, @location(4) iroty: f32) -> VOut {
    let c = cos(iroty); let s = sin(iroty);
    let sp = vec3f(position.x * iscale.x, position.y * iscale.y, position.z * iscale.z);
    let rp = vec3f(sp.x * c - sp.z * s, sp.y, sp.x * s + sp.z * c);
    let world = rp + ipos;
    var o: VOut;
    o.local = position;
    o.col = icol;
    o.pos = u.mvp * vec4f(world, 1.0);
    return o;
}
@fragment fn fs_main(in: VOut) -> @location(0) vec4f {
    let base = select(u.color.rgb, in.col.rgb, in.col.a > 0.01);
    let n = normalize(in.local + vec3f(0.0001, 0.0, 0.0));
    let l = normalize(vec3f(0.45, 0.72, 0.55));
    let shade = 0.30 + 0.70 * max(dot(n, l), 0.0);
    return vec4f(base * shade, 1.0);
}
)";
    Pipe p{}; p.dev = dev; p.fmt = fmt; p.instanced = instanced;
    WGPUShaderSourceWGSL src{}; src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code = vivid_sv(instanced ? kINST : kMESH);
    WGPUShaderModuleDescriptor sd{}; sd.nextInChain = &src.chain; sd.label = vivid_sv("thumb3d shader");
    p.shader = wgpuDeviceCreateShaderModule(dev, &sd);
    WGPUBindGroupLayoutEntry be{}; be.binding = 0;
    be.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    be.buffer.type = WGPUBufferBindingType_Uniform; be.buffer.minBindingSize = sizeof(Uniforms);
    WGPUBindGroupLayoutDescriptor bgld{}; bgld.entryCount = 1; bgld.entries = &be; bgld.label = vivid_sv("thumb3d bgl");
    p.bgl = wgpuDeviceCreateBindGroupLayout(dev, &bgld);
    WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &p.bgl; pld.label = vivid_sv("thumb3d pl");
    p.pl = wgpuDeviceCreatePipelineLayout(dev, &pld);

    // Layout 0: the base mesh — position at offset 0 (Vertex3D-compatible stride).
    WGPUVertexAttribute mesh_attr{}; mesh_attr.format = WGPUVertexFormat_Float32x3; mesh_attr.offset = 0; mesh_attr.shaderLocation = 0;
    WGPUVertexBufferLayout vbl[2]{};
    vbl[0].arrayStride = sizeof(vivid::gpu::Vertex3D); vbl[0].stepMode = WGPUVertexStepMode_Vertex;
    vbl[0].attributeCount = 1; vbl[0].attributes = &mesh_attr;
    // Layout 1: per-instance data (InstanceData3D, 48 bytes) — only for the instanced pipeline.
    WGPUVertexAttribute iattr[4]{};
    iattr[0].format = WGPUVertexFormat_Float32x3; iattr[0].offset = 0;  iattr[0].shaderLocation = 1; // position
    iattr[1].format = WGPUVertexFormat_Float32x3; iattr[1].offset = 16; iattr[1].shaderLocation = 2; // scale
    iattr[2].format = WGPUVertexFormat_Float32x4; iattr[2].offset = 32; iattr[2].shaderLocation = 3; // color
    iattr[3].format = WGPUVertexFormat_Float32;   iattr[3].offset = 12; iattr[3].shaderLocation = 4; // rotation_y
    vbl[1].arrayStride = sizeof(vivid::gpu::InstanceData3D); vbl[1].stepMode = WGPUVertexStepMode_Instance;
    vbl[1].attributeCount = 4; vbl[1].attributes = iattr;

    vivid::gpu::Pipeline3DDesc pd{}; pd.shader = p.shader; pd.layout = p.pl; pd.color_format = fmt;
    pd.vertex_layouts = vbl; pd.vertex_layout_count = instanced ? 2u : 1u;
    pd.cull_mode = WGPUCullMode_None; pd.label = "thumb3d pipeline";
    p.pipe = vivid::gpu::create_3d_pipeline(dev, pd);
    cache.push_back(p);
    return cache.back();
}

// ---------------------------------------------------------------------------
// Cached primitive meshes (unit cube / unit sphere), keyed by device, for proxy previews.
// ---------------------------------------------------------------------------
struct Prim { WGPUDevice dev = nullptr; WGPUBuffer vb = nullptr; uint64_t vb_size = 0;
              WGPUBuffer ib = nullptr; uint32_t index_count = 0; float radius = 1.f; };

inline const Prim& prim_cube(WGPUDevice dev, WGPUQueue queue) {
    static std::vector<Prim> cache;
    for (Prim& p : cache) if (p.dev == dev) return p;
    std::vector<vivid::gpu::Vertex3D> v(8);
    const float c[8][3] = {{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},
                           {-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}};
    for (int i = 0; i < 8; ++i) { v[i] = {}; v[i].position[0]=c[i][0]; v[i].position[1]=c[i][1]; v[i].position[2]=c[i][2]; }
    std::vector<uint32_t> idx = {0,1,2, 0,2,3,  5,4,7, 5,7,6,  4,0,3, 4,3,7,
                                 1,5,6, 1,6,2,  3,2,6, 3,6,7,  4,5,1, 4,1,0};
    Prim p{}; p.dev = dev; p.vb_size = v.size()*sizeof(vivid::gpu::Vertex3D);
    p.vb = vivid::gpu::create_vertex_buffer(dev, queue, v.data(), p.vb_size, "thumb3d cube vb");
    p.ib = vivid::gpu::create_index_buffer(dev, queue, idx.data(), idx.size(), "thumb3d cube ib");
    p.index_count = static_cast<uint32_t>(idx.size()); p.radius = 0.87f;
    cache.push_back(p); return cache.back();
}

inline const Prim& prim_sphere(WGPUDevice dev, WGPUQueue queue) {
    static std::vector<Prim> cache;
    for (Prim& p : cache) if (p.dev == dev) return p;
    const int RINGS = 12, SECT = 16; const float R = 0.5f;
    std::vector<vivid::gpu::Vertex3D> v; std::vector<uint32_t> idx;
    for (int r = 0; r <= RINGS; ++r) {
        float phi = 3.14159265f * static_cast<float>(r) / RINGS;
        for (int s = 0; s <= SECT; ++s) {
            float th = 2.f * 3.14159265f * static_cast<float>(s) / SECT;
            vivid::gpu::Vertex3D vert{};
            vert.position[0] = R * std::sin(phi) * std::cos(th);
            vert.position[1] = R * std::cos(phi);
            vert.position[2] = R * std::sin(phi) * std::sin(th);
            v.push_back(vert);
        }
    }
    for (int r = 0; r < RINGS; ++r) for (int s = 0; s < SECT; ++s) {
        uint32_t a = r*(SECT+1)+s, b = a+SECT+1;
        idx.insert(idx.end(), {a, b, a+1,  a+1, b, b+1});
    }
    Prim p{}; p.dev = dev; p.vb_size = v.size()*sizeof(vivid::gpu::Vertex3D);
    p.vb = vivid::gpu::create_vertex_buffer(dev, queue, v.data(), p.vb_size, "thumb3d sphere vb");
    p.ib = vivid::gpu::create_index_buffer(dev, queue, idx.data(), idx.size(), "thumb3d sphere ib");
    p.index_count = static_cast<uint32_t>(idx.size()); p.radius = R;
    cache.push_back(p); return cache.back();
}

// Compute an AABB from a CPU Vertex3D span (position at offset 0). Returns false if empty.
inline bool aabb_from_verts(const vivid::gpu::Vertex3D* verts, uint32_t n, float bmin[3], float bmax[3]) {
    if (!verts || n == 0) return false;
    for (int k = 0; k < 3; ++k) { bmin[k] = verts[0].position[k]; bmax[k] = verts[0].position[k]; }
    for (uint32_t i = 1; i < n; ++i) for (int k = 0; k < 3; ++k) {
        bmin[k] = std::min(bmin[k], verts[i].position[k]);
        bmax[k] = std::max(bmax[k], verts[i].position[k]);
    }
    return true;
}

// Shared camera + uniform setup, then a begin_3d_pass clear. Returns the open pass + a fresh bind group
// (caller draws, ends, then releases the pass and bind group).
inline void write_camera(const VividGpuContext* ctx, State& st, Pipe& p,
                         const float bmin[3], const float bmax[3], const float color3[3],
                         WGPURenderPassEncoder* out_pass, WGPUBindGroup* out_bg) {
    const uint32_t w = ctx->output_width, h = ctx->output_height;
    if (!st.ubo) st.ubo = vivid::gpu::create_uniform_buffer(ctx->device, sizeof(Uniforms), "thumb3d ubo");
    if (st.dw != w || st.dh != h) {
        if (st.depth_view) vivid::gpu::release(st.depth_view);
        if (st.depth)      vivid::gpu::release(st.depth);
        st.depth = vivid::gpu::create_depth_texture(ctx->device, w, h, "thumb3d depth");
        st.depth_view = vivid::gpu::create_depth_view(st.depth, "thumb3d depth view");
        st.dw = w; st.dh = h;
    }
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
    *out_bg = wgpuDeviceCreateBindGroup(ctx->device, &bgd);
    *out_pass = vivid::gpu::begin_3d_pass(ctx->command_encoder, ctx->output_texture_view, st.depth_view,
                                          "thumb3d pass", WGPUColor{0.07, 0.078, 0.09, 1.0});
    wgpuRenderPassEncoderSetPipeline(*out_pass, p.pipe);
    wgpuRenderPassEncoderSetBindGroup(*out_pass, 0, *out_bg, 0, nullptr);
}

inline bool ctx_ok(const VividGpuContext* ctx) {
    return ctx && ctx->device && ctx->queue && ctx->command_encoder && ctx->output_texture_view
        && ctx->output_width && ctx->output_height;
}

// Render a rotating 3D preview of a single indexed mesh into the op's output texture.
inline void render(const VividGpuContext* ctx, State& st,
                   WGPUBuffer vb, uint64_t vb_size, WGPUBuffer ib, uint32_t index_count,
                   const float bmin[3], const float bmax[3], const float color3[3] = nullptr) {
    if (!ctx_ok(ctx) || !vb || !ib || vb_size == 0 || index_count == 0) return;
    Pipe& p = pipe_for(ctx->device, ctx->output_format, false);
    if (!p.pipe) return;
    WGPURenderPassEncoder pass; WGPUBindGroup bg;
    write_camera(ctx, st, p, bmin, bmax, color3, &pass, &bg);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, vb_size);
    wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, static_cast<uint64_t>(index_count)*sizeof(uint32_t));
    wgpuRenderPassEncoderDrawIndexed(pass, index_count, 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bg);
}

// Render a base mesh once per CPU InstanceData3D. Frames the camera to the instance cloud.
inline void render_instanced(const VividGpuContext* ctx, State& st,
                             WGPUBuffer vb, uint64_t vb_size, WGPUBuffer ib, uint32_t index_count, float mesh_radius,
                             const vivid::gpu::InstanceData3D* insts, uint32_t count, const float color3[3] = nullptr) {
    if (!ctx_ok(ctx) || !vb || !ib || vb_size == 0 || index_count == 0 || !insts || count == 0) return;
    Pipe& p = pipe_for(ctx->device, ctx->output_format, true);
    if (!p.pipe) return;

    // Upload the CPU instances into a growable, op-owned instance buffer.
    if (st.inst_cap < count) {
        if (st.inst_buf) vivid::gpu::release(st.inst_buf);
        st.inst_cap = count + count / 2 + 16;
        st.inst_buf = vivid::gpu::create_vertex_buffer(ctx->device, ctx->queue, nullptr,
                          static_cast<uint64_t>(st.inst_cap)*sizeof(vivid::gpu::InstanceData3D), "thumb3d inst");
    }
    if (!st.inst_buf) return;
    wgpuQueueWriteBuffer(ctx->queue, st.inst_buf, 0, insts, static_cast<uint64_t>(count)*sizeof(vivid::gpu::InstanceData3D));

    // AABB over instance positions, expanded by the base mesh radius * per-instance scale.
    float bmin[3], bmax[3];
    for (int k = 0; k < 3; ++k) { bmin[k] = 1e30f; bmax[k] = -1e30f; }
    for (uint32_t i = 0; i < count; ++i) {
        float ms = std::max({std::fabs(insts[i].scale[0]), std::fabs(insts[i].scale[1]), std::fabs(insts[i].scale[2]), 0.01f}) * mesh_radius;
        for (int k = 0; k < 3; ++k) {
            bmin[k] = std::min(bmin[k], insts[i].position[k] - ms);
            bmax[k] = std::max(bmax[k], insts[i].position[k] + ms);
        }
    }
    WGPURenderPassEncoder pass; WGPUBindGroup bg;
    write_camera(ctx, st, p, bmin, bmax, color3, &pass, &bg);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, vb_size);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, st.inst_buf, 0, static_cast<uint64_t>(count)*sizeof(vivid::gpu::InstanceData3D));
    wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, static_cast<uint64_t>(index_count)*sizeof(uint32_t));
    wgpuRenderPassEncoderDrawIndexed(pass, index_count, count, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bg);
}

// Convenience: instance a unit cube at the given CPU transforms (for the generator ops, which output
// an InstanceArray3D bundle and have no mesh of their own).
inline void render_instances_cpu(const VividGpuContext* ctx, State& st,
                                 const vivid::gpu::InstanceData3D* insts, uint32_t count, const float color3[3] = nullptr) {
    if (!ctx_ok(ctx)) return;
    const Prim& cube = prim_cube(ctx->device, ctx->queue);
    render_instanced(ctx, st, cube.vb, cube.vb_size, cube.ib, cube.index_count, cube.radius, insts, count, color3);
}

// Convenience: a rotating proxy sphere (SDF3D, Light3D). `emissive` brightens the ambient for lights.
inline void render_proxy_sphere(const VividGpuContext* ctx, State& st, const float color3[3], bool emissive = false) {
    if (!ctx_ok(ctx)) return;
    const Prim& sph = prim_sphere(ctx->device, ctx->queue);
    float col[3] = { color3 ? color3[0] : 0.7f, color3 ? color3[1] : 0.7f, color3 ? color3[2] : 0.8f };
    if (emissive) { for (float& c : col) c = std::min(1.f, c * 1.4f + 0.25f); }
    const float bmin[3] = {-0.5f,-0.5f,-0.5f}, bmax[3] = {0.5f,0.5f,0.5f};
    (void)sph;
    render(ctx, st, sph.vb, sph.vb_size, sph.ib, sph.index_count, bmin, bmax, col);
}

// Convenience: a procedural swirling particle cloud (Particles3D, whose instances live only on the GPU).
inline void render_particles_proxy(const VividGpuContext* ctx, State& st, const float color3[3]) {
    if (!ctx_ok(ctx)) return;
    const uint32_t N = 96;
    std::vector<vivid::gpu::InstanceData3D> pts(N);
    const float t = static_cast<float>(ctx->time);
    for (uint32_t i = 0; i < N; ++i) {
        float fi = static_cast<float>(i);
        float a = fi * 2.399963f + t * 0.5f;           // golden-angle spiral, slowly rotating
        float r = 0.15f + 1.05f * (fi / static_cast<float>(N));
        float y = std::sin(fi * 0.7f + t * 0.9f) * 0.6f;
        vivid::gpu::InstanceData3D d{};
        d.position[0] = r * std::cos(a); d.position[1] = y; d.position[2] = r * std::sin(a);
        d.scale[0] = d.scale[1] = d.scale[2] = 0.06f + 0.03f * std::sin(fi + t * 2.f);
        d.rotation_y = a;
        d.color[0] = color3 ? color3[0] : 0.7f; d.color[1] = color3 ? color3[1] : 0.8f;
        d.color[2] = color3 ? color3[2] : 1.0f; d.color[3] = 1.f;
        pts[i] = d;
    }
    render_instances_cpu(ctx, st, pts.data(), N, color3);
}

// Convenience: two overlapping proxy spheres (SceneMerge — a compositor with no mesh of its own).
inline void render_merge_proxy(const VividGpuContext* ctx, State& st, const float color3[3]) {
    if (!ctx_ok(ctx)) return;
    vivid::gpu::InstanceData3D two[2]{};
    two[0].position[0] = -0.35f; two[0].scale[0]=two[0].scale[1]=two[0].scale[2]=1.0f;
    two[0].color[0]=0.42f; two[0].color[1]=0.62f; two[0].color[2]=0.95f; two[0].color[3]=1.f;
    two[1].position[0] =  0.35f; two[1].scale[0]=two[1].scale[1]=two[1].scale[2]=1.0f;
    two[1].color[0]=0.95f; two[1].color[1]=0.55f; two[1].color[2]=0.42f; two[1].color[3]=1.f;
    if (!ctx_ok(ctx)) return;
    const Prim& sph = prim_sphere(ctx->device, ctx->queue);
    render_instanced(ctx, st, sph.vb, sph.vb_size, sph.ib, sph.index_count, sph.radius, two, 2, color3);
}

inline void destroy(State& st) {
    if (st.depth_view) vivid::gpu::release(st.depth_view);
    if (st.depth)      vivid::gpu::release(st.depth);
    if (st.ubo)        vivid::gpu::release(st.ubo);
    if (st.inst_buf)   vivid::gpu::release(st.inst_buf);
    st = State{};
}

}  // namespace vivid::thumb3d
