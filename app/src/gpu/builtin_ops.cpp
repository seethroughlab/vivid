#include "gpu/builtin_ops.h"

#include "gpu/op_runtime.h"
#include "gpu/shader_op.h"
#include "gpu/effect_op.h"
#include "gpu/asset_shader.h"   // AssetShader (CustomShader data-driven .glsl)
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "gpu/freetype_font.h"   // FreeType face wrapper (hinted glyph bitmaps + outlines)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The built-in visuals operators, expressed against the lifted operator ABI.
// Each owns a GLSL ShaderOp/EffectOp (proven primitives) and renders it in
// process_gpu from the VividGpuContext. Shaders authored in GLSL — wgpu-native's
// naga translates them; WGSL operators (the other authoring path) coexist under
// the same runtime. This file is GPU-linked (only compiled into vivid::session).
namespace vivid {
namespace {

// --- shared GLSL owned by the built-in operator descriptors ---


VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}


// --- Blur: 1-input GLSL effect (1 param) ---


// --- Text: typography. Renders a string over its input. The string is read from a project
// asset (a .txt file, exactly like CustomShader reads a .glsl) so it persists + is MCP-drivable
// via set_node_asset. Baked to an R8 coverage texture with stb_truetype on change, then blitted
// positioned/scaled/tinted. 1-in/1-out overlay. ---
const char* kTextWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { res: vec2f, time: f32, size: f32, pos: vec2f, aspect: f32, pad: f32, color: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var in_tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(3) var txt_tex: texture_2d<f32>;   // R8 coverage of the baked string
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let bg = textureSample(in_tex, samp, inp.uv);
    let scr = u.res.x / max(u.res.y, 1.0);
    let h = u.size;
    let w = u.size * u.aspect / scr;                  // keep the text un-distorted on screen
    let ll = u.pos - vec2f(w, h) * 0.5;               // lower-left of the text quad (uv)
    let local = (inp.uv - ll) / vec2f(w, h);
    var a = 0.0;
    if (local.x >= 0.0 && local.x <= 1.0 && local.y >= 0.0 && local.y <= 1.0) {
        a = textureSample(txt_tex, samp, vec2f(local.x, local.y)).r;
    }
    let cov = a * u.color.a;
    return vec4f(mix(bg.rgb, u.color.rgb, cov), max(bg.a, cov));
}
)";

struct TextOp : OperatorBase, GpuProcessable, AssetShader {
    static constexpr const char* kDisplayName = "Text";
    static constexpr const char* kSummary = "Renders a string (from a .txt asset) over its input. Typography.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "text", "typography"};
    Param<float> size{"size", 0.16f, 0.f, 1.f};        // text height in the output (0..1)
    Param<float> x{"x", 0.5f, 0.f, 1.f}, y{"y", 0.5f, 0.f, 1.f};
    Param<float> r{"r", 1.f, 0.f, 1.f}, g{"g", 1.f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f}, a{"a", 1.f, 0.f, 1.f};

    // Text source (asset file) + baked font.
    std::string asset_path_, loaded_path_, text_, baked_text_;
    FtFont font_; bool font_tried_ = false;   // FreeType face: hinted glyph bitmaps + kerning
    // GPU.
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr;
    WGPUTexture txt_ = nullptr; WGPUTextureView txtv_ = nullptr; WGPUBindGroup bg_ = nullptr;
    int tw_ = 1, th_ = 1; float aspect_ = 1.f;

