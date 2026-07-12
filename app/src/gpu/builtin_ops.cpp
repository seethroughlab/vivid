#include "gpu/builtin_ops.h"

#include "gpu/op_runtime.h"
#include "gpu/shader_op.h"
#include "gpu/effect_op.h"
#include "gpu/asset_shader.h"   // AssetShader (CustomShader data-driven .glsl)
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "stb_truetype.h"       // declarations only; the impl (STB_TRUETYPE_IMPLEMENTATION) is in renderer_2d.cpp

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
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

// --- Tint: a generator authored in WGSL (proves both shader paths coexist under
// one runtime — GLSL ops above + this WGSL op, both naga-lowered). Uses the lifted
// gpu_common helpers (fullscreen vs_main/fs_main + a uniform). ---
const char* kTintWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { res: vec2f, time: f32, hue: f32, tint: vec3f };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let c = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + inp.uv.x * 6.2831853 + u.time * 0.5 + u.hue * 6.2831853);
    return vec4f(c * u.tint, 1.0);   // UI-4a: r/g/b tint (COLOR compound-widget demo)
}
)";

struct TintOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Tint";
    static constexpr const char* kSummary = "WGSL example generator: a hue-shifted gradient (shows the WGSL authoring path).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "tint", "wgsl"};
    Param<float> hue{"hue", 0.5f, 0.f, 1.f};
    Param<float> r{"r", 1.f, 0.f, 1.f}, g{"g", 1.f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f};   // UI-4a COLOR group
    bool tried_ = false;
    WGPUShaderModule    sh_   = nullptr;
    WGPUBindGroupLayout bgl_  = nullptr;
    WGPUPipelineLayout  pl_   = nullptr;
    WGPURenderPipeline  pipe_ = nullptr;
    WGPUBuffer          ubo_  = nullptr;
    WGPUBindGroup       bg_   = nullptr;
    ~TintOp() override {
        if (bg_)   wgpuBindGroupRelease(bg_);
        if (ubo_)  wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_);
        if (pl_)   wgpuPipelineLayoutRelease(pl_);
        if (bgl_)  wgpuBindGroupLayoutRelease(bgl_);
        if (sh_)   wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR;   // r starts an r/g/b COLOR group (swatch + channels)
        o.push_back(&hue); o.push_back(&r); o.push_back(&g); o.push_back(&b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = gpu::create_shader_checked(c->device, kTintWGSL, "Tint", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, 32, "Tint U");
        WGPUBindGroupLayoutEntry e{};
        e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 32;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Tint Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 32;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values;   // order: hue, r, g, b
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time),
                       p ? p[0] : hue.value, 0.f, 0.f, 0.f, 0.f };
        u[4] = p ? p[1] : r.value;   // tint.r  (vec3 aligns at offset 16 = u[4])
        u[5] = p ? p[2] : g.value;   // tint.g
        u[6] = p ? p[3] : b.value;   // tint.b
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Tint");
    }
};

// --- Shape: a crisp SDF primitive (circle / polygon) composited OVER its input.
// A 1-in/1-out overlay so `Plasma -> Shape -> Output` draws geometry on the field (input
// optional: unconnected = shape on transparent). WGSL pipeline (needs 10 params; EffectOp
// only carries 4). SDF math lifted from vivid-classic's Shape2D/Render2D sd_shape. ---
const char* kShapeWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U {
    res: vec2f, time: f32, sides: f32,       // 0,4 | 8 | 12
    pos: vec2f, size: f32, rotation: f32,    // 16 | 24 | 28
    softness: f32, pad0: f32, pad1: vec2f,   // 32 | 36 | 40 (pad -> 48)
    color: vec4f,                            // 48..63
};
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var in_tex: texture_2d<f32>;
@group(0) @binding(2) var in_samp: sampler;

