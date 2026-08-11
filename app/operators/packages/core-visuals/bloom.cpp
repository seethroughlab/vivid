// Core visual package operator: Bloom — post-process glow. Extracts pixels above a luminance
// threshold, blurs them (separable 5-tap Gaussian, 2 iterations), and adds the result back over the
// original. The "produced" look for procedural 3D output; sits inline (Render3D -> Bloom -> Output).
// Ported from vivid-classic operators/gpu/bloom to main's package operator ABI (template: feedback.cpp).
// `intensity` is a mappable Param, so the glow can be driven audio-reactively (e.g. master.high -> glow).
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <array>
#include <string>
#include <vector>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

// Uniform (32 bytes): res.xy, threshold, intensity, radius, + 3 pad floats (std140-friendly).
const char* kUniformWGSL = R"(
struct U { res: vec2f, threshold: f32, intensity: f32, radius: f32, p0: f32, p1: f32, p2: f32 };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
)";

// Bright-pass: keep only luminance above threshold, scaled so the knee is smooth.
const char* kBrightWGSL = R"(
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let c = textureSample(tex, samp, inp.uv);
    let lum = dot(c.rgb, vec3f(0.2126, 0.7152, 0.0722));
    let contrib = max(lum - u.threshold, 0.0) / max(1.0 - u.threshold, 0.001);
    return vec4f(c.rgb * contrib, 1.0);
}
)";

// Separable 5-tap Gaussian (the standard 13-tap-equivalent weights). Direction baked per module so a
// single shared uniform is safe (no per-pass uniform rewrite hazard within one command buffer).
const char* kBlurHWGSL = R"(
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let d = vec2f(u.radius / u.res.x, 0.0);
    var r = textureSample(tex, samp, inp.uv) * 0.2270270270;
    r += textureSample(tex, samp, inp.uv + d * 1.3846153846) * 0.3162162162;
    r += textureSample(tex, samp, inp.uv - d * 1.3846153846) * 0.3162162162;
    r += textureSample(tex, samp, inp.uv + d * 3.2307692308) * 0.0702702703;
    r += textureSample(tex, samp, inp.uv - d * 3.2307692308) * 0.0702702703;
    return r;
}
)";
const char* kBlurVWGSL = R"(
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let d = vec2f(0.0, u.radius / u.res.y);
    var r = textureSample(tex, samp, inp.uv) * 0.2270270270;
    r += textureSample(tex, samp, inp.uv + d * 1.3846153846) * 0.3162162162;
    r += textureSample(tex, samp, inp.uv - d * 1.3846153846) * 0.3162162162;
    r += textureSample(tex, samp, inp.uv + d * 3.2307692308) * 0.0702702703;
    r += textureSample(tex, samp, inp.uv - d * 3.2307692308) * 0.0702702703;
    return r;
}
)";

// Composite: original + bloom*intensity. binding(2)=original, binding(3)=blurred bloom.
const char* kCompositeWGSL = R"(
struct U { res: vec2f, threshold: f32, intensity: f32, radius: f32, p0: f32, p1: f32, p2: f32 };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var texOrig: texture_2d<f32>;
@group(0) @binding(3) var texBloom: texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let o = textureSample(texOrig, samp, inp.uv);
    let b = textureSample(texBloom, samp, inp.uv);
    return vec4f(o.rgb + b.rgb * u.intensity, o.a);
}
)";

std::string module_src(const char* body) { return std::string(kUniformWGSL) + body; }
}  // namespace

