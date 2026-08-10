// Core visual package operator: Solids — draws one small 3D SOLID (cube / tetrahedron / octahedron)
// per element of an incoming signal's ACTIVE set. pos -> screen x + hue, amp -> scale; all solids
// share a slow rotation so the forms read as volumes, not flat shapes. Depth-tested, flat-shaded (hard
// facets), with a wireframe mode (barycentric edges) for the technical / data-viz look. Agnostic to the
// source — never refers to "notes"; a Notes/Beat/onset signal drives it identically. One instanced
// draw of a static shape mesh + a per-instance transform buffer. Renders over transparent so it composites.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/element_geom.h"   // VividSignal, input_signal
#include "operator_api/mesh_types.h"     // Mat4 + perspective/rot/translate/mul

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace {
using vivid::geom::Mat4;
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
struct SVert { float px, py, pz, nx, ny, nz, bx, by, bz; };   // pos, flat normal, barycentric (for wireframe)
struct SInst { float px, py, pz, scale, r, g, b; };

// Push a flat-shaded triangle (face normal from the winding) with barycentric coords for edges.
void tri(std::vector<SVert>& v, const float* a, const float* b, const float* c) {
    const float ux = b[0]-a[0], uy = b[1]-a[1], uz = b[2]-a[2];
    const float vx = c[0]-a[0], vy = c[1]-a[1], vz = c[2]-a[2];
    float nx = uy*vz - uz*vy, ny = uz*vx - ux*vz, nz = ux*vy - uy*vx;
    const float l = std::sqrt(nx*nx+ny*ny+nz*nz); if (l>1e-6f){nx/=l;ny/=l;nz/=l;}
    v.push_back({a[0],a[1],a[2],nx,ny,nz,1,0,0});
    v.push_back({b[0],b[1],b[2],nx,ny,nz,0,1,0});
    v.push_back({c[0],c[1],c[2],nx,ny,nz,0,0,1});
}
std::vector<SVert> make_cube() {
    std::vector<SVert> v; const float s=0.6f;
    const float p[8][3]={{-s,-s,-s},{s,-s,-s},{s,s,-s},{-s,s,-s},{-s,-s,s},{s,-s,s},{s,s,s},{-s,s,s}};
    const int f[6][4]={{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{4,5,1,0},{3,2,6,7}};
    for (auto& q : f) { tri(v,p[q[0]],p[q[1]],p[q[2]]); tri(v,p[q[0]],p[q[2]],p[q[3]]); }
    return v;
}
std::vector<SVert> make_tetra() {
    std::vector<SVert> v; const float s=0.8f;
    const float p[4][3]={{s,s,s},{s,-s,-s},{-s,s,-s},{-s,-s,s}};
    const int f[4][3]={{0,1,2},{0,3,1},{0,2,3},{1,3,2}};
    for (auto& q : f) tri(v,p[q[0]],p[q[1]],p[q[2]]);
    return v;
}
std::vector<SVert> make_octa() {
    std::vector<SVert> v; const float s=0.8f;
    const float p[6][3]={{s,0,0},{-s,0,0},{0,s,0},{0,-s,0},{0,0,s},{0,0,-s}};
    const int f[8][3]={{0,2,4},{2,1,4},{1,3,4},{3,0,4},{2,0,5},{1,2,5},{3,1,5},{0,3,5}};
    for (auto& q : f) tri(v,p[q[0]],p[q[1]],p[q[2]]);
    return v;
}
const char* kWGSL = R"(
struct U { viewproj: mat4x4<f32>, spin: f32, time: f32, wire: f32, pad: f32 };
@group(0) @binding(0) var<uniform> u: U;
struct VIn { @location(0) pos: vec3f, @location(1) nrm: vec3f, @location(2) bary: vec3f,
             @location(3) ipos: vec3f, @location(4) iscale: f32, @location(5) icol: vec3f };
struct VOut { @builtin(position) pos: vec4f, @location(0) shade: f32, @location(1) col: vec3f, @location(2) bary: vec3f };
fn rotY(p: vec3f, a: f32) -> vec3f { let c=cos(a); let s=sin(a); return vec3f(c*p.x+s*p.z, p.y, -s*p.x+c*p.z); }
fn rotX(p: vec3f, a: f32) -> vec3f { let c=cos(a); let s=sin(a); return vec3f(p.x, c*p.y-s*p.z, s*p.y+c*p.z); }
@vertex fn vs_main(v: VIn) -> VOut {
    var o: VOut;
    let a = u.time * u.spin;
    let rp = rotX(rotY(v.pos, a), a * 0.6);
    let rn = rotX(rotY(v.nrm, a), a * 0.6);
    let world = v.ipos + rp * v.iscale;
    o.pos = u.viewproj * vec4f(world, 1.0);
    let ld = normalize(vec3f(0.4, 0.7, 0.6));
    o.shade = 0.35 + 0.65 * max(dot(normalize(rn), ld), 0.0);   // flat facet lighting
    o.col = v.icol;
    o.bary = v.bary;
    return o;
}
@fragment fn fs_main(i: VOut) -> @location(0) vec4f {
    if (u.wire > 0.5) {
        let d = min(min(i.bary.x, i.bary.y), i.bary.z);
        let e = 1.0 - smoothstep(0.0, 0.045, d);      // crisp wireframe edge
        if (e < 0.02) { discard; }
        return vec4f(i.col * e, e);
    }
    return vec4f(i.col * i.shade, 1.0);
}
)";
}  // namespace

