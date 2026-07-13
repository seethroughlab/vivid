// Core visual package operator: Composite — blend two inputs (A base, B over):
// normal/add/multiply/screen/overlay. Migrated verbatim from the built-in CompositeOp
// (builtin_ops.cpp); behaviour unchanged. Blend math from vivid-classic composite.cpp.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <array>
#include <string>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
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
}  // namespace

struct CompositeOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Composite";
    static constexpr const char* kDisplayName = "Composite";
    static constexpr const char* kSummary = "Blend two inputs (A base, B over): normal/add/multiply/screen/overlay.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "composite", "blend"};
    vivid::Param<float> mode{"mode", 0.f, 0.f, 1.f};
    vivid::Param<float> opacity{"opacity", 1.f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~CompositeOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&mode); o.push_back(&opacity); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("A", VIVID_PORT_INPUT));       // port A (base) = input port 0
        o.push_back(tex_port("B", VIVID_PORT_INPUT));       // port B (over) = input port 1
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kCompositeWGSL, "Composite", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "Composite U");
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
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Composite Pipeline");
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
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Composite");
    }
};

VIVID_REGISTER(CompositeOp)