// Regular n-gon SDF (iq); sides < 2.5 => circle. Negative inside, 0 on the edge.
fn sd_shape(pin: vec2f, r: f32, sides: f32) -> f32 {
    if (sides < 2.5) { return length(pin) - r; }
    let n = floor(sides + 0.5);
    let an = 3.14159265 / n;
    let acs = vec2f(cos(an), sin(an));
    var bn = atan2(pin.x, pin.y);
    bn = bn - 2.0 * an * floor((bn + an) / (2.0 * an));
    var p = length(pin) * vec2f(cos(bn), abs(sin(bn)));
    p = p - r * acs;
    p.y = p.y + clamp(-p.y, 0.0, r * acs.y);
    return length(p) * sign(p.x);
}
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let bg = textureSample(in_tex, in_samp, inp.uv);
    let sides = floor(u.sides * 8.0);                     // 0..1 param -> 0..8 sides
    var p = inp.uv - u.pos;                               // u.pos in 0..1 (0.5 = centered)
    p.x = p.x * (u.res.x / max(u.res.y, 1.0));            // aspect-correct to round
    let a = u.rotation * 6.2831853;
    p = vec2f(p.x * cos(a) - p.y * sin(a), p.x * sin(a) + p.y * cos(a));
    let d = sd_shape(p, max(u.size * 0.7, 0.001), sides);
    let aa = fwidth(d) + u.softness * 0.06 + 0.0015;
    let cov = (1.0 - smoothstep(-aa, aa, d)) * u.color.a;
    return vec4f(mix(bg.rgb, u.color.rgb, cov), max(bg.a, cov));
}
)";

