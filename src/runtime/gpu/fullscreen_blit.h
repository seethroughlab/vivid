#pragma once

#include <webgpu/webgpu.h>

namespace vivid {

enum class FitMode { Fit = 0, Fill = 1, Stretch = 2 };

class FullscreenBlit {
public:
    bool init(WGPUDevice device, WGPUTextureFormat target_format);
    void blit(WGPUCommandEncoder encoder,
              WGPUTextureView source, WGPUTextureView dest);
    // Fit/Fill/Stretch blit with scale/offset
    void blit_fit(WGPUCommandEncoder encoder,
                  WGPUTextureView source, WGPUTextureView dest,
                  uint32_t src_w, uint32_t src_h,
                  uint32_t dst_w, uint32_t dst_h,
                  FitMode fit_mode,
                  bool ui_visible);
    void shutdown();

private:
    WGPUDevice          device_        = nullptr;
    WGPUQueue           queue_         = nullptr;
    WGPURenderPipeline  pipeline_      = nullptr;
    WGPUBindGroupLayout bind_layout_   = nullptr;
    WGPUSampler         sampler_       = nullptr;
    WGPUPipelineLayout  pipe_layout_   = nullptr;
    WGPUShaderModule    shader_        = nullptr;
    WGPUBindGroup       cached_bind_group_ = nullptr;
    WGPUTextureView     cached_source_     = nullptr;

    WGPUTextureFormat   target_format_   = WGPUTextureFormat_Undefined;

    // Fit-mode pipeline (separate shader with scale/offset uniform)
    WGPURenderPipeline  fit_pipeline_    = nullptr;
    WGPUBindGroupLayout fit_bind_layout_ = nullptr;
    WGPUPipelineLayout  fit_pipe_layout_ = nullptr;
    WGPUShaderModule    fit_shader_      = nullptr;
    WGPUBuffer          fit_uniform_buf_ = nullptr;
    WGPUBindGroup       fit_cached_bind_group_ = nullptr;
    WGPUTextureView     fit_cached_source_     = nullptr;
    bool                fit_inited_      = false;
    bool init_fit_pipeline();
};

} // namespace vivid
