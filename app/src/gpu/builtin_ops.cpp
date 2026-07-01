#include "gpu/builtin_ops.h"

#include "gpu/op_runtime.h"
#include "gpu/shader_op.h"
#include "gpu/effect_op.h"
#include "gpu/asset_shader.h"   // AssetShader (CustomShader data-driven .glsl)
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <array>
#include <cstdio>
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
    void collect_params(std::vector<ParamBase*>& o) override { o.push_back(&warp); o.push_back(&hue); o.push_back(&density); o.push_back(&glow); }
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
struct U { res: vec2f, time: f32, hue: f32 };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let c = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + inp.uv.x * 6.2831853 + u.time * 0.5 + u.hue * 6.2831853);
    return vec4f(c, 1.0);
}
)";

struct TintOp : OperatorBase, GpuProcessable {
    static constexpr const char* kDisplayName = "Tint";
    static constexpr const char* kSummary = "WGSL example generator: a hue-shifted gradient (shows the WGSL authoring path).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "tint", "wgsl"};
    Param<float> hue{"hue", 0.5f, 0.f, 1.f};
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
    void collect_params(std::vector<ParamBase*>& o) override { o.push_back(&hue); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = gpu::create_shader_checked(c->device, kTintWGSL, "Tint", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = gpu::create_uniform_buffer(c->device, 16, "Tint U");
        WGPUBindGroupLayoutEntry e{};
        e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 16;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Tint Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 16;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        float u[4] = { float(c->output_width), float(c->output_height), float(c->time),
                       c->param_values ? c->param_values[0] : hue.value };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Tint");
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
    register_op<CustomShaderOp>(reg, "CustomShader");  // data-driven .glsl generator
}

}  // namespace vivid