    void set_asset_path(const std::string& p) override { asset_path_ = p; }
    ~TextOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (txtv_) wgpuTextureViewRelease(txtv_); if (txt_) wgpuTextureRelease(txt_);
        if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&size); o.push_back(&x); o.push_back(&y);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&a);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }

    void ensure_font() {
        if (font_.ok || font_tried_) return;
        font_tried_ = true;
        font_.load(VIVID_FONT_PATH, 180);       // FreeType: raster the atlas at 180px
    }
    // Bake `text_` into a tight R8 coverage texture via FreeType (hinted + kerned). Empty -> 1x1.
    void bake(const VividGpuContext* c) {
        ensure_font();
        const std::string s = (text_.empty() || !font_.ok) ? std::string() : text_;
        FT_Face face = font_.face;
        const int base = font_.ascent() + 1;
        const int H = std::max(1, font_.ascent() + font_.descent() + 2);
        float wf = 0.f; uint32_t prev = 0;                 // measure: advances + kerning
        for (unsigned char ch : s) {
            wf += font_.kerning(prev, ch);
            if (!FT_Load_Char(face, ch, FT_LOAD_RENDER)) wf += static_cast<float>(face->glyph->advance.x >> 6);
            prev = ch;
        }
        const int W = std::max(1, static_cast<int>(wf) + 2);
        std::vector<unsigned char> bmp(static_cast<size_t>(W) * H, 0);
        float pen = 1.f; prev = 0;
        for (unsigned char ch : s) {
            pen += font_.kerning(prev, ch);
            if (FT_Load_Char(face, ch, FT_LOAD_RENDER)) { prev = ch; continue; }
            FT_GlyphSlot gsl = face->glyph;
            const int ox = static_cast<int>(pen) + gsl->bitmap_left, oy = base - gsl->bitmap_top;
            for (int row = 0; row < static_cast<int>(gsl->bitmap.rows); ++row)
                for (int col = 0; col < static_cast<int>(gsl->bitmap.width); ++col) {
                    const int ax = ox + col, ay = oy + row;
                    if (ax >= 0 && ay >= 0 && ax < W && ay < H)
                        bmp[static_cast<size_t>(ay) * W + ax] = gsl->bitmap.buffer[row * gsl->bitmap.pitch + col];
                }
            pen += static_cast<float>(gsl->advance.x >> 6);
            prev = ch;
        }
        // (Re)create the R8 texture at W x H.
        if (txtv_) { wgpuTextureViewRelease(txtv_); txtv_ = nullptr; }
        if (txt_)  { wgpuTextureRelease(txt_); txt_ = nullptr; }
        WGPUTextureDescriptor td{};
        td.size = { static_cast<uint32_t>(W), static_cast<uint32_t>(H), 1 };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_R8Unorm;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        txt_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTextureViewDescriptor vd{};
        vd.format = WGPUTextureFormat_R8Unorm; vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
        txtv_ = wgpuTextureCreateView(txt_, &vd);
        // Upload, padding rows to a 256-aligned stride (WebGPU copy requirement).
        const uint32_t stride = ((static_cast<uint32_t>(W) + 255u) / 256u) * 256u;
        std::vector<unsigned char> padded(static_cast<size_t>(stride) * H, 0);
        for (int row = 0; row < H; ++row)
            std::memcpy(padded.data() + static_cast<size_t>(row) * stride, bmp.data() + static_cast<size_t>(row) * W, W);
        WGPUTexelCopyTextureInfo dst{}; dst.texture = txt_; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay{}; lay.bytesPerRow = stride; lay.rowsPerImage = static_cast<uint32_t>(H);
        WGPUExtent3D ext = { static_cast<uint32_t>(W), static_cast<uint32_t>(H), 1 };
        wgpuQueueWriteTexture(c->queue, &dst, padded.data(), padded.size(), &lay, &ext);
        tw_ = W; th_ = H; aspect_ = static_cast<float>(W) / static_cast<float>(H);
        baked_text_ = text_;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }   // stale (referenced the old txt view)
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = gpu::create_shader_checked(c->device, kTextWGSL, "Text", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, 48, "Text U");
        WGPUBindGroupLayoutEntry e[4]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 48;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment; e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        e[3].binding = 3; e[3].visibility = WGPUShaderStage_Fragment;
        e[3].texture.sampleType = WGPUTextureSampleType_Float; e[3].texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 4; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Text Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        // Reload the text when the asset path changes (read the file's contents as the string).
        if (asset_path_ != loaded_path_) {
            loaded_path_ = asset_path_;
            text_.clear();
            if (!asset_path_.empty()) {
                std::ifstream f(asset_path_);
                if (f) { text_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
                while (!text_.empty() && (text_.back() == '\n' || text_.back() == '\r' || text_.back() == ' ')) text_.pop_back();
            }
        }
        if (!txt_ || text_ != baked_text_) bake(c);
        const float* p = c->param_values;   // size,x,y,r,g,b,a
        auto pv = [&](int i, float d) { return p ? p[i] : d; };
        float u[12] = {
            float(c->output_width), float(c->output_height), float(c->time), pv(0, size.value),
            pv(1, x.value), pv(2, y.value), aspect_, 0.f,
            pv(3, r.value), pv(4, g.value), pv(5, b.value), pv(6, a.value),
        };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        const WGPUTextureView in = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[4]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 48;
        be[1].binding = 1; be[1].textureView = in;
        be[2].binding = 2; be[2].sampler = samp_;
        be[3].binding = 3; be[3].textureView = txtv_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 4; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Text");
    }
};




