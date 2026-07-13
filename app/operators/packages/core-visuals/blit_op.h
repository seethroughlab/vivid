#pragma once
// Shared 1-in/1-out passthrough blit (samples input port 0 into the output). Used by
// Video (the host injects the shared video/image source into port 0 for the VOp::Video
// node) and Output (the viewer sink). Migrated from builtin_ops.cpp's BlitOp; the GLSL
// blit is translated to WGSL. The host's name-derived VOp classification
// (vop_from_name) still special-cases these — no ABI change needed.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <string>
#include <vector>

namespace core_visuals {

inline VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

inline const char* kBlitWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
@group(0) @binding(0) var tex: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f { return textureSample(tex, samp, inp.uv); }
)";

struct BlitOp : vivid::OperatorBase, vivid::GpuProcessable {
    const char* label_;
    explicit BlitOp(const char* label) : label_(label) {}
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUSampler samp_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~BlitOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kBlitWGSL, label_, err);
        if (!sh_ || !err.empty()) return false;
        WGPUBindGroupLayoutEntry e[2]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment;
        e[0].texture.sampleType = WGPUTextureSampleType_Float; e[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment; e[1].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 2; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, label_);
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const WGPUTextureView in = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[2]{};
        be[0].binding = 0; be[0].textureView = in;
        be[1].binding = 1; be[1].sampler = samp_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 2; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, label_);
    }
};

}  // namespace core_visuals
