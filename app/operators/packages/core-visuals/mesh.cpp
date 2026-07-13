// Core visual package operator: Mesh — migrated verbatim from the built-in MeshOp
// (builtin_ops.cpp); behaviour unchanged. Real vertex geometry via a raw-wgpu pipeline.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
// actual 3D vertex buffer through a full MVP pipeline with a DEPTH buffer — the 3D half of the
// geometry family (the counterpart to ShapeGrid/Lines/VectorText's flat NDC geometry). Solid mode
// is flat-lit (per-face normals · a fixed light); wireframe mode is a LineList of the edges. The
// mesh spins over time; size/spin/tilt/colour animate via the uniform. A generator (clears to bg).
// Camera is fixed (looking down -Z from z=+3.2); the model rotates. ---
struct MVert { float px, py, pz, nx, ny, nz; };   // 3D position + face normal (flat shading)
namespace mesh_math {
    using Mat4 = std::array<float, 16>;            // column-major (WGSL convention): (row i,col j) = m[j*4+i]
    inline Mat4 identity() { Mat4 m{}; m[0] = m[5] = m[10] = m[15] = 1.f; return m; }
    inline Mat4 mul(const Mat4& a, const Mat4& b) {  // a*b, column-major
        Mat4 c{};
        for (int j = 0; j < 4; ++j) for (int i = 0; i < 4; ++i) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + i] * b[j * 4 + k];
            c[j * 4 + i] = s;
        }
        return c;
    }
    inline Mat4 rot_x(float a) { Mat4 m = identity(); const float c = std::cos(a), s = std::sin(a);
        m[5] = c; m[9] = -s; m[6] = s; m[10] = c; return m; }
    inline Mat4 rot_y(float a) { Mat4 m = identity(); const float c = std::cos(a), s = std::sin(a);
        m[0] = c; m[8] = s; m[2] = -s; m[10] = c; return m; }
    inline Mat4 translate(float x, float y, float z) { Mat4 m = identity(); m[12] = x; m[13] = y; m[14] = z; return m; }
    // WebGPU perspective (clip z in [0,1]); y is negated to compensate the NDC y-flip (see gotchas).
    inline Mat4 perspective(float fovy, float aspect, float znear, float zfar) {
        Mat4 m{}; const float f = 1.f / std::tan(fovy * 0.5f);
        m[0] = f / aspect; m[5] = -f;                       // negate y -> screen-correct
        m[10] = zfar / (znear - zfar); m[11] = -1.f;
        m[14] = (znear * zfar) / (znear - zfar);
        return m;
    }
}
const char* kMeshWGSL = R"(
struct U { mvp: mat4x4<f32>, model: mat4x4<f32>, fill: vec4f, light: vec4f };
@group(0) @binding(0) var<uniform> u: U;
struct VIn { @location(0) pos: vec3f, @location(1) nrm: vec3f };
struct VOut { @builtin(position) pos: vec4f, @location(0) shade: f32 };
@vertex fn vs_main(v: VIn) -> VOut {
    var o: VOut;
    o.pos = u.mvp * vec4f(v.pos, 1.0);
    let n = normalize((u.model * vec4f(v.nrm, 0.0)).xyz);
    let l = normalize(u.light.xyz);
    let diff = max(dot(n, l), 0.0);
    o.shade = u.light.w + (1.0 - u.light.w) * diff;         // light.w = ambient (1.0 => flat, for wireframe)
    return o;
}
@fragment fn fs_main(i: VOut) -> @location(0) vec4f { return vec4f(u.fill.rgb * i.shade, 1.0); }
)";
}  // namespace

