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
const char* kPlasmaGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U {
    vec2 u_res; float u_time; float u_warp; float u_hue; float u_density; float u_glow; };
void main() {
    vec2 uv = v_uv;
    float t = u_time;
    float dens = 6.0 + u_density * 18.0;
    vec2 w = uv + u_warp * 0.3 * vec2(sin(uv.y * 8.0 + t), cos(uv.x * 8.0 + t));
    float v = sin(w.x * dens + t) + sin(w.y * dens + t * 1.3)
            + sin((w.x + w.y) * dens * 0.6 + t * 0.7)
            + sin(length(w - 0.5) * dens * 1.8 - t * 2.0);
    vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + v + u_hue * 6.2832);
    o_color = vec4(col * (0.6 + u_glow), 1.0);
}
)";

const char* kFeedbackGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float u_decay; float p1; float p2; float p3; };
layout(set = 0, binding = 1) uniform texture2D u_gen;
layout(set = 0, binding = 2) uniform sampler   u_samp;
layout(set = 0, binding = 3) uniform texture2D u_prev;
void main() {
    vec2 c = v_uv - 0.5;
    vec2 puv = 0.5 + c * 0.985;
    vec4 gen  = texture(sampler2D(u_gen,  u_samp), v_uv);
    vec4 prev = texture(sampler2D(u_prev, u_samp), puv);
    o_color = max(gen, prev * u_decay);
}
)";

const char* kBlurGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float u_radius; float p1; float p2; float p3; };
layout(set = 0, binding = 1) uniform texture2D u_tex;
layout(set = 0, binding = 2) uniform sampler   u_samp;
void main() {
    vec2 px = (1.0 / u_res) * (1.0 + u_radius * 8.0);
    vec4 s = texture(sampler2D(u_tex, u_samp), v_uv) * 0.36;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2( px.x, 0.0)) * 0.16;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2(-px.x, 0.0)) * 0.16;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2(0.0,  px.y)) * 0.16;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2(0.0, -px.y)) * 0.16;
    o_color = s;
}
)";

const char* kBlitGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float p0; float p1; float p2; float p3; };
layout(set = 0, binding = 1) uniform texture2D u_tex;
layout(set = 0, binding = 2) uniform sampler   u_samp;
void main() { o_color = texture(sampler2D(u_tex, u_samp), v_uv); }
)";

VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

// --- Plasma: GLSL generator (4 params, 1 texture out) ---
struct PlasmaOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Plasma";
    static constexpr const char* kSummary = "Animated plasma colour-field generator (GLSL). No input; drives a chain.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "plasma", "color"};
    Param<float> warp   {"warp",    0.5f, 0.f, 1.f};
    Param<float> hue    {"hue",     0.0f, 0.f, 1.f};
    Param<float> density{"density", 0.5f, 0.f, 1.f};
    Param<float> glow   {"glow",    0.5f, 0.f, 1.f};
    ShaderOp shader_; bool tried_ = false;
    void collect_params(std::vector<ParamBase*>& o) override {
        // UI-4a: warp (x) + hue (y) form an XY-pad group — drag distortion × color together. The
        // pad claims the next param (hue), so these two must stay adjacent in this list.
        semantic_intent(warp, "domain warp amount");   warp.display_hint = VIVID_DISPLAY_XY_PAD;
        semantic_tag(hue, "phase_01"); semantic_intent(hue, "color hue"); hue.display_hint = VIVID_DISPLAY_KNOB;
        semantic_intent(density, "pattern density");    density.display_hint = VIVID_DISPLAY_KNOB;
        semantic_intent(glow, "glow intensity");        glow.display_hint = VIVID_DISPLAY_KNOB;
        o.push_back(&warp); o.push_back(&hue); o.push_back(&density); o.push_back(&glow);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; shader_.init(c->device, c->queue, c->output_format, kPlasmaGLSL); }
        if (!shader_.ok()) return;
        shader_.render(c->command_encoder, c->output_texture_view, 0.f, 0.f,
                       float(c->output_width), float(c->output_height), float(c->time),
                       c->param_values, /*clear*/true);
    }
};

// --- A 1-input GLSL blit (Video source-in / Output sink): passes input -> output ---
struct BlitOp : OperatorBase, GpuProcessable {
    EffectOp fx_; bool tried_ = false;
    void collect_params(std::vector<ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; fx_.init(c->device, c->queue, c->output_format, kBlitGLSL, 1); }
        if (!fx_.ok() || c->input_texture_count < 1) return;
        const WGPUTextureView in = c->input_texture_views[0];
        fx_.render(c->command_encoder, c->output_texture_view, 0.f, 0.f,
                   float(c->output_width), float(c->output_height), /*clear*/true,
                   &in, 1, float(c->time), nullptr, 0);
    }
};
// generator fed by the app's external source texture
struct VideoOp  : BlitOp {
    static constexpr const char* kDisplayName = "Video";
    static constexpr const char* kSummary = "Plays the shared image/video source texture into the chain.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "video", "source"};
};
// sink / passthrough to the viewer
struct OutputOp : BlitOp {
    static constexpr const char* kDisplayName = "Output";
    static constexpr const char* kSummary = "Chain sink: feeds the connected texture to the on-screen viewer.";
    static constexpr std::array<const char*, 3> kKeywords = {"output", "viewer", "sink"};
};