struct BloomOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Bloom";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_TRANSFORM;   // ADR-0046: post-process transform
    static constexpr const char* kDisplayName = "Bloom";
    static constexpr const char* kSummary = "Post-process glow: bright pixels bleed light. Put it after Render3D; drive intensity with audio.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "bloom", "glow"};

    vivid::Param<float> threshold{"threshold", 0.6f, 0.f, 1.f};
    vivid::Param<float> intensity{"intensity", 0.8f, 0.f, 3.f};
    vivid::Param<float> radius{"radius", 1.5f, 0.f, 6.f};

    bool tried_ = false; std::string err_;   // ADR-0019: surfaced per-frame via report_if_no_pipeline
    WGPUShaderModule sh_bright_ = nullptr, sh_blurh_ = nullptr, sh_blurv_ = nullptr, sh_comp_ = nullptr;
    WGPUBindGroupLayout bgl1_ = nullptr, bgl2_ = nullptr;
    WGPUPipelineLayout pl1_ = nullptr, pl2_ = nullptr;
    WGPURenderPipeline pipe_bright_ = nullptr, pipe_blurh_ = nullptr, pipe_blurv_ = nullptr, pipe_comp_ = nullptr;
    WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr;
    WGPUTexture texA_ = nullptr, texB_ = nullptr; WGPUTextureView viewA_ = nullptr, viewB_ = nullptr;
    uint32_t tw_ = 0, th_ = 0;
    std::vector<WGPUBindGroup> frame_bgs_;

    ~BloomOp() override {
        release_frame_bgs();
        if (viewA_) wgpuTextureViewRelease(viewA_); if (texA_) wgpuTextureRelease(texA_);
        if (viewB_) wgpuTextureViewRelease(viewB_); if (texB_) wgpuTextureRelease(texB_);
        if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_bright_) wgpuRenderPipelineRelease(pipe_bright_);
        if (pipe_blurh_) wgpuRenderPipelineRelease(pipe_blurh_);
        if (pipe_blurv_) wgpuRenderPipelineRelease(pipe_blurv_);
        if (pipe_comp_) wgpuRenderPipelineRelease(pipe_comp_);
        if (pl1_) wgpuPipelineLayoutRelease(pl1_); if (pl2_) wgpuPipelineLayoutRelease(pl2_);
        if (bgl1_) wgpuBindGroupLayoutRelease(bgl1_); if (bgl2_) wgpuBindGroupLayoutRelease(bgl2_);
        if (sh_bright_) wgpuShaderModuleRelease(sh_bright_);
        if (sh_blurh_) wgpuShaderModuleRelease(sh_blurh_);
        if (sh_blurv_) wgpuShaderModuleRelease(sh_blurv_);
        if (sh_comp_) wgpuShaderModuleRelease(sh_comp_);
    }
    void release_frame_bgs() { for (auto bg : frame_bgs_) if (bg) wgpuBindGroupRelease(bg); frame_bgs_.clear(); }

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&threshold); o.push_back(&intensity); o.push_back(&radius);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }

    void ensure_tex(const VividGpuContext* c) {
        if (texA_ && tw_ == c->output_width && th_ == c->output_height) return;
        if (viewA_) wgpuTextureViewRelease(viewA_); if (texA_) wgpuTextureRelease(texA_);
        if (viewB_) wgpuTextureViewRelease(viewB_); if (texB_) wgpuTextureRelease(texB_);
        WGPUTextureDescriptor td{};
        td.size = { c->output_width, c->output_height, 1 };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D;
        td.format = c->output_format;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
        WGPUTextureViewDescriptor vd{};
        vd.format = c->output_format; vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
        texA_ = wgpuDeviceCreateTexture(c->device, &td); viewA_ = wgpuTextureCreateView(texA_, &vd);
        texB_ = wgpuDeviceCreateTexture(c->device, &td); viewB_ = wgpuTextureCreateView(texB_, &vd);
        tw_ = c->output_width; th_ = c->output_height;
    }

    WGPUBindGroupLayout make_bgl(WGPUDevice dev, uint32_t tex_count) {
        std::vector<WGPUBindGroupLayoutEntry> e(2 + tex_count, WGPUBindGroupLayoutEntry{});
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 32;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment; e[1].sampler.type = WGPUSamplerBindingType_Filtering;
        for (uint32_t i = 0; i < tex_count; ++i) {
            e[2 + i].binding = 2 + i; e[2 + i].visibility = WGPUShaderStage_Fragment;
            e[2 + i].texture.sampleType = WGPUTextureSampleType_Float;
            e[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
        }
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = static_cast<uint32_t>(e.size()); ld.entries = e.data();
        return wgpuDeviceCreateBindGroupLayout(dev, &ld);
    }

    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        const std::string bright = module_src(kBrightWGSL), blurh = module_src(kBlurHWGSL), blurv = module_src(kBlurVWGSL);
        sh_bright_ = vivid::gpu::create_shader_checked(c->device, bright.c_str(), "Bloom.bright", err);
        sh_blurh_  = vivid::gpu::create_shader_checked(c->device, blurh.c_str(),  "Bloom.blurH", err);
        sh_blurv_  = vivid::gpu::create_shader_checked(c->device, blurv.c_str(),  "Bloom.blurV", err);
        sh_comp_   = vivid::gpu::create_shader_checked(c->device, kCompositeWGSL, "Bloom.composite", err);
        if (!sh_bright_ || !sh_blurh_ || !sh_blurv_ || !sh_comp_ || !err.empty()) { err_ = vivid::gpu::concise_gpu_error(err); return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "Bloom U");
        bgl1_ = make_bgl(c->device, 1);   // bright + blur: one source texture
        bgl2_ = make_bgl(c->device, 2);   // composite: original + bloom
        WGPUPipelineLayoutDescriptor pld1{}; pld1.bindGroupLayoutCount = 1; pld1.bindGroupLayouts = &bgl1_;
        pl1_ = wgpuDeviceCreatePipelineLayout(c->device, &pld1);
        WGPUPipelineLayoutDescriptor pld2{}; pld2.bindGroupLayoutCount = 1; pld2.bindGroupLayouts = &bgl2_;
        pl2_ = wgpuDeviceCreatePipelineLayout(c->device, &pld2);
        pipe_bright_ = vivid::gpu::create_pipeline(c->device, sh_bright_, pl1_, c->output_format, "Bloom.bright");
        pipe_blurh_  = vivid::gpu::create_pipeline(c->device, sh_blurh_,  pl1_, c->output_format, "Bloom.blurH");
        pipe_blurv_  = vivid::gpu::create_pipeline(c->device, sh_blurv_,  pl1_, c->output_format, "Bloom.blurV");
        pipe_comp_   = vivid::gpu::create_pipeline(c->device, sh_comp_,   pl2_, c->output_format, "Bloom.composite");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_bright_ && pipe_blurh_ && pipe_blurv_ && pipe_comp_;
    }

    WGPUBindGroup make_bg(WGPUDevice dev, WGPUBindGroupLayout bgl, WGPUTextureView t0, WGPUTextureView t1) {
        std::vector<WGPUBindGroupEntry> be;
        WGPUBindGroupEntry e0{}; e0.binding = 0; e0.buffer = ubo_; e0.size = 32; be.push_back(e0);
        WGPUBindGroupEntry e1{}; e1.binding = 1; e1.sampler = samp_; be.push_back(e1);
        WGPUBindGroupEntry e2{}; e2.binding = 2; e2.textureView = t0; be.push_back(e2);
        if (t1) { WGPUBindGroupEntry e3{}; e3.binding = 3; e3.textureView = t1; be.push_back(e3); }
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl; bd.entryCount = static_cast<uint32_t>(be.size()); bd.entries = be.data();
        WGPUBindGroup bg = wgpuDeviceCreateBindGroup(dev, &bd);
        frame_bgs_.push_back(bg);
        return bg;
    }

    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (vivid::gpu::report_if_no_pipeline(c, pipe_comp_, err_)) return;
        ensure_tex(c);
        release_frame_bgs();

        auto pv = [&](int i, float def) { return c->param_values ? c->param_values[i] : def; };
        const float u[8] = { float(c->output_width), float(c->output_height),
                             pv(0, threshold.value), pv(1, intensity.value), pv(2, radius.value), 0.f, 0.f, 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));

        const WGPUTextureView src = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;

        // bright: src -> A ; then 2 iterations of separable blur ping-ponging A<->B ; composite src+A -> output.
        vivid::gpu::run_pass(c->command_encoder, pipe_bright_, make_bg(c->device, bgl1_, src, nullptr), viewA_, "Bloom.bright");
        for (int i = 0; i < 2; ++i) {
            vivid::gpu::run_pass(c->command_encoder, pipe_blurh_, make_bg(c->device, bgl1_, viewA_, nullptr), viewB_, "Bloom.blurH");
            vivid::gpu::run_pass(c->command_encoder, pipe_blurv_, make_bg(c->device, bgl1_, viewB_, nullptr), viewA_, "Bloom.blurV");
        }
        vivid::gpu::run_pass(c->command_encoder, pipe_comp_, make_bg(c->device, bgl2_, src, viewA_), c->output_texture_view, "Bloom.composite");
    }
};

VIVID_REGISTER(BloomOp)