struct ShapeOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Shape";
    static constexpr const char* kSummary = "A crisp SDF shape (circle/polygon) drawn over its input. Geometry, not a field.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "shape", "geometry"};
    // Visual node params are normalized 0..1 (clamped by the graph); ranges are remapped in-shader.
    Param<float> sides{"sides", 0.75f, 0.f, 1.f};        // -> floor(*8): 0=circle .. 8=octagon; 0.75=hex
    Param<float> x{"x", 0.5f, 0.f, 1.f}, y{"y", 0.5f, 0.f, 1.f};   // position (0.5 = centered)
    Param<float> size{"size", 0.35f, 0.f, 1.f};
    Param<float> rotation{"rotation", 0.f, 0.f, 1.f};
    Param<float> softness{"softness", 0.04f, 0.f, 1.f};
    Param<float> r{"r", 1.f, 0.f, 1.f}, g{"g", 0.2f, 0.f, 1.f}, b{"b", 0.6f, 0.f, 1.f}, a{"a", 1.f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr;
    WGPUBindGroup bg_ = nullptr;
    ~ShapeOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR;               // r/g/b/a a colour swatch
        o.push_back(&sides); o.push_back(&x); o.push_back(&y); o.push_back(&size);
        o.push_back(&rotation); o.push_back(&softness);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&a);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = gpu::create_shader_checked(c->device, kShapeWGSL, "Shape", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, 64, "Shape U");
        WGPUBindGroupLayoutEntry e[3]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 64;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment;
        e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 3; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Shape Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values;   // sides,x,y,size,rot,softness,r,g,b,a
        auto pv = [&](int i, float d) { return p ? p[i] : d; };
        float u[16] = {
            float(c->output_width), float(c->output_height), float(c->time), pv(0, sides.value),
            pv(1, x.value), pv(2, y.value), pv(3, size.value), pv(4, rotation.value),
            pv(5, softness.value), 0.f, 0.f, 0.f,
            pv(6, r.value), pv(7, g.value), pv(8, b.value), pv(9, a.value),
        };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        // Rebuild the bind group each frame with the current input view (or the shared black
        // fallback when unconnected — the executor always provides a view).
        const WGPUTextureView in = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[3]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 64;
        be[1].binding = 1; be[1].textureView = in;
        be[2].binding = 2; be[2].sampler = samp_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 3; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Shape");
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
    std::vector<unsigned char> font_data_;
    stbtt_fontinfo font_{}; bool font_ok_ = false, font_tried_ = false;
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
        if (font_tried_) return;
        font_tried_ = true;
        std::ifstream f(VIVID_FONT_PATH, std::ios::binary);
        if (!f) return;
        font_data_.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        font_ok_ = !font_data_.empty() && stbtt_InitFont(&font_, font_data_.data(), 0);
    }
    // Bake `text_` into a tight R8 coverage texture. No-op string -> a 1x1 transparent texture.
    void bake(const VividGpuContext* c) {
        ensure_font();
        const std::string s = text_.empty() || !font_ok_ ? std::string() : text_;
        const float rh = 180.f;
        float sc = font_ok_ ? stbtt_ScaleForPixelHeight(&font_, rh) : 0.f;
        int asc = 0, desc = 0, lg = 0;
        if (font_ok_) stbtt_GetFontVMetrics(&font_, &asc, &desc, &lg);
        const int base = static_cast<int>(asc * sc);
        const int H = std::max(1, static_cast<int>((asc - desc) * sc) + 2);
        float wf = 0.f;
        for (unsigned char ch : s) { int adv = 0, lsb = 0; stbtt_GetCodepointHMetrics(&font_, ch, &adv, &lsb); wf += adv * sc; }
        const int W = std::max(1, static_cast<int>(wf) + 2);
        std::vector<unsigned char> bmp(static_cast<size_t>(W) * H, 0);
        float pen = 1.f;
        for (unsigned char ch : s) {
            int x0, y0, x1, y1; stbtt_GetCodepointBitmapBox(&font_, ch, sc, sc, &x0, &y0, &x1, &y1);
            const int gw = x1 - x0, gh = y1 - y0;
            const int ox = static_cast<int>(pen) + x0, oy = base + y0;
            if (gw > 0 && gh > 0 && ox >= 0 && oy >= 0 && ox + gw <= W && oy + gh <= H)
                stbtt_MakeCodepointBitmap(&font_, bmp.data() + static_cast<size_t>(oy) * W + ox, gw, gh, W, sc, sc, ch);
            int adv = 0, lsb = 0; stbtt_GetCodepointHMetrics(&font_, ch, &adv, &lsb); pen += adv * sc;
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

// --- Gradient: a clean 2-colour linear/radial gradient GENERATOR (0-in). A flat, non-noisy
// source to composite shapes/text over, or to tint a chain. (Lifted from classic gradient.wgsl.) ---
const char* kGradientWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, mode: f32, center: vec2f, angle: f32, scale: f32, colA: vec4f, colB: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    var t: f32;
    if (u.mode < 0.5) {                                  // linear
        let a = u.angle * 6.2831853;
        t = dot(inp.uv - u.center, vec2f(cos(a), sin(a))) * (0.5 + u.scale * 2.0) + 0.5;
    } else {                                             // radial
        let d = (inp.uv - u.center) * vec2f(u.res.x / max(u.res.y, 1.0), 1.0);
        t = length(d) * (0.7 + u.scale * 3.0);
    }
    return vec4f(mix(u.colA.rgb, u.colB.rgb, clamp(t, 0.0, 1.0)), 1.0);
}
)";
struct GradientOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Gradient";
    static constexpr const char* kSummary = "A clean 2-colour linear/radial gradient (a flat source, not a field).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "gradient", "color"};
    Param<float> mode{"mode", 0.f, 0.f, 1.f};                 // <0.5 linear, else radial
    Param<float> cx{"cx", 0.5f, 0.f, 1.f}, cy{"cy", 0.5f, 0.f, 1.f};
    Param<float> angle{"angle", 0.f, 0.f, 1.f}, scale{"scale", 0.5f, 0.f, 1.f};
    Param<float> ar{"ar", 0.1f, 0.f, 1.f}, ag{"ag", 0.1f, 0.f, 1.f}, ab{"ab", 0.35f, 0.f, 1.f};
    Param<float> br{"br", 0.9f, 0.f, 1.f}, bg{"bg", 0.2f, 0.f, 1.f}, bb{"bb", 0.6f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~GradientOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<ParamBase*>& o) override {
        ar.display_hint = VIVID_DISPLAY_COLOR; br.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&mode); o.push_back(&cx); o.push_back(&cy); o.push_back(&angle); o.push_back(&scale);
        o.push_back(&ar); o.push_back(&ag); o.push_back(&ab); o.push_back(&br); o.push_back(&bg); o.push_back(&bb);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = gpu::create_shader_checked(c->device, kGradientWGSL, "Gradient", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, 64, "Gradient U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 64;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Gradient Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 64;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        float u[16] = { float(c->output_width), float(c->output_height), float(c->time), pv(0, mode.value),
                        pv(1, cx.value), pv(2, cy.value), pv(3, angle.value), pv(4, scale.value),
                        pv(5, ar.value), pv(6, ag.value), pv(7, ab.value), 1.f,
                        pv(8, br.value), pv(9, bg.value), pv(10, bb.value), 1.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Gradient");
    }
};