// --- Blur: 1-input GLSL effect (1 param) ---
struct BlurOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Blur";
    static constexpr const char* kSummary = "Box blur of the input texture; radius is wire-drivable.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "blur", "soften"};
    Param<float> radius{"radius", 0.3f, 0.f, 1.f};
    EffectOp fx_; bool tried_ = false;
    void collect_params(std::vector<ParamBase*>& o) override { o.push_back(&radius); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; fx_.init(c->device, c->queue, c->output_format, kBlurGLSL, 1); }
        if (!fx_.ok() || c->input_texture_count < 1) return;
        const WGPUTextureView in = c->input_texture_views[0];
        const float r = c->param_values ? c->param_values[0] : radius.value;
        fx_.render(c->command_encoder, c->output_texture_view, 0.f, 0.f,
                   float(c->output_width), float(c->output_height), /*clear*/true,
                   &in, 1, float(c->time), &r, 1);
    }
};

// --- Feedback: 2-input GLSL effect (gen + own prev-frame history texture) ---
struct FeedbackOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Feedback";
    static constexpr const char* kSummary = "Frame feedback / trails: blends the input with a decaying history texture.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "feedback", "trails"};
    Param<float> decay{"decay", 0.5f, 0.f, 1.f};
    EffectOp fx_; bool tried_ = false;
    WGPUTexture hist_ = nullptr; WGPUTextureView hist_view_ = nullptr;
    uint32_t hw_ = 0, hh_ = 0;
    ~FeedbackOp() override { if (hist_view_) wgpuTextureViewRelease(hist_view_); if (hist_) wgpuTextureRelease(hist_); }
    void collect_params(std::vector<ParamBase*>& o) override { o.push_back(&decay); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    void ensure_hist(const VividGpuContext* c) {
        if (hist_ && hw_ == c->output_width && hh_ == c->output_height) return;
        if (hist_view_) wgpuTextureViewRelease(hist_view_);
        if (hist_) wgpuTextureRelease(hist_);
        WGPUTextureDescriptor td{};
        td.size = { c->output_width, c->output_height, 1 };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D;
        td.format = c->output_format;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        hist_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTextureViewDescriptor vd{};
        vd.format = c->output_format; vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
        hist_view_ = wgpuTextureCreateView(hist_, &vd);
        hw_ = c->output_width; hh_ = c->output_height;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; fx_.init(c->device, c->queue, c->output_format, kFeedbackGLSL, 2); }
        if (!fx_.ok() || c->input_texture_count < 1) return;
        ensure_hist(c);
        const float d = 0.82f + (c->param_values ? c->param_values[0] : decay.value) * 0.16f;  // 0.82..0.98
        const WGPUTextureView ins[2] = { c->input_texture_views[0], hist_view_ };
        fx_.render(c->command_encoder, c->output_texture_view, 0.f, 0.f,
                   float(c->output_width), float(c->output_height), /*clear*/true,
                   ins, 2, float(c->time), &d, 1);
        // Copy output -> history for next frame.
        WGPUTexelCopyTextureInfo src{}; src.texture = c->output_texture;
        WGPUTexelCopyTextureInfo dst{}; dst.texture = hist_;
        WGPUExtent3D ext = { c->output_width, c->output_height, 1 };
        wgpuCommandEncoderCopyTextureToTexture(c->command_encoder, &src, &dst, &ext);
    }
};


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


