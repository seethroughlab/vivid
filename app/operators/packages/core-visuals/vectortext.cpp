// Core visual package operator: VectorText — filled text as REAL vertex geometry
// (FreeType glyph outlines -> fan triangles, filled via the stencil even-odd rule).
// Migrated from the built-in VectorTextOp; AssetShader channel -> Param<FilePath>.
// Behaviour preserved.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "gpu/freetype_font.h"   // vivid::FtFont / FtContours / ft_string_contours (package links freetype)

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
struct GVert2 { float x, y; };
const char* kVectorTextWGSL = R"(
struct U { res: vec2f, time: f32, size: f32, posx: f32, posy: f32, pad0: f32, pad1: f32, fill: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@vertex fn vs_fan(@location(0) p: vec2f) -> @builtin(position) vec4f {
    var q = vec2f(p.x, p.y) * (u.size * 1.6);
    q.x = q.x * (u.res.y / max(u.res.x, 1.0));   // aspect-correct against the REAL target
    q = q + vec2f((u.posx - 0.5) * 2.0, (u.posy - 0.5) * 2.0);
    return vec4f(q, 0.0, 1.0);
}
@vertex fn vs_cover(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
    var v = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    return vec4f(v[vi], 0.0, 1.0);
}
@fragment fn fs_main() -> @location(0) vec4f { return u.fill; }
)";
}  // namespace