// --- Transform: a 1-in/1-out UV warp (zoom / rotate / translate / tile). Near-identity at its
// defaults so inserting it is safe. (Lifted from classic transform.wgsl.) ---
const char* kTransformWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, tx: f32, ty: f32, rot: f32, scale: f32, tile: f32 };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var in_tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    var p = inp.uv - vec2f(0.5, 0.5);
    let zoom = 0.25 + u.scale * 3.75;                    // scale 0..1 -> 0.25..4x  (~0.2 = 1x)
    p = p / zoom;
    let a = u.rot * 6.2831853;
    p = vec2f(p.x * cos(a) - p.y * sin(a), p.x * sin(a) + p.y * cos(a));
    p = p - (vec2f(u.tx, u.ty) - vec2f(0.5, 0.5));
    var uv2 = p + vec2f(0.5, 0.5);
    let tiles = 1.0 + floor(u.tile * 8.0);               // tile 0 -> 1 (no repeat)
    uv2 = fract(uv2 * tiles);
    return textureSample(in_tex, samp, uv2);
}
)";
// --- Kaleidoscope: a 1-in/1-out radial-symmetry fold (mirror wedges). Classic mirror.wgsl. ---
const char* kKaleidoWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, segments: f32, cx: f32, cy: f32, angle: f32, zoom: f32 };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var in_tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let ar = u.res.x / max(u.res.y, 1.0);
    var p = (inp.uv - vec2f(u.cx, u.cy)) * vec2f(ar, 1.0);
    let seg = 2.0 + floor(u.segments * 14.0);            // 2..16 wedges
    let wedge = 6.2831853 / seg;
    var ang = atan2(p.y, p.x) + u.angle * 6.2831853;
    ang = ang - wedge * floor(ang / wedge);
    ang = abs(ang - wedge * 0.5);                        // mirror within the wedge
    let r = length(p) * (0.6 + u.zoom * 0.8);
    var uv2 = vec2f(cos(ang), sin(ang)) * r / vec2f(ar, 1.0) + vec2f(u.cx, u.cy);
    uv2 = clamp(uv2, vec2f(0.0), vec2f(1.0));
    return textureSample(in_tex, samp, uv2);
}
)";
// Shared body for a 1-in/1-out WGSL UV filter with a small uniform (Transform / Kaleidoscope).
template <int NPARAM, int UBYTES>
struct UvFilterOp : OperatorBase, GpuProcessable {
    const char* wgsl_; const char* label_;
    UvFilterOp(const char* wgsl, const char* label) : wgsl_(wgsl), label_(label) {}
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~UvFilterOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT)); o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = gpu::create_shader_checked(c->device, wgsl_, label_, err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, UBYTES, label_);
        WGPUBindGroupLayoutEntry e[3]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = UBYTES;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment; e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 3; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = gpu::create_pipeline(c->device, sh_, pl_, c->output_format, label_);
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }
    // Subclass fills u[] (res/time already in [0],[1],[2]).
    virtual void fill(const VividGpuContext* c, const float* p, float* u) = 0;
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        float u[UBYTES / 4]{}; u[0] = float(c->output_width); u[1] = float(c->output_height); u[2] = float(c->time);
        fill(c, c->param_values, u);
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, UBYTES);
        const WGPUTextureView in = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[3]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = UBYTES;
        be[1].binding = 1; be[1].textureView = in; be[2].binding = 2; be[2].sampler = samp_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 3; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, label_);
    }
};
struct TransformOp : UvFilterOp<5, 32> {
    static constexpr const char* kDisplayName = "Transform";
    static constexpr const char* kSummary = "Zoom / rotate / translate / tile the input (near-identity at defaults).";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "transform", "tile"};
    Param<float> tx{"tx", 0.5f, 0.f, 1.f}, ty{"ty", 0.5f, 0.f, 1.f}, rot{"rot", 0.f, 0.f, 1.f};
    Param<float> scale{"scale", 0.2f, 0.f, 1.f}, tile{"tile", 0.f, 0.f, 1.f};
    TransformOp() : UvFilterOp(kTransformWGSL, "Transform") {}
    void collect_params(std::vector<ParamBase*>& o) override {
        o.push_back(&tx); o.push_back(&ty); o.push_back(&rot); o.push_back(&scale); o.push_back(&tile);
    }
    void fill(const VividGpuContext*, const float* p, float* u) override {
        auto pv = [&](int i, float d) { return p ? p[i] : d; };
        u[3] = pv(0, tx.value); u[4] = pv(1, ty.value); u[5] = pv(2, rot.value); u[6] = pv(3, scale.value); u[7] = pv(4, tile.value);
    }
};
struct KaleidoscopeOp : UvFilterOp<5, 32> {
    static constexpr const char* kDisplayName = "Kaleidoscope";
    static constexpr const char* kSummary = "Radial mirror-symmetry (kaleidoscope) fold of the input.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "kaleidoscope", "mirror"};
    Param<float> segments{"segments", 0.3f, 0.f, 1.f}, cx{"cx", 0.5f, 0.f, 1.f}, cy{"cy", 0.5f, 0.f, 1.f};
    Param<float> angle{"angle", 0.f, 0.f, 1.f}, zoom{"zoom", 0.5f, 0.f, 1.f};
    KaleidoscopeOp() : UvFilterOp(kKaleidoWGSL, "Kaleidoscope") {}
    void collect_params(std::vector<ParamBase*>& o) override {
        o.push_back(&segments); o.push_back(&cx); o.push_back(&cy); o.push_back(&angle); o.push_back(&zoom);
    }
    void fill(const VividGpuContext*, const float* p, float* u) override {
        auto pv = [&](int i, float d) { return p ? p[i] : d; };
        u[3] = pv(0, segments.value); u[4] = pv(1, cx.value); u[5] = pv(2, cy.value); u[6] = pv(3, angle.value); u[7] = pv(4, zoom.value);
    }
};

