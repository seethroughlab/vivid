#pragma once
#include <webgpu/webgpu.h>
#include <cstdint>

namespace vivid {

// A simple offscreen colour render target: a 2D texture usable as both a render
// attachment and a sampled texture (plus copy src/dst for ping-pong/history).
// Used by the FBO effect chain (P12b) to render passes into textures that later
// passes sample.
struct RenderTarget {
    WGPUTexture     tex  = nullptr;
    WGPUTextureView view = nullptr;
    uint32_t        w = 0, h = 0;

    bool init(WGPUDevice device, uint32_t W, uint32_t H, WGPUTextureFormat fmt) {
        w = W; h = H;
        WGPUTextureDescriptor td{};
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
                 | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D;
        td.size = WGPUExtent3D{ W, H, 1 };
        td.format = fmt;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        tex = wgpuDeviceCreateTexture(device, &td);
        if (!tex) return false;
        WGPUTextureViewDescriptor vd{};
        vd.format = fmt;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.baseMipLevel = 0; vd.mipLevelCount = 1;
        vd.baseArrayLayer = 0; vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        view = wgpuTextureCreateView(tex, &vd);
        return view != nullptr;
    }
    void release() {
        if (view) { wgpuTextureViewRelease(view); view = nullptr; }
        if (tex)  { wgpuTextureRelease(tex);       tex  = nullptr; }
    }
};

}  // namespace vivid