struct VectorTextOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "VectorText";
    static constexpr const char* kDisplayName = "Vector Text";
    static constexpr const char* kSummary = "Filled text as REAL vertex geometry (glyph outlines -> triangles).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "text", "geometry"};
    vivid::Param<float> size{"size", 0.2f, 0.f, 1.f}, x{"x", 0.5f, 0.f, 1.f}, y{"y", 0.5f, 0.f, 1.f};
    vivid::Param<float> r{"r", 0.98f, 0.f, 1.f}, g{"g", 0.98f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f};
    vivid::Param<float> bg_r{"bg_r", 0.05f, 0.f, 1.f}, bg_g{"bg_g", 0.06f, 0.f, 1.f}, bg_b{"bg_b", 0.1f, 0.f, 1.f};
    vivid::Param<vivid::FilePath> file{"file", ""};   // .txt whose contents are the string

    VectorTextOp() { vivid::asset_kind(file, "text"); }   // ADR-0021/P3: dialog/drop filter

    vivid::FtFont font_; bool font_tried_ = false;
    std::string loaded_path_ = "\x01", text_, baked_text_ = "\x01";
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline stencil_pipe_ = nullptr, cover_pipe_ = nullptr;
    WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer vbo_ = nullptr; uint32_t vbo_cap_ = 0, vcount_ = 0;
    WGPUTexture st_ = nullptr; WGPUTextureView stv_ = nullptr; uint32_t sw_ = 0, shh_ = 0;
    ~VectorTextOp() override {
        if (stv_) wgpuTextureViewRelease(stv_); if (st_) wgpuTextureRelease(st_);
        if (vbo_) wgpuBufferRelease(vbo_); if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (cover_pipe_) wgpuRenderPipelineRelease(cover_pipe_); if (stencil_pipe_) wgpuRenderPipelineRelease(stencil_pipe_);
        if (pl_) wgpuPipelineLayoutRelease(pl_); if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR; bg_r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&size); o.push_back(&x); o.push_back(&y);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&bg_r); o.push_back(&bg_g); o.push_back(&bg_b);
        o.push_back(&file);   // last: keeps float-param indices 0..8 stable
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    void rebuild_geometry(const VividGpuContext* ctx) {
        if (!font_.ok && !font_tried_) { font_tried_ = true; font_.load(VIVID_FONT_PATH, 256); }
        std::vector<GVert2> v;
        if (!text_.empty() && font_.ok) {
            vivid::FtContours fc = vivid::ft_string_contours(font_, text_);
            const float h = static_cast<float>(font_.ascent() + font_.descent());
            const float sc = h > 0 ? 1.f / h : 1.f;
            const float cx = fc.width * 0.5f, cy = (font_.ascent() - font_.descent()) * 0.5f;
            const vivid::FtVec2 anchor{ 0.f, 0.f };
            for (const auto& cont : fc.contours) {
                const size_t n = cont.size();
                if (n < 2) continue;
                auto nz = [&](const vivid::FtVec2& p) { return GVert2{ (p.x - cx) * sc, (p.y - cy) * sc }; };
                for (size_t i = 0; i < n; ++i) {
                    const vivid::FtVec2& a = cont[i]; const vivid::FtVec2& bb = cont[(i + 1) % n];
                    v.push_back({ anchor.x, anchor.y }); v.push_back(nz(a)); v.push_back(nz(bb));
                }
            }
        }
        vcount_ = static_cast<uint32_t>(v.size());
        const uint32_t bytes = vcount_ * sizeof(GVert2);
        if (bytes > vbo_cap_) {
            if (vbo_) wgpuBufferRelease(vbo_);
            WGPUBufferDescriptor bd{}; bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = bytes ? bytes : 16;
            vbo_ = wgpuDeviceCreateBuffer(ctx->device, &bd); vbo_cap_ = bd.size;
        }
        if (vbo_ && bytes) wgpuQueueWriteBuffer(ctx->queue, vbo_, 0, v.data(), bytes);
        baked_text_ = text_;
    }
    void ensure_stencil(const VividGpuContext* c) {
        if (st_ && sw_ == c->output_width && shh_ == c->output_height) return;
        if (stv_) wgpuTextureViewRelease(stv_); if (st_) wgpuTextureRelease(st_);
        WGPUTextureDescriptor td{}; td.size = { c->output_width, c->output_height, 1 };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_Stencil8; td.usage = WGPUTextureUsage_RenderAttachment;
        st_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTextureViewDescriptor vd{}; vd.format = WGPUTextureFormat_Stencil8; vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
        stv_ = wgpuTextureCreateView(st_, &vd);
        sw_ = c->output_width; shh_ = c->output_height;
    }
    WGPURenderPipeline make_pipe(const VividGpuContext* c, const char* vs, bool color, WGPUStencilOperation passOp,
                                 WGPUCompareFunction cmp, bool has_vbuf) {
        WGPUVertexAttribute attr{}; attr.format = WGPUVertexFormat_Float32x2; attr.offset = 0; attr.shaderLocation = 0;
        WGPUVertexBufferLayout vbl{}; vbl.arrayStride = sizeof(GVert2); vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = 1; vbl.attributes = &attr;
        WGPUBlendState blend{}; blend.color.srcFactor = WGPUBlendFactor_SrcAlpha; blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.color.operation = WGPUBlendOperation_Add; blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha; blend.alpha.operation = WGPUBlendOperation_Add;
        WGPUColorTargetState ct{}; ct.format = c->output_format; ct.writeMask = color ? WGPUColorWriteMask_All : WGPUColorWriteMask_None;
        if (color) ct.blend = &blend;
        WGPUFragmentState fs{}; fs.module = sh_; fs.entryPoint = vivid_sv("fs_main"); fs.targetCount = 1; fs.targets = &ct;
        WGPUStencilFaceState face{}; face.compare = cmp; face.failOp = WGPUStencilOperation_Keep;
        face.depthFailOp = WGPUStencilOperation_Keep; face.passOp = passOp;
        WGPUDepthStencilState ds{}; ds.format = WGPUTextureFormat_Stencil8;
        ds.depthWriteEnabled = WGPUOptionalBool_False; ds.depthCompare = WGPUCompareFunction_Always;
        ds.stencilFront = face; ds.stencilBack = face; ds.stencilReadMask = 0xFF; ds.stencilWriteMask = 0xFF;
        WGPURenderPipelineDescriptor rp{}; rp.layout = pl_;
        rp.vertex.module = sh_; rp.vertex.entryPoint = vivid_sv(vs);
        if (has_vbuf) { rp.vertex.bufferCount = 1; rp.vertex.buffers = &vbl; }
        rp.primitive.topology = WGPUPrimitiveTopology_TriangleList; rp.primitive.cullMode = WGPUCullMode_None;
        rp.multisample.count = 1; rp.multisample.mask = 0xFFFFFFFF; rp.fragment = &fs; rp.depthStencil = &ds;
        return wgpuDeviceCreateRenderPipeline(c->device, &rp);
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kVectorTextWGSL, "VectorText", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 48, "VectorText U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 48;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        stencil_pipe_ = make_pipe(c, "vs_fan", false, WGPUStencilOperation_Invert, WGPUCompareFunction_Always, true);
        cover_pipe_   = make_pipe(c, "vs_cover", true, WGPUStencilOperation_Keep, WGPUCompareFunction_NotEqual, false);
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 48;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return stencil_pipe_ && cover_pipe_;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!stencil_pipe_ || !cover_pipe_) return;
        if (file.str_value != loaded_path_) {   // reload the string from the .txt file
            loaded_path_ = file.str_value; text_.clear();
            if (!file.str_value.empty()) { std::ifstream f(file.str_value); if (f) text_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
            while (!text_.empty() && (text_.back() == '\n' || text_.back() == '\r' || text_.back() == ' ')) text_.pop_back();
        }
        if (!vbo_ || text_ != baked_text_) rebuild_geometry(c);
        ensure_stencil(c);
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        float u[12] = { float(c->output_width), float(c->output_height), float(c->time),
                        pv(0, size.value), pv(1, x.value), pv(2, y.value), 0.f, 0.f,
                        pv(3, r.value), pv(4, g.value), pv(5, b.value), 1.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        WGPURenderPassColorAttachment cat{}; cat.view = c->output_texture_view; cat.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        cat.loadOp = WGPULoadOp_Clear; cat.storeOp = WGPUStoreOp_Store;
        cat.clearValue = { pv(6, bg_r.value), pv(7, bg_g.value), pv(8, bg_b.value), 1.0 };
        WGPURenderPassDepthStencilAttachment sat{}; sat.view = stv_;
        sat.stencilLoadOp = WGPULoadOp_Clear; sat.stencilStoreOp = WGPUStoreOp_Store; sat.stencilClearValue = 0;
        WGPURenderPassDescriptor rpd{}; rpd.colorAttachmentCount = 1; rpd.colorAttachments = &cat; rpd.depthStencilAttachment = &sat;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rpd);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
        if (vbo_ && vcount_) {
            wgpuRenderPassEncoderSetPipeline(pass, stencil_pipe_);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbo_, 0, vcount_ * sizeof(GVert2));
            wgpuRenderPassEncoderDraw(pass, vcount_, 1, 0, 0);
        }
        wgpuRenderPassEncoderSetPipeline(pass, cover_pipe_);
        wgpuRenderPassEncoderSetStencilReference(pass, 0);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
};

VIVID_REGISTER(VectorTextOp)

// ADR-0021/P3: a .txt can also become vector-outlined text — offered below Text (lower priority).
static const char* const kVectorTextDropExts[] = { ".txt" };
static const VividFileDropHandlerDescriptor kVectorTextDrop[] = {
    { "VectorText", kVectorTextDropExts, 1, "file", 4, "Render as vector text" }
};
VIVID_FILE_DROP(kVectorTextDrop)
