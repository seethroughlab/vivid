#pragma once

#include <webgpu/webgpu.h>
#include <unordered_map>
#include <cstdint>

namespace vivid {

// Renders textured quads at specific pixel positions on the surface.
// Uses setViewport() per thumbnail and loadOp=Load to composite over existing UI.
class ThumbnailRenderer {
public:
    bool init(WGPUDevice device, WGPUTextureFormat surface_format);
    void shutdown();

    // Begin a render pass on the surface (loadOp=Load)
    void begin(WGPUCommandEncoder encoder, WGPUTextureView surface,
               uint32_t surface_w, uint32_t surface_h);
    // Draw a single thumbnail at pixel position (x,y) with size (w,h)
    void draw(WGPUTextureView source, float x, float y, float w, float h);
    // End the render pass
    void end();

private:
    WGPUBindGroup get_bind_group(WGPUTextureView source);

    WGPUDevice device_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUPipelineLayout pipe_layout_ = nullptr;
    WGPUShaderModule shader_ = nullptr;

    // Active render pass state
    WGPURenderPassEncoder pass_ = nullptr;

    // Bind group cache: source view → bind group
    std::unordered_map<WGPUTextureView, WGPUBindGroup> bind_cache_;
};

} // namespace vivid
