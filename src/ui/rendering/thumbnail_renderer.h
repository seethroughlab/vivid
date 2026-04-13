#pragma once

#include <webgpu/webgpu.h>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace vivid::ui {

// Renders textured quads at specific pixel positions on the surface.
// Uses a uniform buffer for positioning (NDC) and scissor for clipping,
// with deferred draw batching. loadOp=Load to composite over existing UI.
class ThumbnailRenderer {
public:
    ~ThumbnailRenderer() { shutdown(); }
    ThumbnailRenderer() = default;
    ThumbnailRenderer(const ThumbnailRenderer&) = delete;
    ThumbnailRenderer& operator=(const ThumbnailRenderer&) = delete;

    bool init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surface_format);
    void shutdown();

    // Begin a thumbnail draw batch on the surface
    void begin(WGPUCommandEncoder encoder, WGPUTextureView surface,
               uint32_t surface_w, uint32_t surface_h);
    // Queue a thumbnail draw at pixel position (x,y) with size (w,h)
    // Scissor rect clips output to visible area (crop, not squish)
    void draw(WGPUTextureView source, float x, float y, float w, float h,
              uint32_t scissor_x, uint32_t scissor_y, uint32_t scissor_w, uint32_t scissor_h,
              float source_aspect = 0.0f);
    // Flush all queued draws and end the render pass
    void end();

private:
    WGPUBindGroup get_bind_group(WGPUTextureView source);

    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;  // group 0: texture + sampler
    WGPUSampler sampler_ = nullptr;
    WGPUPipelineLayout pipe_layout_ = nullptr;
    WGPUShaderModule shader_ = nullptr;

    // Rect uniform (group 1)
    WGPUBuffer rect_buf_ = nullptr;
    WGPUBindGroupLayout rect_layout_ = nullptr;
    WGPUBindGroup rect_bind_group_ = nullptr;

    // Surface dimensions for current batch
    uint32_t surface_w_ = 0, surface_h_ = 0;

    // Deferred draw state
    WGPUCommandEncoder pending_encoder_ = nullptr;
    WGPUTextureView pending_surface_ = nullptr;

    struct PendingDraw {
        WGPUTextureView source;
        float x, y, w, h;
        uint32_t sc_x, sc_y, sc_w, sc_h;
        float source_aspect;
    };
    static constexpr uint32_t kMaxThumbs = 64;
    static constexpr uint32_t kRectStride = 256;  // minUniformBufferOffsetAlignment
    std::vector<PendingDraw> pending_;

    // Bind group cache: source view -> bind group
    std::unordered_map<WGPUTextureView, WGPUBindGroup> bind_cache_;
};

} // namespace vivid::ui
