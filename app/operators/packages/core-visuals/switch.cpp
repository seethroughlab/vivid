// Core visual package operator: Switch — pick 1 of N texture inputs by an index param.
// Rides on the N-input graph (Phase 5): declares 4 input ports the host wires and feeds
// via input_texture_views[0..3]. WGSL can't dynamically index a list of texture bindings,
// so all four are bound and the fragment selects by index. Re-authored against the
// loadable-operator ABI (VIVID_REGISTER), modeled on the built-in CompositeOp pipeline.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <array>
#include <string>

namespace {
constexpr int kNumInputs = 4;

VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
const char* kSwitchWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { res: vec2f, time: f32, index: f32, pad: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var tex0: texture_2d<f32>;
@group(0) @binding(3) var tex1: texture_2d<f32>;
@group(0) @binding(4) var tex2: texture_2d<f32>;
@group(0) @binding(5) var tex3: texture_2d<f32>;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let idx = i32(round(u.index * 3.0));   // normalized [0,1] -> 0..3
    if (idx == 1) { return textureSample(tex1, samp, inp.uv); }
    else if (idx == 2) { return textureSample(tex2, samp, inp.uv); }
    else if (idx == 3) { return textureSample(tex3, samp, inp.uv); }
    return textureSample(tex0, samp, inp.uv);
}
)";
}  // namespace

struct SwitchOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Switch";
    static constexpr const char* kDisplayName = "Switch";
    static constexpr const char* kSummary = "Pass one of four texture inputs through, chosen by index.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "switch", "route"};

    vivid::Param<int> index{"index", 0, {"In 0", "In 1", "In 2", "In 3"}};

    SwitchOp() { vivid::description(index, "Which input port is passed to the output"); }
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&index); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("in_0", VIVID_PORT_INPUT));
        o.push_back(tex_port("in_1", VIVID_PORT_INPUT));
        o.push_back(tex_port("in_2", VIVID_PORT_INPUT));
        o.push_back(tex_port("in_3", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }

    ~SwitchOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_);
        if (ubo_) wgpuBufferRelease(ubo_); if (pipe_) wgpuRenderPipelineRelease(pipe_);
        if (pl_) wgpuPipelineLayoutRelease(pl_); if (bgl_) wgpuBindGroupLayoutRelease(bgl_);
        if (sh_) wgpuShaderModuleRelease(sh_);
    }

    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kSwitchWGSL, "Switch", err);
        if (!sh_ || !err.empty()) { err_ = err.empty() ? "shader module null" : err; return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "Switch U");
        WGPUBindGroupLayoutEntry e[6]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 32;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment; e[1].sampler.type = WGPUSamplerBindingType_Filtering;
        for (int i = 0; i < kNumInputs; ++i) {
            e[2 + i].binding = 2 + i; e[2 + i].visibility = WGPUShaderStage_Fragment;
            e[2 + i].texture.sampleType = WGPUTextureSampleType_Float; e[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
        }
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 6; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Switch Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }

    void process_gpu(const VividGpuContext* c) override {
        if (init_failed_) { vivid_report_gpu_error(c, err_.c_str()); return; }
        if (!pipe_) { if (!lazy_init(c)) { init_failed_ = true; return; } }

        const float idx = c->param_values ? c->param_values[0] : static_cast<float>(index.int_value());
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time), idx, 0.f, 0.f, 0.f, 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        // Bind all four inputs; unconnected ports arrive as the host's black fallback view.
        WGPUTextureView v[kNumInputs];
        for (int i = 0; i < kNumInputs; ++i)
            v[i] = (static_cast<int>(c->input_texture_count) > i && c->input_texture_views[i])
                       ? c->input_texture_views[i] : c->output_texture_view;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[6]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 32;
        be[1].binding = 1; be[1].sampler = samp_;
        for (int i = 0; i < kNumInputs; ++i) { be[2 + i].binding = 2 + i; be[2 + i].textureView = v[i]; }
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 6; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Switch");
    }

private:
    WGPUShaderModule    sh_  = nullptr;
    WGPUBindGroupLayout bgl_ = nullptr;
    WGPUPipelineLayout  pl_  = nullptr;
    WGPURenderPipeline  pipe_ = nullptr;
    WGPUBuffer          ubo_ = nullptr;
    WGPUSampler         samp_ = nullptr;
    WGPUBindGroup       bg_  = nullptr;
    bool                init_failed_ = false;
    std::string         err_;
};

VIVID_REGISTER(SwitchOp)
