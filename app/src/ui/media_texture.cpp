#include "ui/media_texture.h"
#include "platform/review_player.h"

namespace vivid::ui {

void MediaTexture::release() {
    if (view_) wgpuTextureViewRelease(view_);
    if (tex_)  wgpuTextureRelease(tex_);
    view_ = nullptr; tex_ = nullptr; w_ = h_ = 0;
}

bool MediaTexture::update(WGPUDevice device, WGPUQueue queue, const ReviewFrame& frame) {
    if (!device || !queue || !frame.bgra || frame.width == 0 || frame.height == 0) return false;
    if (!tex_ || frame.width != w_ || frame.height != h_) {
        release();
        WGPUTextureDescriptor td{};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D;
        td.size = { frame.width, frame.height, 1 };
        td.format = WGPUTextureFormat_BGRA8Unorm;   // matches the player's BGRA8 rows — no CPU swizzle
        td.mipLevelCount = 1; td.sampleCount = 1;
        tex_ = wgpuDeviceCreateTexture(device, &td);
        if (!tex_) return false;
        view_ = wgpuTextureCreateView(tex_, nullptr);
        if (!view_) { release(); return false; }
        w_ = frame.width; h_ = frame.height;
    }
    WGPUTexelCopyTextureInfo dst{}; dst.texture = tex_; dst.mipLevel = 0; dst.origin = {0, 0, 0}; dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout lay{}; lay.offset = 0; lay.bytesPerRow = frame.bytes_per_row; lay.rowsPerImage = frame.height;
    WGPUExtent3D ext{ frame.width, frame.height, 1 };
    wgpuQueueWriteTexture(queue, &dst, frame.bgra, static_cast<size_t>(frame.bytes_per_row) * frame.height, &lay, &ext);
    ++frames_;
    return true;
}

}  // namespace vivid::ui