// --- Lines: real LINE geometry (LineList) — a technical grid / radial burst / concentric rings.
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
struct LinesOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Lines";
    static constexpr const char* kSummary = "Real line geometry: a grid / radial burst / concentric rings (wireframe).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "lines", "wireframe"};
    Param<float> mode{"mode", 0.f, 0.f, 1.f};           // 0 grid, ~.4 radial, ~.8 rings
    Param<float> count{"count", 0.5f, 0.f, 1.f}, sides{"sides", 0.6f, 0.f, 1.f};
    Param<float> size{"size", 0.75f, 0.f, 1.f}, rotation{"rotation", 0.f, 0.f, 1.f};
    Param<float> r{"r", 0.3f, 0.f, 1.f}, g{"g", 1.f, 0.f, 1.f}, b{"b", 0.85f, 0.f, 1.f};
    Param<float> bg_r{"bg_r", 0.03f, 0.f, 1.f}, bg_g{"bg_g", 0.04f, 0.f, 1.f}, bg_b{"bg_b", 0.07f, 0.f, 1.f};
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
    void collect_params(std::vector<ParamBase*>& o) override {
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
        std::string err; sh_ = gpu::create_shader_checked(c->device, kLinesWGSL, "Lines", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, 48, "Lines U");
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

// --- Mesh: REAL 3D vertex geometry. Platonic solids (cube / tetra / octa / icosa) drawn from an
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
struct MeshOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Mesh";
    static constexpr const char* kSummary = "Real 3D geometry: spinning platonic solids (solid flat-lit or wireframe).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "geometry", "3d"};
    Param<float> shape{"shape", 0.f, 0.f, 1.f};             // -> cube/tetra/octa/icosa
    Param<float> wireframe{"wireframe", 0.f, 0.f, 1.f};     // <.5 solid, >=.5 wireframe
    Param<float> size{"size", 0.6f, 0.f, 1.f};
    Param<float> spin{"spin", 0.35f, 0.f, 1.f}, tilt{"tilt", 0.5f, 0.f, 1.f};
    Param<float> r{"r", 0.9f, 0.f, 1.f}, g{"g", 0.92f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f};
    Param<float> bg_r{"bg_r", 0.03f, 0.f, 1.f}, bg_g{"bg_g", 0.03f, 0.f, 1.f}, bg_b{"bg_b", 0.05f, 0.f, 1.f};
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
    void collect_params(std::vector<ParamBase*>& o) override {
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
        std::string err; sh_ = gpu::create_shader_checked(c->device, kMeshWGSL, "Mesh", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, 160, "Mesh U");
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
        Mat4 proj = perspective(0.7854f, 1.7778f, 0.1f, 100.f);   // 45deg fov, 16:9 display aspect (gotcha)
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

// --- CustomShader: data-driven GLSL generator loaded from a project .glsl ---
// Mirrors PlasmaOp but the fragment source comes from a file (the node's `asset`,
// resolved to an absolute path by VisualGraph and pushed via AssetShader) instead of
// a compile-time literal. The .glsl must follow the ShaderOp contract (v_uv/o_color +
// the u_res/u_time/u_warp/u_hue/u_density/u_glow uniform block). Missing file or a
// compile error degrades to a no-op node (logged once) — it never crashes the app.
struct CustomShaderOp : OperatorBase, GpuProcessable, AssetShader {
    static constexpr const char* kDisplayName = "Custom Shader";
    static constexpr const char* kSummary = "Data-driven GLSL generator: renders a project .glsl file (set its `asset`).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "shader", "glsl"};
    // Four generic float knobs -> the shader's u_warp/u_hue/u_density/u_glow uniforms.
    Param<float> p0{"warp",    0.5f, 0.f, 1.f};
    Param<float> p1{"hue",     0.0f, 0.f, 1.f};
    Param<float> p2{"density", 0.5f, 0.f, 1.f};
    Param<float> p3{"glow",    0.5f, 0.f, 1.f};
    ShaderOp shader_;
    std::string want_;      // absolute asset path requested (set_asset_path)
    std::string loaded_;    // asset path currently compiled
    bool        failed_ = false;   // last (re)load failed -> render nothing, don't retry

    void collect_params(std::vector<ParamBase*>& o) override { o.push_back(&p0); o.push_back(&p1); o.push_back(&p2); o.push_back(&p3); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    void set_asset_path(const std::string& abs) override { want_ = abs; }   // called before process_gpu

    void reload(const VividGpuContext* c) {
        shader_.shutdown();
        loaded_ = want_; failed_ = true;   // pessimistic until it compiles
        if (want_.empty()) return;
        std::ifstream f(want_, std::ios::binary);
        if (!f) { std::fprintf(stderr, "[CustomShader] cannot open %s\n", want_.c_str()); return; }
        std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (src.empty()) { std::fprintf(stderr, "[CustomShader] empty shader %s\n", want_.c_str()); return; }
        shader_.init(c->device, c->queue, c->output_format, src.c_str());
        failed_ = !shader_.ok();
        if (failed_) std::fprintf(stderr, "[CustomShader] compile failed: %s\n", want_.c_str());
    }
    void process_gpu(const VividGpuContext* c) override {
        if (want_ != loaded_) reload(c);       // (re)load on first use or asset change
        if (failed_ || !shader_.ok()) return;  // degrade gracefully
        shader_.render(c->command_encoder, c->output_texture_view, 0.f, 0.f,
                       float(c->output_width), float(c->output_height), float(c->time),
                       c->param_values, /*clear*/true);
    }
};

}  // namespace

void register_builtin_ops(OpRegistry& reg) {
    register_op<PlasmaOp>  (reg, "Plasma");
    register_op<VideoOp>   (reg, "Video");
    register_op<FeedbackOp>(reg, "Feedback");
    register_op<BlurOp>    (reg, "Blur");
    register_op<OutputOp>  (reg, "Output");
    register_op<TextOp>    (reg, "Text");           // typography (string from a .txt asset)
    register_op<LinesOp>   (reg, "Lines");          // REAL line geometry: grid / radial / rings
    register_op<VectorTextOp>(reg, "VectorText");   // REAL filled text geometry (FreeType outlines)
    register_op<MeshOp>    (reg, "Mesh");           // REAL 3D geometry: spinning platonic solids (+depth)
    register_op<CustomShaderOp>(reg, "CustomShader");  // data-driven .glsl generator
}

}  // namespace vivid