// --- VectorText: REAL filled text geometry. Glyph outlines (FreeType) are flattened to fan
// triangles and filled via the stencil even-odd rule (fan-write with INVERT, then cover) — so
// holes ('O','A','e') come out right without a fragile triangulator. True vertex type: crisp at
// any scale, the basis for kinetic/extruded 3D type. Reads its string from a .txt asset. ---
struct GVert2 { float x, y; };   // a 2D position vertex (vector text fans, etc.)
const char* kVectorTextWGSL = R"(
struct U { res: vec2f, time: f32, size: f32, posx: f32, posy: f32, pad0: f32, pad1: f32, fill: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@vertex fn vs_fan(@location(0) p: vec2f) -> @builtin(position) vec4f {
    var q = vec2f(p.x, p.y) * (u.size * 1.6);             // glyph outline is y-up; NDC +y is screen-up after the Output blit
    q.x = q.x / 1.7778;                                   // 16:9 display aspect
    q = q + vec2f((u.posx - 0.5) * 2.0, (u.posy - 0.5) * 2.0);
    return vec4f(q, 0.0, 1.0);
}
@vertex fn vs_cover(@builtin(vertex_index) vi: u32) -> @builtin(position) vec4f {
    var v = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
    return vec4f(v[vi], 0.0, 1.0);
}
@fragment fn fs_main() -> @location(0) vec4f { return u.fill; }
)";
struct VectorTextOp : OperatorBase, GpuProcessable, AssetShader {
    static constexpr const char* kDisplayName = "Vector Text";
    static constexpr const char* kSummary = "Filled text as REAL vertex geometry (glyph outlines -> triangles).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "text", "geometry"};
    Param<float> size{"size", 0.2f, 0.f, 1.f}, x{"x", 0.5f, 0.f, 1.f}, y{"y", 0.5f, 0.f, 1.f};
    Param<float> r{"r", 0.98f, 0.f, 1.f}, g{"g", 0.98f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f};
    Param<float> bg_r{"bg_r", 0.05f, 0.f, 1.f}, bg_g{"bg_g", 0.06f, 0.f, 1.f}, bg_b{"bg_b", 0.1f, 0.f, 1.f};
    FtFont font_; bool font_tried_ = false;
    std::string asset_path_, loaded_path_, text_, baked_text_;
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline stencil_pipe_ = nullptr, cover_pipe_ = nullptr;
    WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer vbo_ = nullptr; uint32_t vbo_cap_ = 0, vcount_ = 0;
    WGPUTexture st_ = nullptr; WGPUTextureView stv_ = nullptr; uint32_t sw_ = 0, shh_ = 0;
    void set_asset_path(const std::string& p) override { asset_path_ = p; }
    ~VectorTextOp() override {
        if (stv_) wgpuTextureViewRelease(stv_); if (st_) wgpuTextureRelease(st_);
        if (vbo_) wgpuBufferRelease(vbo_); if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (cover_pipe_) wgpuRenderPipelineRelease(cover_pipe_); if (stencil_pipe_) wgpuRenderPipelineRelease(stencil_pipe_);
        if (pl_) wgpuPipelineLayoutRelease(pl_); if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR; bg_r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&size); o.push_back(&x); o.push_back(&y);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&bg_r); o.push_back(&bg_g); o.push_back(&bg_b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    void rebuild_geometry(const VividGpuContext* ctx) {
        if (!font_.ok && !font_tried_) { font_tried_ = true; font_.load(VIVID_FONT_PATH, 256); }
        std::vector<GVert2> v;   // reuse a simple 2-float vertex
        if (!text_.empty() && font_.ok) {
            FtContours fc = ft_string_contours(font_, text_);
            const float h = static_cast<float>(font_.ascent() + font_.descent());
            const float sc = h > 0 ? 1.f / h : 1.f;
            const float cx = fc.width * 0.5f, cy = (font_.ascent() - font_.descent()) * 0.5f;
            const FtVec2 anchor{ 0.f, 0.f };                 // fixed fan point (even-odd via stencil INVERT)
            for (const auto& cont : fc.contours) {
                const size_t n = cont.size();
                if (n < 2) continue;
                auto nz = [&](const FtVec2& p) { return GVert2{ (p.x - cx) * sc, (p.y - cy) * sc }; };
                for (size_t i = 0; i < n; ++i) {
                    const FtVec2& a = cont[i]; const FtVec2& bb = cont[(i + 1) % n];
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
        std::string err; sh_ = gpu::create_shader_checked(c->device, kVectorTextWGSL, "VectorText", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, 48, "VectorText U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 48;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        stencil_pipe_ = make_pipe(c, "vs_fan", /*color*/false, WGPUStencilOperation_Invert, WGPUCompareFunction_Always, /*vbuf*/true);
        cover_pipe_   = make_pipe(c, "vs_cover", /*color*/true, WGPUStencilOperation_Keep, WGPUCompareFunction_NotEqual, /*vbuf*/false);
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 48;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return stencil_pipe_ && cover_pipe_;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!stencil_pipe_ || !cover_pipe_) return;
        if (asset_path_ != loaded_path_) {                   // reload the string from the .txt asset
            loaded_path_ = asset_path_; text_.clear();
            if (!asset_path_.empty()) { std::ifstream f(asset_path_); if (f) text_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); }
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
        if (vbo_ && vcount_) {   // pass 1: write the even-odd coverage into stencil (no colour)
            wgpuRenderPassEncoderSetPipeline(pass, stencil_pipe_);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbo_, 0, vcount_ * sizeof(GVert2));
            wgpuRenderPassEncoderDraw(pass, vcount_, 1, 0, 0);
        }
        wgpuRenderPassEncoderSetPipeline(pass, cover_pipe_);   // pass 2: fill colour where stencil != 0
        wgpuRenderPassEncoderSetStencilReference(pass, 0);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }
};




}  // namespace

void register_builtin_ops(OpRegistry& reg) {
    register_op<TextOp>    (reg, "Text");           // typography (string from a .txt asset)
    register_op<VectorTextOp>(reg, "VectorText");   // REAL filled text geometry (FreeType outlines)
}

}  // namespace vivid