// --- Composite: blend TWO inputs (A = base, B = over) with normal/add/multiply/screen/overlay.
// The one 2-input op — the executor gives it both texture inputs (port A = input, port B = input_b).
// Blend math lifted from vivid-classic composite.cpp. ---
const char* kCompositeWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, mode: f32, opacity: f32, pad0: f32, pad1: vec2f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var tex_a: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(3) var tex_b: texture_2d<f32>;
fn blend_overlay(a: vec3f, b: vec3f) -> vec3f {
    return select(2.0 * a * b, 1.0 - 2.0 * (1.0 - a) * (1.0 - b), a > vec3f(0.5));
}
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let a = textureSample(tex_a, samp, inp.uv);
    let b = textureSample(tex_b, samp, inp.uv);
    let o = clamp(u.opacity, 0.0, 1.0);
    let m = u.mode * 4.0;                                 // 0..1 param -> 5 modes
    var c: vec3f;
    if      (m < 0.5) { c = mix(a.rgb, b.rgb, o); }                                    // normal
    else if (m < 1.5) { c = a.rgb + b.rgb * o; }                                       // add
    else if (m < 2.5) { c = mix(a.rgb, a.rgb * b.rgb, o); }                            // multiply
    else if (m < 3.5) { c = mix(a.rgb, 1.0 - (1.0 - a.rgb) * (1.0 - b.rgb), o); }      // screen
    else              { c = mix(a.rgb, blend_overlay(a.rgb, b.rgb), o); }              // overlay
    return vec4f(c, 1.0);
}
)";
struct CompositeOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Composite";
    static constexpr const char* kSummary = "Blend two inputs (A base, B over): normal/add/multiply/screen/overlay.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "composite", "blend"};
    Param<float> mode{"mode", 0.f, 0.f, 1.f};            // 0 normal .. 1 overlay (5 modes)
    Param<float> opacity{"opacity", 1.f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~CompositeOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<ParamBase*>& o) override { o.push_back(&mode); o.push_back(&opacity); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("A", VIVID_PORT_INPUT));       // port A (base) = node.input
        o.push_back(tex_port("B", VIVID_PORT_INPUT));       // port B (over) = node.input_b
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = gpu::create_shader_checked(c->device, kCompositeWGSL, "Composite", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, 32, "Composite U");
        WGPUBindGroupLayoutEntry e[4]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 32;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment; e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        e[3].binding = 3; e[3].visibility = WGPUShaderStage_Fragment;
        e[3].texture.sampleType = WGPUTextureSampleType_Float; e[3].texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 4; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Composite Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time),
                       pv(0, mode.value), pv(1, opacity.value), 0.f, 0.f, 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        // A = input 0 (base), B = input 1 (over); either may be the black fallback if unconnected.
        const WGPUTextureView va = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        const WGPUTextureView vb = (c->input_texture_count > 1) ? c->input_texture_views[1] : va;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[4]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 32;
        be[1].binding = 1; be[1].textureView = va;
        be[2].binding = 2; be[2].sampler = samp_;
        be[3].binding = 3; be[3].textureView = vb;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 4; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Composite");
    }
};

