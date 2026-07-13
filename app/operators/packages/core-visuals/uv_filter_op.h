#pragma once
// Shared body for a 1-in/1-out WGSL UV filter with a small uniform (Transform /
// Kaleidoscope). Migrated verbatim from builtin_ops.cpp's UvFilterOp template so the
// two ops can each live in their own auto-discovered package dylib.
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

template <int NPARAM, int UBYTES>
struct UvFilterOp : vivid::OperatorBase, vivid::GpuProcessable {
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
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, wgsl_, label_, err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, UBYTES, label_);
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
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, label_);
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
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, label_);
    }
};

}  // namespace core_visuals
