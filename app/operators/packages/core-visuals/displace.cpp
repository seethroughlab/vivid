// Core visual package operator: Displace — warp a source texture's sampling by a
// second texture. A 2-input effect (the loadable GPU ABI carries both inputs via
// input_texture_views[0..1]); the node's A/B ports are UI-wireable thanks to the
// descriptor-driven port UI. Modeled on the built-in CompositeOp's 2-input WGSL
// pipeline (uniform + tex_a + sampler + tex_b) and vivid-classic's mesh_warp
// displacement math, re-authored against the loadable-operator ABI (VIVID_REGISTER).
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
// binding 1 = source, binding 3 = displacement (matches CompositeOp's A/B layout).
const char* kDisplaceWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { res: vec2f, time: f32, amount: f32, mode: f32, pad0: f32, pad1: vec2f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var srcTex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@group(0) @binding(3) var dispTex: texture_2d<f32>;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let amount = u.amount * 0.5;                      // normalized param -> 0 .. 0.5 UV offset
    let d = textureSample(dispTex, samp, inp.uv);
    var off: vec2f;
    if (u.mode < 0.5) {
        off = (d.rg - 0.5) * 2.0 * amount;           // RG vector: R->x, G->y
    } else {
        let luma = dot(d.rgb, vec3f(0.299, 0.587, 0.114));
        off = vec2f((luma - 0.5) * 2.0 * amount);    // luminance: same offset on both axes
    }
    return textureSample(srcTex, samp, inp.uv + off);
}
)";
}  // namespace

struct DisplaceOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Displace";
    static constexpr const char* kDisplayName = "Displace";
    static constexpr const char* kSummary = "Warp a source texture's sampling by a displacement texture (RG-vector or luminance).";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "displace", "warp"};

    vivid::Param<float> amount{"amount", 0.3f, 0.f, 1.f};   // -> 0 .. 0.5 UV offset
    vivid::Param<int>   mode  {"mode", 0, {"RG Vector", "Luminance"}};

    DisplaceOp() {
        vivid::description(amount, "Displacement strength (0 = no warp)");
        vivid::description(mode, "How the displacement texture drives the offset");
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&amount); o.push_back(&mode); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("source",   VIVID_PORT_INPUT));   // port A = node.input
        o.push_back(tex_port("displace", VIVID_PORT_INPUT));   // port B = node.input_b
        o.push_back(tex_port("texture",  VIVID_PORT_OUTPUT));
    }

    ~DisplaceOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_);
        if (ubo_) wgpuBufferRelease(ubo_); if (pipe_) wgpuRenderPipelineRelease(pipe_);
        if (pl_) wgpuPipelineLayoutRelease(pl_); if (bgl_) wgpuBindGroupLayoutRelease(bgl_);
        if (sh_) wgpuShaderModuleRelease(sh_);
    }

    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kDisplaceWGSL, "Displace", err);
        if (!sh_ || !err.empty()) { err_ = err.empty() ? "shader module null" : err; return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "Displace U");
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
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Displace Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }

    void process_gpu(const VividGpuContext* c) override {
        if (init_failed_) { vivid_report_gpu_error(c, err_.c_str()); return; }
        if (!pipe_) { if (!lazy_init(c)) { init_failed_ = true; return; } }

        const float* p = c->param_values;
        auto pf = [&](int i, float d) { return p ? p[i] : d; };
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time),
                       pf(0, amount.value), pf(1, static_cast<float>(mode.int_value())), 0.f, 0.f, 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        // source = input 0, displacement = input 1; unconnected falls back to the output view.
        const WGPUTextureView vsrc = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        const WGPUTextureView vdsp = (c->input_texture_count > 1) ? c->input_texture_views[1] : vsrc;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[4]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 32;
        be[1].binding = 1; be[1].textureView = vsrc;
        be[2].binding = 2; be[2].sampler = samp_;
        be[3].binding = 3; be[3].textureView = vdsp;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 4; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Displace");
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

VIVID_REGISTER(DisplaceOp)