// --- ShapeGrid: the first REAL-GEOMETRY op. Draws a grid of filled polygons from an actual
// vertex buffer (triangle fans), not a fullscreen SDF. Proves the vertex-rendering path: a real
// pipeline with a vertex-buffer layout + a real Draw(vertex_count). Structure (sides/cols/rows)
// bakes into the buffer; size/rotation/colour animate per-frame via the uniform (in the vertex
// shader), so it stays cheap + beat-reactive. A generator (clears to a bg colour). ---
struct GVert { float cx, cy, ox, oy; };   // cell center (NDC) + n-gon offset (unit)
const char* kShapeGridWGSL = R"(
struct U { res: vec2f, time: f32, size: f32, rotation: f32, pad0: f32, pad1: vec2f, fill: vec4f };
@group(0) @binding(0) var<uniform> u: U;
struct VIn { @location(0) center: vec2f, @location(1) offset: vec2f };
@vertex fn vs_main(v: VIn) -> @builtin(position) vec4f {
    let a = u.rotation * 6.2831853;
    var o = v.offset * u.size;
    o = vec2f(o.x * cos(a) - o.y * sin(a), o.x * sin(a) + o.y * cos(a));
    o.x = o.x / 1.7778;   // correct to a 16:9 DISPLAY (the FBO is a wide internal res, not the output aspect)    // keep shapes round on a wide target
    return vec4f(v.center + o, 0.0, 1.0);
}
@fragment fn fs_main() -> @location(0) vec4f { return u.fill; }
)";
struct ShapeGridOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Shape Grid";
    static constexpr const char* kSummary = "A grid of filled polygons drawn as REAL vertex geometry (not a shader field).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "geometry", "grid"};
    Param<float> sides{"sides", 0.2f, 0.f, 1.f};        // -> 3..8 (square by default)
    Param<float> cols{"cols", 0.55f, 0.f, 1.f}, rows{"rows", 0.55f, 0.f, 1.f};   // -> 1..12
    Param<float> size{"size", 0.62f, 0.f, 1.f}, rotation{"rotation", 0.f, 0.f, 1.f};
    Param<float> r{"r", 0.95f, 0.f, 1.f}, g{"g", 0.95f, 0.f, 1.f}, b{"b", 0.9f, 0.f, 1.f};
    Param<float> bg_r{"bg_r", 0.05f, 0.f, 1.f}, bg_g{"bg_g", 0.05f, 0.f, 1.f}, bg_b{"bg_b", 0.08f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUBuffer vbo_ = nullptr; uint32_t vbo_cap_ = 0, vcount_ = 0;
    int n_ = -1, c_ = -1, rw_ = -1;   // cached structure (rebuild the vertex buffer on change)
    ~ShapeGridOp() override {
        if (vbo_) wgpuBufferRelease(vbo_); if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR; bg_r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&sides); o.push_back(&cols); o.push_back(&rows); o.push_back(&size); o.push_back(&rotation);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&bg_r); o.push_back(&bg_g); o.push_back(&bg_b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    void rebuild_geometry(const VividGpuContext* ctx, int n, int C, int R) {
        std::vector<GVert> v; v.reserve(static_cast<size_t>(C) * R * n * 3);
        const float sx = 1.8f / C, sy = 1.8f / R;                 // cell spacing in NDC [-0.9,0.9]
        const float rad = 0.5f * std::min(sx, sy);                // unit shape radius (fills a cell at size=1)
        for (int rr = 0; rr < R; ++rr) for (int cc = 0; cc < C; ++cc) {
            const float cx = -0.9f + (cc + 0.5f) * sx, cy = -0.9f + (rr + 0.5f) * sy;
            for (int i = 0; i < n; ++i) {                          // triangle fan: center + edge i..i+1
                const float a0 = 6.2831853f * i / n + 1.5707963f, a1 = 6.2831853f * (i + 1) / n + 1.5707963f;
                v.push_back({ cx, cy, 0.f, 0.f });
                v.push_back({ cx, cy, rad * std::cos(a0), rad * std::sin(a0) });
                v.push_back({ cx, cy, rad * std::cos(a1), rad * std::sin(a1) });
            }
        }
        vcount_ = static_cast<uint32_t>(v.size());
        const uint32_t bytes = vcount_ * sizeof(GVert);
        if (bytes > vbo_cap_) {                                   // (re)allocate the vertex buffer
            if (vbo_) wgpuBufferRelease(vbo_);
            WGPUBufferDescriptor bd{}; bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = bytes;
            vbo_ = wgpuDeviceCreateBuffer(ctx->device, &bd); vbo_cap_ = bytes;
        }
        if (vbo_ && bytes) wgpuQueueWriteBuffer(ctx->queue, vbo_, 0, v.data(), bytes);
        n_ = n; c_ = C; rw_ = R;
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = gpu::create_shader_checked(c->device, kShapeGridWGSL, "ShapeGrid", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, 48, "ShapeGrid U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 48;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        // Real vertex-buffer pipeline (center@0, offset@1) — the departure from fullscreen ops.
        WGPUVertexAttribute attrs[2]{};
        attrs[0].format = WGPUVertexFormat_Float32x2; attrs[0].offset = offsetof(GVert, cx); attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x2; attrs[1].offset = offsetof(GVert, ox); attrs[1].shaderLocation = 1;
        WGPUVertexBufferLayout vbl{}; vbl.arrayStride = sizeof(GVert); vbl.stepMode = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = 2; vbl.attributes = attrs;
        WGPUColorTargetState ct{}; ct.format = c->output_format; ct.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs{}; fs.module = sh_; fs.entryPoint = vivid_sv("fs_main"); fs.targetCount = 1; fs.targets = &ct;
        WGPURenderPipelineDescriptor rp{}; rp.layout = pl_;
        rp.vertex.module = sh_; rp.vertex.entryPoint = vivid_sv("vs_main");
        rp.vertex.bufferCount = 1; rp.vertex.buffers = &vbl;
        rp.primitive.topology = WGPUPrimitiveTopology_TriangleList; rp.primitive.frontFace = WGPUFrontFace_CCW;
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
        const int n = 3 + static_cast<int>(std::lround(pv(0, sides.value) * 5.f));      // 3..8
        const int C = 1 + static_cast<int>(std::lround(pv(1, cols.value) * 11.f));      // 1..12
        const int R = 1 + static_cast<int>(std::lround(pv(2, rows.value) * 11.f));
        if (n != n_ || C != c_ || R != rw_) rebuild_geometry(c, n, C, R);
        float u[12] = { float(c->output_width), float(c->output_height), float(c->time),
                        pv(3, size.value), pv(4, rotation.value), 0.f, 0.f, 0.f,
                        pv(5, r.value), pv(6, g.value), pv(7, b.value), 1.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        // Real geometry render pass: clear to the bg colour, then draw the polygon vertices.
        WGPURenderPassColorAttachment att{};
        att.view = c->output_texture_view; att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        att.loadOp = WGPULoadOp_Clear; att.storeOp = WGPUStoreOp_Store;
        att.clearValue = { pv(8, bg_r.value), pv(9, bg_g.value), pv(10, bg_b.value), 1.0 };
        WGPURenderPassDescriptor rpd{}; rpd.colorAttachmentCount = 1; rpd.colorAttachments = &att;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rpd);
        wgpuRenderPassEncoderSetPipeline(pass, pipe_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
        if (vbo_ && vcount_) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbo_, 0, vcount_ * sizeof(GVert));
            wgpuRenderPassEncoderDraw(pass, vcount_, 1, 0, 0);
        }
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
    register_op<TintOp>    (reg, "Tint");           // WGSL example
    register_op<ShapeOp>   (reg, "Shape");          // SDF primitive (circle/polygon) overlay
    register_op<TextOp>    (reg, "Text");           // typography (string from a .txt asset)
    register_op<GradientOp>(reg, "Gradient");       // 2-colour linear/radial gradient source
    register_op<TransformOp>(reg, "Transform");     // zoom/rotate/translate/tile
    register_op<KaleidoscopeOp>(reg, "Kaleidoscope"); // radial mirror fold
    register_op<CompositeOp>(reg, "Composite");     // 2-input blend (over/add/screen/multiply/overlay)
    register_op<ShapeGridOp>(reg, "ShapeGrid");     // REAL vertex geometry: a grid of filled polygons
    register_op<LinesOp>   (reg, "Lines");          // REAL line geometry: grid / radial / rings
    register_op<CustomShaderOp>(reg, "CustomShader");  // data-driven .glsl generator
}

}  // namespace vivid
