#pragma once
// ADR-0064 — a CPU frame → 2D-renderer texture bridge for review media. The ReviewPlayer hands the
// frame loop tightly packed BGRA8 frames; this owns one wgpu texture (BGRA8Unorm, so no swizzle),
// recreating it when the frame size changes, and exposes the view Renderer2D::draw_texture samples.
// The sampled pipeline reads texture_2d<f32>, so storage order is invisible to the shader.
#include <webgpu/webgpu.h>

#include <cstdint>

namespace vivid { struct ReviewFrame; }

namespace vivid::ui {

class MediaTexture {
public:
    MediaTexture() = default;
    ~MediaTexture() { release(); }
    MediaTexture(const MediaTexture&) = delete;
    MediaTexture& operator=(const MediaTexture&) = delete;

    // Upload `frame` (tightly packed BGRA8, bytes_per_row = width*4). Creates/resizes the texture as
    // needed. Returns false when the frame is empty or the texture could not be created.
    bool update(WGPUDevice device, WGPUQueue queue, const ReviewFrame& frame);
    void release();

    bool            valid()  const { return view_ != nullptr; }
    WGPUTextureView view()   const { return view_; }
    uint32_t        width()  const { return w_; }
    uint32_t        height() const { return h_; }
    // Frame count uploaded so far (a UI can tell "no frame yet" from "frame shown").
    uint64_t        frames() const { return frames_; }

private:
    WGPUTexture     tex_  = nullptr;
    WGPUTextureView view_ = nullptr;
    uint32_t        w_ = 0, h_ = 0;
    uint64_t        frames_ = 0;
};

}  // namespace vivid::ui
