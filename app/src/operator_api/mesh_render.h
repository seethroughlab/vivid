#pragma once
// GPU-side geometry helpers, header-only, shared by every mesh operator so the vertex/index upload
// and the MVP + depth + flat-lit-textured render pipeline exist exactly ONCE:
//   - GpuMesh      : owns wgpu vertex/index buffers for a CpuMesh; exposes a VividMesh (the value a
//                    producer op publishes on a custom-ref port). Buffers get Storage usage so a
//                    compute op (MeshDisplace) can read them.
//   - MeshRenderer : owns the shader/pipeline/depth/sampler; renders any VividMesh (MeshVertex
//                    layout) with a spinning camera, tint, flat lighting and an optional baseColor.
//                    The vertex shader also carries the 3D-noise displacement Model needs (amount 0
//                    => untouched), so one pipeline serves Model AND the composable MeshRender.
// Depends on mesh_types.h for Mat4/MeshVertex only (no glTF/json needed to render).
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_types.h"
#include "operator_api/mesh_types.h"

#include <cstdint>
#include <string>

namespace vivid::geom {

// The one vertex layout every mesh operator agrees on (matches MeshVertex: pos@0, nrm@1, uv@2).
inline const VividVertexAttribute* mesh_vertex_attributes(uint32_t& count) {
    static const VividVertexAttribute kAttrs[3] = {
        { WGPUVertexFormat_Float32x3, offsetof(MeshVertex, px), 0 },
        { WGPUVertexFormat_Float32x3, offsetof(MeshVertex, nx), 1 },
        { WGPUVertexFormat_Float32x2, offsetof(MeshVertex, u),  2 },
    };
    count = 3;
    return kAttrs;
}

// Owns the wgpu buffers for a mesh and the VividMesh view onto them. Upload once (on file change);
// republish `mesh` each frame. Buffers carry Storage usage so a compute pass can read them.
struct GpuMesh {
    WGPUBuffer vbo = nullptr, ibo = nullptr;
    uint32_t   vert_n = 0, index_n = 0;
    VividMesh  mesh{};

    bool valid() const { return vbo && ibo && index_n > 0; }

    void release() {
        if (vbo) { wgpuBufferRelease(vbo); vbo = nullptr; }
        if (ibo) { wgpuBufferRelease(ibo); ibo = nullptr; }
        vert_n = index_n = 0; mesh = VividMesh{};
    }

    // Create fresh vertex+index buffers from a CpuMesh and fill `mesh`. Releases any prior buffers.
    void upload(const VividGpuContext* c, const CpuMesh& m) {
        release();
        if (m.verts.empty() || m.indices.empty()) return;
        const uint32_t vbytes = (uint32_t)(m.verts.size() * sizeof(MeshVertex));
        WGPUBufferDescriptor vd{};
        vd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        vd.size = vbytes;
        vbo = wgpuDeviceCreateBuffer(c->device, &vd);
        wgpuQueueWriteBuffer(c->queue, vbo, 0, m.verts.data(), vbytes);
        const uint32_t ibytes = (uint32_t)(m.indices.size() * 4);
        WGPUBufferDescriptor id{};
        id.usage = WGPUBufferUsage_Index | WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        id.size = ibytes;
        ibo = wgpuDeviceCreateBuffer(c->device, &id);
        wgpuQueueWriteBuffer(c->queue, ibo, 0, m.indices.data(), ibytes);
        vert_n = (uint32_t)m.verts.size(); index_n = (uint32_t)m.indices.size();
        fill_view();
    }

