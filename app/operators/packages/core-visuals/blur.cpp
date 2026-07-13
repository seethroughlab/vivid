// Core visual package operator: Blur — a small box blur of the input (radius wire-drivable).
// Migrated from the built-in BlurOp; GLSL translated to WGSL. Behaviour preserved.
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
const char* kBlurWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, radius: f32, pad: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let px = (vec2f(1.0) / u.res) * (1.0 + u.radius * 8.0);
    var s = textureSample(tex, samp, inp.uv) * 0.36;
    s += textureSample(tex, samp, inp.uv + vec2f( px.x, 0.0)) * 0.16;
    s += textureSample(tex, samp, inp.uv + vec2f(-px.x, 0.0)) * 0.16;
    s += textureSample(tex, samp, inp.uv + vec2f(0.0,  px.y)) * 0.16;
    s += textureSample(tex, samp, inp.uv + vec2f(0.0, -px.y)) * 0.16;
    return s;
}
)";
}  // namespace

struct BlurOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Blur";
    static constexpr const char* kDisplayName = "Blur";
    static constexpr const char* kSummary = "Box blur of the input texture; radius is wire-drivable.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "blur", "soften"};
    vivid::Param<float> radius{"radius", 0.3f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~BlurOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&radius); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kBlurWGSL, "Blur", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "Blur U");
        WGPUBindGroupLayoutEntry e[3]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 32;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment; e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 3; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Blur Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values;
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time),
                       p ? p[0] : radius.value, 0.f, 0.f, 0.f, 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        const WGPUTextureView in = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[3]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 32;
        be[1].binding = 1; be[1].textureView = in;
        be[2].binding = 2; be[2].sampler = samp_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 3; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Blur");
    }
};

VIVID_REGISTER(BlurOp)
