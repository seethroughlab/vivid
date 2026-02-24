#pragma once

#include <webgpu/webgpu.h>

namespace vivid {

class FullscreenBlit {
public:
    bool init(WGPUDevice device, WGPUTextureFormat target_format);
    void blit(WGPUCommandEncoder encoder,
              WGPUTextureView source, WGPUTextureView dest);
    void shutdown();

private:
    WGPUDevice          device_        = nullptr;
    WGPURenderPipeline  pipeline_      = nullptr;
    WGPUBindGroupLayout bind_layout_   = nullptr;
    WGPUSampler         sampler_       = nullptr;
    WGPUPipelineLayout  pipe_layout_   = nullptr;
    WGPUShaderModule    shader_        = nullptr;
    WGPUBindGroup       cached_bind_group_ = nullptr;
    WGPUTextureView     cached_source_     = nullptr;
};

} // namespace vivid