    // Point `mesh` at the current buffers (call after swapping vbo/ibo, e.g. a compute op's output).
    void fill_view() {
        uint32_t nattr = 0; const VividVertexAttribute* attrs = mesh_vertex_attributes(nattr);
        mesh = VividMesh{};
        mesh.vertex_buffer = vbo; mesh.vertex_buffer_offset = 0;
        mesh.vertex_count = vert_n; mesh.vertex_stride = (uint32_t)sizeof(MeshVertex);
        mesh.index_buffer = ibo; mesh.index_format = WGPUIndexFormat_Uint32; mesh.index_count = index_n;
        mesh.topology = WGPUPrimitiveTopology_TriangleList;
        mesh.attributes = attrs; mesh.attribute_count = nattr;
    }
};

// The spinning-camera transform Model/MeshRender share: size (0..1 -> scale), spin (0..1 -> speed),
// tilt (0..1 -> ±π), evaluated at `time` for a given aspect. Returns MVP and the model matrix
// (the latter transforms normals for lighting).
struct MeshTransform { Mat4 mvp, model; };
inline MeshTransform spinning_camera(float size01, float spin01, float tilt01, float time, float aspect) {
    const float sc  = 0.4f + size01 * 1.4f;
    const float spd = spin01 * 1.4f;
    const float tlt = (tilt01 - 0.5f) * 3.14159265f;
    Mat4 model  = mul(rot_y(time * spd), rot_x(tlt));
    Mat4 modelS = mul(model, scale3(sc));
    Mat4 view   = translate(0.f, 0.f, -3.0f);
    Mat4 proj   = perspective(0.7854f, aspect, 0.05f, 100.f);
    return { mul(proj, mul(view, modelS)), model };
}

inline const char* mesh_render_wgsl() {
    return R"(
struct U { mvp: mat4x4<f32>, model: mat4x4<f32>, tint: vec4f, light: vec4f, misc: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
struct VIn { @location(0) pos: vec3f, @location(1) nrm: vec3f, @location(2) uv: vec2f };
struct VOut { @builtin(position) pos: vec4f, @location(0) shade: f32, @location(1) uv: vec2f };
fn hash13(q: vec3f) -> f32 {
    var p = fract(q * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}
fn vnoise(x: vec3f) -> f32 {
    let i = floor(x); let f = fract(x); let s = f * f * (3.0 - 2.0 * f);
    let n000 = hash13(i);                     let n100 = hash13(i + vec3f(1.0,0.0,0.0));
    let n010 = hash13(i + vec3f(0.0,1.0,0.0)); let n110 = hash13(i + vec3f(1.0,1.0,0.0));
    let n001 = hash13(i + vec3f(0.0,0.0,1.0)); let n101 = hash13(i + vec3f(1.0,0.0,1.0));
    let n011 = hash13(i + vec3f(0.0,1.0,1.0)); let n111 = hash13(i + vec3f(1.0,1.0,1.0));
    let x00 = mix(n000, n100, s.x); let x10 = mix(n010, n110, s.x);
    let x01 = mix(n001, n101, s.x); let x11 = mix(n011, n111, s.x);
    return mix(mix(x00, x10, s.y), mix(x01, x11, s.y), s.z);
}
@vertex fn vs_main(v: VIn) -> VOut {
    var o: VOut;
    // misc = (noise_amount, noise_freq, time_drift, _). Displace each vertex along its normal by an
    // evolving 3D noise field (object-space). amount 0 => untouched (the plain render path).
    let drift = vec3f(u.misc.z * 0.6, u.misc.z, u.misc.z * 1.4);
    let d = (vnoise(v.pos * u.misc.y + drift) - 0.5) * 2.0;
    let disp = v.pos + v.nrm * (d * u.misc.x);
    o.pos = u.mvp * vec4f(disp, 1.0);
    let n = normalize((u.model * vec4f(v.nrm, 0.0)).xyz);
    let l = normalize(u.light.xyz);
    o.shade = u.light.w + (1.0 - u.light.w) * max(dot(n, l), 0.0);
    o.uv = v.uv;
    return o;
}
@fragment fn fs_main(i: VOut) -> @location(0) vec4f {
    let c = textureSample(tex, samp, i.uv).rgb;
    return vec4f(c * u.tint.rgb * i.shade, 1.0);
}
)";
}

// The shared render pipeline: MVP + depth + flat-lit + (optional) baseColor. One per operator
// instance. Lazily built on first render; sized depth follows the output each frame.
struct MeshRenderer {
    WGPUShaderModule    sh_  = nullptr;  WGPUBindGroupLayout bgl_ = nullptr;  WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline  pipe_ = nullptr; WGPUBuffer ubo_ = nullptr;  WGPUSampler samp_ = nullptr;  WGPUBindGroup bg_ = nullptr;
    WGPUTexture white_ = nullptr; WGPUTextureView white_view_ = nullptr;   // untextured fallback
    WGPUTextureView bound_view_ = nullptr;                                 // baseColor currently in bg_
    WGPUTexture depth_ = nullptr; WGPUTextureView depth_view_ = nullptr; uint32_t dw_ = 0, dh_ = 0;
    std::string err_;

    void release() {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (white_view_) wgpuTextureViewRelease(white_view_); if (white_) wgpuTextureRelease(white_);
        if (depth_view_) wgpuTextureViewRelease(depth_view_); if (depth_) wgpuTextureRelease(depth_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
        *this = MeshRenderer{};
    }

    bool ready() const { return pipe_ != nullptr && bg_ != nullptr; }

    // Build shader/pipeline/ubo/sampler/white/bind-group. Returns false + sets err_ on failure.
    bool init(const VividGpuContext* c) {
        sh_ = vivid::gpu::create_shader_checked(c->device, mesh_render_wgsl(), "MeshRender", err_);
        if (!sh_ || !err_.empty()) { if (err_.empty()) err_ = "shader null"; return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 176, "Mesh U");
        WGPUBindGroupLayoutEntry e[3]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 176;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment; e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 3; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        uint32_t nattr = 0; const VividVertexAttribute* ma = mesh_vertex_attributes(nattr);
        WGPUVertexAttribute attrs[3]{};
        for (uint32_t i = 0; i < nattr; ++i) { attrs[i].format = ma[i].format; attrs[i].offset = ma[i].offset; attrs[i].shaderLocation = ma[i].shader_location; }
        WGPUVertexBufferLayout vbl{}; vbl.arrayStride = sizeof(MeshVertex); vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = nattr; vbl.attributes = attrs;
        WGPUColorTargetState ct{}; ct.format = c->output_format; ct.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs{}; fs.module = sh_; fs.entryPoint = vivid_sv("fs_main"); fs.targetCount = 1; fs.targets = &ct;
        WGPUDepthStencilState ds{}; ds.format = WGPUTextureFormat_Depth24Plus;
        ds.depthWriteEnabled = WGPUOptionalBool_True; ds.depthCompare = WGPUCompareFunction_Less;
        ds.stencilFront.compare = WGPUCompareFunction_Always; ds.stencilBack.compare = WGPUCompareFunction_Always;
        WGPURenderPipelineDescriptor rp{}; rp.layout = pl_;
        rp.vertex.module = sh_; rp.vertex.entryPoint = vivid_sv("vs_main"); rp.vertex.bufferCount = 1; rp.vertex.buffers = &vbl;
        rp.primitive.topology = WGPUPrimitiveTopology_TriangleList; rp.primitive.frontFace = WGPUFrontFace_CCW;
        rp.primitive.cullMode = WGPUCullMode_None;
        rp.multisample.count = 1; rp.multisample.mask = 0xFFFFFFFF; rp.fragment = &fs; rp.depthStencil = &ds;
        pipe_ = wgpuDeviceCreateRenderPipeline(c->device, &rp);
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_Repeat; sd.addressModeV = WGPUAddressMode_Repeat; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        make_white(c);
        set_base_color(c, nullptr);   // valid bind group (white) before first render
        if (!pipe_) { err_ = "pipeline null"; return false; }
        return bg_ != nullptr;
    }

    // Swap the baseColor texture bound in the render bind group (null => the white fallback). No-op if
    // unchanged, so it is cheap to call every frame.
    void set_base_color(const VividGpuContext* c, WGPUTextureView view) {
        WGPUTextureView want = view ? view : white_view_;
        if (bg_ && want == bound_view_) return;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[3]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 176;
        be[1].binding = 1; be[1].textureView = want;
        be[2].binding = 2; be[2].sampler = samp_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 3; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        bound_view_ = want;
    }

    // Render `mesh` into the context's output. transform = camera; tint = rgb multiply; light =
    // (dir.xyz, ambient); misc = (noise_amount, noise_freq, time_drift); bg = clear colour (alpha 0
    // so the model composites); base_color = optional texture (null -> white). Clears colour+depth.
    void render(const VividGpuContext* c, const VividMesh& mesh, const MeshTransform& transform,
                float tr, float tg, float tb, const float light[4], const float misc[3],
                float br, float bg_col, float bb, WGPUTextureView base_color) {
        ensure_depth(c);
        set_base_color(c, base_color);
        float u[44]{};
        for (int i = 0; i < 16; ++i) u[i] = transform.mvp[i];
        for (int i = 0; i < 16; ++i) u[16 + i] = transform.model[i];
        u[32] = tr; u[33] = tg; u[34] = tb; u[35] = 1.f;
        u[36] = light[0]; u[37] = light[1]; u[38] = light[2]; u[39] = light[3];
        u[40] = misc[0]; u[41] = misc[1]; u[42] = misc[2]; u[43] = 0.f;
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));

        WGPURenderPassColorAttachment cat{};
        cat.view = c->output_texture_view; cat.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        cat.loadOp = WGPULoadOp_Clear; cat.storeOp = WGPUStoreOp_Store;
        cat.clearValue = { br, bg_col, bb, 0.0 };
        WGPURenderPassDepthStencilAttachment dat{}; dat.view = depth_view_;
        dat.depthLoadOp = WGPULoadOp_Clear; dat.depthStoreOp = WGPUStoreOp_Store; dat.depthClearValue = 1.f;
        WGPURenderPassDescriptor rpd{}; rpd.colorAttachmentCount = 1; rpd.colorAttachments = &cat; rpd.depthStencilAttachment = &dat;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rpd);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
        wgpuRenderPassEncoderSetPipeline(pass, pipe_);
        if (mesh.vertex_buffer && mesh.index_buffer && mesh.index_count) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, mesh.vertex_buffer, mesh.vertex_buffer_offset,
                                                 (uint64_t)mesh.vertex_count * mesh.vertex_stride);
            wgpuRenderPassEncoderSetIndexBuffer(pass, mesh.index_buffer, mesh.index_format, 0, (uint64_t)mesh.index_count * 4);
            wgpuRenderPassEncoderDrawIndexed(pass, mesh.index_count, 1, 0, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

private:
    void make_white(const VividGpuContext* c) {
        static const uint8_t kWhite[4] = { 255, 255, 255, 255 };
        WGPUTextureDescriptor td{}; td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = { 1, 1, 1 }; td.format = WGPUTextureFormat_RGBA8Unorm;
        td.mipLevelCount = 1; td.sampleCount = 1;
        white_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTexelCopyTextureInfo dst{}; dst.texture = white_; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay{}; lay.bytesPerRow = 4; lay.rowsPerImage = 1; WGPUExtent3D ext{ 1, 1, 1 };
        wgpuQueueWriteTexture(c->queue, &dst, kWhite, 4, &lay, &ext);
        white_view_ = wgpuTextureCreateView(white_, nullptr);
    }
    void ensure_depth(const VividGpuContext* c) {
        if (depth_ && dw_ == c->output_width && dh_ == c->output_height) return;
        if (depth_view_) wgpuTextureViewRelease(depth_view_); if (depth_) wgpuTextureRelease(depth_);
        WGPUTextureDescriptor td{}; td.size = { c->output_width, c->output_height, 1 };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_Depth24Plus; td.usage = WGPUTextureUsage_RenderAttachment;
        depth_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTextureViewDescriptor vd{}; vd.format = WGPUTextureFormat_Depth24Plus; vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
        depth_view_ = wgpuTextureCreateView(depth_, &vd);
        dw_ = c->output_width; dh_ = c->output_height;
    }
};

// Upload an RGBA8 texture the renderer can sample as baseColor. Owns one texture+view; re-upload
// replaces it. Used by ops that carry a per-mesh texture (Model). Kept separate from MeshRenderer so
// the renderer stays mesh-only and the texture lifetime is the producer op's concern.
struct BaseColorTexture {
    WGPUTexture tex_ = nullptr; WGPUTextureView view_ = nullptr;
    WGPUTextureView view() const { return view_; }
    void release() {
        if (view_) { wgpuTextureViewRelease(view_); view_ = nullptr; }
        if (tex_)  { wgpuTextureRelease(tex_); tex_ = nullptr; }
    }
    void set(const VividGpuContext* c, const uint8_t* rgba, uint32_t w, uint32_t h) {
        release();
        WGPUTextureDescriptor td{}; td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = { w, h, 1 }; td.format = WGPUTextureFormat_RGBA8Unorm;
        td.mipLevelCount = 1; td.sampleCount = 1;
        tex_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTexelCopyTextureInfo dst{}; dst.texture = tex_; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay{}; lay.bytesPerRow = w * 4; lay.rowsPerImage = h; WGPUExtent3D ext{ w, h, 1 };
        wgpuQueueWriteTexture(c->queue, &dst, rgba, (size_t)w * h * 4, &lay, &ext);
        view_ = wgpuTextureCreateView(tex_, nullptr);
    }
};

}  // namespace vivid::geom
