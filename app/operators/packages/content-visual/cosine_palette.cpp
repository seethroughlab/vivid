// Core visual package operator: CosinePalette — designed color grading via Inigo Quilez's cosine
// palette: color(t) = a + b*cos(2pi*(c*t + d + phase)). Maps the input's luminance through the palette
// so a scene is recolored with a coherent, designed gradient (not per-op hardcoded enums). `phase` is a
// mappable Param — wire master.high (or a band) to it for band-reactive color cycling; `mix` blends
// between the original and the paletted color. Aligned with ADR-0041's call for a shared iq-palette
// helper for designed color. Self-contained single-pass op (template: feedback.cpp / bloom.cpp).
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
const char* kWGSL = R"(
struct U { a: vec4f, b: vec4f, c: vec4f, d: vec4f, ph_mix: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let src = textureSample(tex, samp, inp.uv);
    let t = dot(src.rgb, vec3f(0.2126, 0.7152, 0.0722));
    let col = u.a.xyz + u.b.xyz * cos(6.28318530718 * (u.c.xyz * t + u.d.xyz + u.ph_mix.x));
    return vec4f(mix(src.rgb, clamp(col, vec3f(0.0), vec3f(1.0)), u.ph_mix.y), src.a);
}
)";
}  // namespace

struct CosinePaletteOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "CosinePalette";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_TRANSFORM;
    static constexpr const char* kDisplayName = "Cosine Palette";
    static constexpr const char* kSummary = "Designed color grading (iq cosine palette). Maps luminance to a gradient; map audio to phase for color cycling.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "color", "palette"};

    // phase (mappable color-cycle knob) + mix, then the four iq vec3 coefficients (default = classic
    // rainbow: a=0.5, b=0.5, c=1, d=(0, 0.33, 0.67)).
    vivid::Param<float> phase{"phase", 0.f, 0.f, 1.f};
    vivid::Param<float> mix_amt{"mix", 1.f, 0.f, 1.f};
    vivid::Param<float> a_r{"a_r", 0.5f, 0.f, 1.f}, a_g{"a_g", 0.5f, 0.f, 1.f}, a_b{"a_b", 0.5f, 0.f, 1.f};
    vivid::Param<float> b_r{"b_r", 0.5f, 0.f, 1.f}, b_g{"b_g", 0.5f, 0.f, 1.f}, b_b{"b_b", 0.5f, 0.f, 1.f};
    vivid::Param<float> c_r{"c_r", 1.0f, 0.f, 4.f}, c_g{"c_g", 1.0f, 0.f, 4.f}, c_b{"c_b", 1.0f, 0.f, 4.f};
    vivid::Param<float> d_r{"d_r", 0.0f, 0.f, 1.f}, d_g{"d_g", 0.33f, 0.f, 1.f}, d_b{"d_b", 0.67f, 0.f, 1.f};

    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~CosinePaletteOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&phase); o.push_back(&mix_amt);
        o.push_back(&a_r); o.push_back(&a_g); o.push_back(&a_b);
        o.push_back(&b_r); o.push_back(&b_g); o.push_back(&b_b);
        o.push_back(&c_r); o.push_back(&c_g); o.push_back(&c_b);
        o.push_back(&d_r); o.push_back(&d_g); o.push_back(&d_b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kWGSL, "CosinePalette", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 80, "CosinePalette U");
        WGPUBindGroupLayoutEntry e[3]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 80;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment; e[1].sampler.type = WGPUSamplerBindingType_Filtering;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment;
        e[2].texture.sampleType = WGPUTextureSampleType_Float; e[2].texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 3; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "CosinePalette Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        auto pv = [&](int i, float def) { return c->param_values ? c->param_values[i] : def; };
        // uniform: a.xyz0, b.xyz0, c.xyz0, d.xyz0, (phase, mix, 0, 0) — 20 floats / 80 bytes.
        const float u[20] = {
            pv(2, a_r.value),  pv(3, a_g.value),  pv(4, a_b.value),  0.f,
            pv(5, b_r.value),  pv(6, b_g.value),  pv(7, b_b.value),  0.f,
            pv(8, c_r.value),  pv(9, c_g.value),  pv(10, c_b.value), 0.f,
            pv(11, d_r.value), pv(12, d_g.value), pv(13, d_b.value), 0.f,
            pv(0, phase.value), pv(1, mix_amt.value), 0.f, 0.f,
        };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        const WGPUTextureView src = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[3]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 80;
        be[1].binding = 1; be[1].sampler = samp_;
        be[2].binding = 2; be[2].textureView = src;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 3; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "CosinePalette");
    }
};

VIVID_REGISTER(CosinePaletteOp)