struct MeshOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Mesh";
    static constexpr const char* kDisplayName = "Mesh";
    static constexpr const char* kSummary = "Real 3D geometry: spinning platonic solids (solid flat-lit or wireframe).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "geometry", "3d"};
    vivid::Param<float> shape{"shape", 0.f, 0.f, 1.f};             // -> cube/tetra/octa/icosa
    vivid::Param<float> wireframe{"wireframe", 0.f, 0.f, 1.f};     // <.5 solid, >=.5 wireframe
    vivid::Param<float> size{"size", 0.6f, 0.f, 1.f};
    vivid::Param<float> spin{"spin", 0.35f, 0.f, 1.f}, tilt{"tilt", 0.5f, 0.f, 1.f};
    vivid::Param<float> r{"r", 0.9f, 0.f, 1.f}, g{"g", 0.92f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f};
    vivid::Param<float> bg_r{"bg_r", 0.03f, 0.f, 1.f}, bg_g{"bg_g", 0.03f, 0.f, 1.f}, bg_b{"bg_b", 0.05f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline solid_pipe_ = nullptr, wire_pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer tri_vbo_ = nullptr, line_vbo_ = nullptr; uint32_t tri_n_ = 0, line_n_ = 0;
    WGPUTexture depth_ = nullptr; WGPUTextureView depth_view_ = nullptr; uint32_t dw_ = 0, dh_ = 0;
    int shape_ = -1;
    ~MeshOp() override {
        if (depth_view_) wgpuTextureViewRelease(depth_view_); if (depth_) wgpuTextureRelease(depth_);
        if (tri_vbo_) wgpuBufferRelease(tri_vbo_); if (line_vbo_) wgpuBufferRelease(line_vbo_);
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (solid_pipe_) wgpuRenderPipelineRelease(solid_pipe_); if (wire_pipe_) wgpuRenderPipelineRelease(wire_pipe_);
        if (pl_) wgpuPipelineLayoutRelease(pl_); if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR; bg_r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&shape); o.push_back(&wireframe); o.push_back(&size); o.push_back(&spin); o.push_back(&tilt);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&bg_r); o.push_back(&bg_g); o.push_back(&bg_b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    // Platonic-solid tables: unit-ish vertices + polygon faces (index lists). Normals computed per-face.
    static void solid_data(int s, std::vector<std::array<float,3>>& V, std::vector<std::vector<int>>& F) {
        if (s == 0) {                                        // cube
            V = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
            F = {{4,5,6,7},{0,1,2,3},{0,3,7,4},{1,5,6,2},{3,2,6,7},{0,4,5,1}};
        } else if (s == 1) {                                 // tetrahedron
            V = {{1,1,1},{1,-1,-1},{-1,1,-1},{-1,-1,1}};
            F = {{0,1,2},{0,3,1},{0,2,3},{1,3,2}};
        } else if (s == 2) {                                 // octahedron
            V = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            F = {{0,2,4},{2,1,4},{1,3,4},{3,0,4},{0,5,2},{2,5,1},{1,5,3},{3,5,0}};
        } else {                                             // icosahedron
            const float t = 1.61803399f;
            V = {{-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},{0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},
                 {t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}};
            F = {{0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
                 {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}};
        }
    }
    void rebuild_geometry(const VividGpuContext* ctx, int s) {
        std::vector<std::array<float,3>> V; std::vector<std::vector<int>> F; solid_data(s, V, F);
        float maxr = 1e-6f;                                  // normalize to unit radius (comparable sizes)
        for (auto& p : V) maxr = std::max(maxr, std::sqrt(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]));
        for (auto& p : V) { p[0] /= maxr; p[1] /= maxr; p[2] /= maxr; }
        std::vector<MVert> tris; std::set<std::pair<int,int>> edges;
        for (const auto& f : F) {
            const auto& a = V[f[0]]; const auto& b = V[f[1]]; const auto& c = V[f[2]];
            float ux = b[0]-a[0], uy = b[1]-a[1], uz = b[2]-a[2];
            float vx = c[0]-a[0], vy = c[1]-a[1], vz = c[2]-a[2];
            float nx = uy*vz - uz*vy, ny = uz*vx - ux*vz, nz = ux*vy - uy*vx;
            float nl = std::sqrt(nx*nx + ny*ny + nz*nz); if (nl < 1e-6f) nl = 1.f; nx/=nl; ny/=nl; nz/=nl;
            // centroid of the face -> flip normal outward (mesh is centered at origin)
            float cxf=0, cyf=0, czf=0; for (int idx : f) { cxf+=V[idx][0]; cyf+=V[idx][1]; czf+=V[idx][2]; }
            const float inv = 1.f/f.size(); cxf*=inv; cyf*=inv; czf*=inv;
            if (nx*cxf + ny*cyf + nz*czf < 0.f) { nx=-nx; ny=-ny; nz=-nz; }
            for (size_t i = 1; i + 1 < f.size(); ++i) {      // fan-triangulate the polygon
                for (int idx : {f[0], f[(int)i], f[(int)i+1]}) {
                    const auto& q = V[idx]; tris.push_back({q[0],q[1],q[2], nx,ny,nz});
                }
            }
            for (size_t i = 0; i < f.size(); ++i) {          // unique edges for the wireframe
                int e0 = f[i], e1 = f[(i+1) % f.size()];
                edges.insert({std::min(e0,e1), std::max(e0,e1)});
            }
        }
        std::vector<MVert> lines;
        for (auto& e : edges) {
            const auto& a = V[e.first]; const auto& b = V[e.second];
            lines.push_back({a[0],a[1],a[2], 0,0,0}); lines.push_back({b[0],b[1],b[2], 0,0,0});
        }
        tri_n_ = (uint32_t)tris.size(); line_n_ = (uint32_t)lines.size();
        auto upload = [&](WGPUBuffer& buf, const std::vector<MVert>& v) {
            if (buf) wgpuBufferRelease(buf);
            const uint32_t bytes = (uint32_t)(v.size() * sizeof(MVert));
            WGPUBufferDescriptor bd{}; bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = bytes ? bytes : 16;
            buf = wgpuDeviceCreateBuffer(ctx->device, &bd);
            if (bytes) wgpuQueueWriteBuffer(ctx->queue, buf, 0, v.data(), bytes);
        };
        upload(tri_vbo_, tris); upload(line_vbo_, lines);
        shape_ = s;
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
    WGPURenderPipeline make_pipe(const VividGpuContext* c, WGPUPrimitiveTopology topo) {
        WGPUVertexAttribute attrs[2]{};
        attrs[0].format = WGPUVertexFormat_Float32x3; attrs[0].offset = offsetof(MVert, px); attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x3; attrs[1].offset = offsetof(MVert, nx); attrs[1].shaderLocation = 1;
        WGPUVertexBufferLayout vbl{}; vbl.arrayStride = sizeof(MVert); vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = 2; vbl.attributes = attrs;
        WGPUColorTargetState ct{}; ct.format = c->output_format; ct.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs{}; fs.module = sh_; fs.entryPoint = vivid_sv("fs_main"); fs.targetCount = 1; fs.targets = &ct;
        WGPUDepthStencilState ds{}; ds.format = WGPUTextureFormat_Depth24Plus;
        ds.depthWriteEnabled = WGPUOptionalBool_True; ds.depthCompare = WGPUCompareFunction_Less;
        ds.stencilFront.compare = WGPUCompareFunction_Always; ds.stencilBack.compare = WGPUCompareFunction_Always;
        WGPURenderPipelineDescriptor rp{}; rp.layout = pl_;
        rp.vertex.module = sh_; rp.vertex.entryPoint = vivid_sv("vs_main");
        rp.vertex.bufferCount = 1; rp.vertex.buffers = &vbl;
        rp.primitive.topology = topo; rp.primitive.frontFace = WGPUFrontFace_CCW; rp.primitive.cullMode = WGPUCullMode_None;
        rp.multisample.count = 1; rp.multisample.mask = 0xFFFFFFFF; rp.fragment = &fs; rp.depthStencil = &ds;
        return wgpuDeviceCreateRenderPipeline(c->device, &rp);
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kMeshWGSL, "Mesh", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 160, "Mesh U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 160;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        solid_pipe_ = make_pipe(c, WGPUPrimitiveTopology_TriangleList);
        wire_pipe_  = make_pipe(c, WGPUPrimitiveTopology_LineList);
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 160;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return solid_pipe_ && wire_pipe_;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!solid_pipe_ || !wire_pipe_) return;
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        const int s = static_cast<int>(std::lround(pv(0, shape.value) * 3.f));           // 0..3
        if (s != shape_) rebuild_geometry(c, s);
        ensure_depth(c);
        const bool wire = pv(1, wireframe.value) >= 0.5f;
        const float t = float(c->time);
        const float scale = 0.5f + pv(2, size.value) * 1.3f;
        const float spd = pv(3, spin.value) * 1.6f;
        const float tilt_a = (pv(4, tilt.value) - 0.5f) * 3.14159265f;
        using namespace mesh_math;
        Mat4 model = mul(rot_y(t * spd), rot_x(tilt_a + t * spd * 0.37f));
        Mat4 modelS = mul(model, Mat4{scale,0,0,0, 0,scale,0,0, 0,0,scale,0, 0,0,0,1});   // scale then rotate
        Mat4 view = translate(0.f, 0.f, -3.2f);
        // 45deg fov, projected at the REAL target aspect (was hard-coded 16:9 to compensate for a
        // fixed 2.4:1 FBO that got stretched on present; the output now has a true aspect).
        Mat4 proj = perspective(0.7854f, float(c->output_width) / std::max(1.f, float(c->output_height)),
                                0.1f, 100.f);
        Mat4 mvp = mul(proj, mul(view, modelS));
        float u[40]{};
        for (int i = 0; i < 16; ++i) u[i] = mvp[i];            // mvp        (0..63)
        for (int i = 0; i < 16; ++i) u[16 + i] = model[i];     // model      (64..127)
        u[32] = pv(5, r.value); u[33] = pv(6, g.value); u[34] = pv(7, b.value); u[35] = 1.f;   // fill (128)
        u[36] = 0.4f; u[37] = 0.7f; u[38] = 0.55f; u[39] = wire ? 1.f : 0.28f;                 // light dir + ambient (144)
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        WGPURenderPassColorAttachment cat{};
        cat.view = c->output_texture_view; cat.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        cat.loadOp = WGPULoadOp_Clear; cat.storeOp = WGPUStoreOp_Store;
        cat.clearValue = { pv(8, bg_r.value), pv(9, bg_g.value), pv(10, bg_b.value), 1.0 };
        WGPURenderPassDepthStencilAttachment dat{}; dat.view = depth_view_;
        dat.depthLoadOp = WGPULoadOp_Clear; dat.depthStoreOp = WGPUStoreOp_Store; dat.depthClearValue = 1.f;
        WGPURenderPassDescriptor rpd{}; rpd.colorAttachmentCount = 1; rpd.colorAttachments = &cat; rpd.depthStencilAttachment = &dat;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rpd);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
        WGPUBuffer vbo = wire ? line_vbo_ : tri_vbo_; const uint32_t n = wire ? line_n_ : tri_n_;
        wgpuRenderPassEncoderSetPipeline(pass, wire ? wire_pipe_ : solid_pipe_);
        if (vbo && n) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbo, 0, n * sizeof(MVert));
            wgpuRenderPassEncoderDraw(pass, n, 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
};

VIVID_REGISTER(MeshOp)