struct SolidsOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Solids";
    // ADR-0046: bundles layout + colour + geometry + rendering in one node — a RECIPE.
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_RECIPE;
    static constexpr const char* kDisplayName = "Solids";
    static constexpr const char* kSummary = "One 3D solid (cube/tetra/octa) per element of a signal; pos->x+hue, amp->size, shared rotation. Depth-tested, flat or wireframe.";
    static constexpr std::array<const char*, 3> kKeywords = {"3d", "geometry", "solids"};
    vivid::Param<float> shape{"shape", 0.f, 0.f, 2.f};       // 0 cube · 1 tetra · 2 octa
    vivid::Param<float> size{"size", 0.5f, 0.f, 1.f};
    vivid::Param<float> spread{"spread", 0.8f, 0.f, 1.f};
    vivid::Param<float> spin{"spin", 0.4f, 0.f, 1.f};
    vivid::Param<float> trail{"trail", 0.3f, 0.f, 1.f};
    vivid::Param<float> wireframe{"wireframe", 0.f, 0.f, 1.f};

    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer vbuf_[3] = {nullptr,nullptr,nullptr}; uint32_t vcount_[3] = {0,0,0};   // cube/tetra/octa
    WGPUBuffer inst_ = nullptr; uint32_t inst_cap_ = 0;
    WGPUTexture depth_ = nullptr; WGPUTextureView depth_view_ = nullptr; uint32_t dw_ = 0, dh_ = 0;
    struct Live { int id; float pos; float amp; float age; };
    std::vector<Live> lives_;
    std::vector<SInst> insts_;

    ~SolidsOp() override {
        for (auto b : vbuf_) if (b) wgpuBufferRelease(b);
        if (inst_) wgpuBufferRelease(inst_);
        if (depth_view_) wgpuTextureViewRelease(depth_view_); if (depth_) wgpuTextureRelease(depth_);
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&shape); o.push_back(&size); o.push_back(&spread);
        o.push_back(&spin); o.push_back(&trail); o.push_back(&wireframe);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(VIVID_CUSTOM_REF_PORT("signal", VIVID_PORT_INPUT, VividSignal));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }

    WGPUBuffer make_vb(const VividGpuContext* c, const std::vector<SVert>& v, uint32_t& n) {
        n = static_cast<uint32_t>(v.size());
        WGPUBufferDescriptor d{}; d.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; d.size = v.size()*sizeof(SVert);
        WGPUBuffer b = wgpuDeviceCreateBuffer(c->device, &d);
        wgpuQueueWriteBuffer(c->queue, b, 0, v.data(), d.size);
        return b;
    }
    void ensure_depth(const VividGpuContext* c) {
        if (depth_ && dw_ == c->output_width && dh_ == c->output_height) return;
        if (depth_view_) wgpuTextureViewRelease(depth_view_); if (depth_) wgpuTextureRelease(depth_);
        dw_ = c->output_width; dh_ = c->output_height;
        WGPUTextureDescriptor td{}; td.usage = WGPUTextureUsage_RenderAttachment; td.dimension = WGPUTextureDimension_2D;
        td.size = { dw_, dh_, 1 }; td.format = WGPUTextureFormat_Depth24Plus; td.mipLevelCount = 1; td.sampleCount = 1;
        depth_ = wgpuDeviceCreateTexture(c->device, &td);
        depth_view_ = wgpuTextureCreateView(depth_, nullptr);
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kWGSL, "Solids", err);
        if (!sh_ || !err.empty()) { vivid_report_gpu_error(c, ("Solids WGSL: " + err).c_str()); return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 80, "Solids U");   // mat4(64) + 4 f32(16)
        vbuf_[0] = make_vb(c, make_cube(),  vcount_[0]);
        vbuf_[1] = make_vb(c, make_tetra(), vcount_[1]);
        vbuf_[2] = make_vb(c, make_octa(),  vcount_[2]);
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 80;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        WGPUVertexAttribute va[3]{};
        va[0].format = WGPUVertexFormat_Float32x3; va[0].offset = offsetof(SVert,px); va[0].shaderLocation = 0;
        va[1].format = WGPUVertexFormat_Float32x3; va[1].offset = offsetof(SVert,nx); va[1].shaderLocation = 1;
        va[2].format = WGPUVertexFormat_Float32x3; va[2].offset = offsetof(SVert,bx); va[2].shaderLocation = 2;
        WGPUVertexAttribute ia[3]{};
        ia[0].format = WGPUVertexFormat_Float32x3; ia[0].offset = offsetof(SInst,px);    ia[0].shaderLocation = 3;
        ia[1].format = WGPUVertexFormat_Float32;   ia[1].offset = offsetof(SInst,scale); ia[1].shaderLocation = 4;
        ia[2].format = WGPUVertexFormat_Float32x3; ia[2].offset = offsetof(SInst,r);     ia[2].shaderLocation = 5;
        WGPUVertexBufferLayout vbl[2]{};
        vbl[0].arrayStride = sizeof(SVert); vbl[0].stepMode = WGPUVertexStepMode_Vertex;   vbl[0].attributeCount = 3; vbl[0].attributes = va;
        vbl[1].arrayStride = sizeof(SInst); vbl[1].stepMode = WGPUVertexStepMode_Instance; vbl[1].attributeCount = 3; vbl[1].attributes = ia;
        WGPUBlendState blend{};
        blend.color.srcFactor = WGPUBlendFactor_SrcAlpha; blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha; blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One; blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha; blend.alpha.operation = WGPUBlendOperation_Add;
        WGPUColorTargetState ct{}; ct.format = c->output_format; ct.blend = &blend; ct.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs{}; fs.module = sh_; fs.entryPoint = vivid_sv("fs_main"); fs.targetCount = 1; fs.targets = &ct;
        WGPUDepthStencilState ds{}; ds.format = WGPUTextureFormat_Depth24Plus; ds.depthWriteEnabled = WGPUOptionalBool_True; ds.depthCompare = WGPUCompareFunction_Less;
        WGPURenderPipelineDescriptor rp{}; rp.layout = pl_;
        rp.vertex.module = sh_; rp.vertex.entryPoint = vivid_sv("vs_main");
        rp.vertex.bufferCount = 2; rp.vertex.buffers = vbl;
        rp.primitive.topology = WGPUPrimitiveTopology_TriangleList; rp.primitive.frontFace = WGPUFrontFace_CCW;
        rp.primitive.cullMode = WGPUCullMode_None;
        rp.multisample.count = 1; rp.multisample.mask = 0xFFFFFFFF;
        rp.fragment = &fs; rp.depthStencil = &ds;
        pipe_ = wgpuDeviceCreateRenderPipeline(c->device, &rp);
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 80;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    static void pos_colour(float h, float& r, float& g, float& b) {
        r = std::clamp(1.6f*h - 0.3f, 0.f, 1.f);
        g = std::clamp(1.f - std::fabs(h-0.5f)*1.8f, 0.f, 1.f);
        b = std::clamp(1.6f*(1.f-h) - 0.3f, 0.f, 1.f);
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        ensure_depth(c);
        const float* p = c->param_values; auto pv = [&](int i, float d){ return p ? p[i] : d; };
        const int   si    = std::clamp(static_cast<int>(std::round(pv(0, shape.value))), 0, 2);
        const float base  = 0.05f + 0.3f * pv(1, size.value);
        const float spr   = pv(2, spread.value);
        const float maxAge= 0.04f + 1.4f * pv(4, trail.value);
        const float dt    = static_cast<float>(c->delta_time);

        const VividSignal* sig = vivid::elements::input_signal(c, 0);
        for (auto& L : lives_) L.age += dt;
        if (sig && sig->active) {
            for (uint32_t i = 0; i < sig->active_count; ++i) {
                const VividElement& e = sig->active[i];
                auto it = std::find_if(lives_.begin(), lives_.end(), [&](const Live& L){ return L.id == e.id; });
                if (it != lives_.end()) { it->age = 0.f; it->amp = e.amp; it->pos = e.pos; }
                else if (lives_.size() < 48) lives_.push_back({ e.id, e.pos, e.amp, 0.f });
            }
        }
        lives_.erase(std::remove_if(lives_.begin(), lives_.end(), [&](const Live& L){ return L.age > maxAge; }), lives_.end());

        insts_.clear();
        for (const auto& L : lives_) {
            const float alpha = std::clamp(1.f - L.age / maxAge, 0.f, 1.f);
            const float h = std::clamp(L.pos, 0.f, 1.f);
            const float x = (h - 0.5f) * 3.2f * spr;
            const float y = 0.3f * std::sin(L.pos * 40.f);
            float r,g,b; pos_colour(h, r, g, b);
            const float br = (0.4f + 0.6f * L.amp) * (0.4f + 0.6f * alpha);
            const float sc = base * (0.5f + L.amp) * (0.4f + 0.6f * alpha);
            insts_.push_back({ x, y, 0.f, sc, r*br, g*br, b*br });
        }

        // viewproj: perspective * a camera pulled back on +z.
        const float aspect = float(c->output_width) / std::max(1.f, float(c->output_height));
        Mat4 vp = vivid::geom::mul(vivid::geom::perspective(0.9f, aspect, 0.05f, 100.f),
                                   vivid::geom::translate(0.f, 0.f, -4.5f));
        float u[20];
        for (int i = 0; i < 16; ++i) u[i] = vp[i];
        u[16] = 0.4f + 1.6f * pv(3, spin.value); u[17] = float(c->time);
        u[18] = pv(5, wireframe.value) > 0.5f ? 1.f : 0.f; u[19] = 0.f;
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));

        const uint32_t bytes = static_cast<uint32_t>(insts_.size() * sizeof(SInst));
        if (bytes > inst_cap_) {
            if (inst_) wgpuBufferRelease(inst_);
            WGPUBufferDescriptor bd{}; bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = bytes;
            inst_ = wgpuDeviceCreateBuffer(c->device, &bd); inst_cap_ = bytes;
        }
        if (inst_ && bytes) wgpuQueueWriteBuffer(c->queue, inst_, 0, insts_.data(), bytes);

        WGPURenderPassColorAttachment att{};
        att.view = c->output_texture_view; att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        att.loadOp = WGPULoadOp_Clear; att.storeOp = WGPUStoreOp_Store;
        att.clearValue = { 0.0, 0.0, 0.0, 0.0 };   // transparent → composites cleanly
        WGPURenderPassDepthStencilAttachment datt{};
        datt.view = depth_view_; datt.depthLoadOp = WGPULoadOp_Clear; datt.depthStoreOp = WGPUStoreOp_Store; datt.depthClearValue = 1.f;
        WGPURenderPassDescriptor rpd{}; rpd.colorAttachmentCount = 1; rpd.colorAttachments = &att; rpd.depthStencilAttachment = &datt;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rpd);
        wgpuRenderPassEncoderSetPipeline(pass, pipe_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
        if (inst_ && !insts_.empty()) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf_[si], 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 1, inst_, 0, bytes);
            wgpuRenderPassEncoderDraw(pass, vcount_[si], static_cast<uint32_t>(insts_.size()), 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
};

VIVID_REGISTER(SolidsOp)
