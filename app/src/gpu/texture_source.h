#pragma once
#include <webgpu/webgpu.h>
#include <cstdint>
#include <vector>

namespace vivid {

// A sampleable texture fed from CPU pixels (BGRA8): the source for an image or
// video node. Reused by the blit pass that pushes it into the visuals chain.
struct TextureSource {
    WGPUTexture       tex  = nullptr;
    WGPUTextureView   view = nullptr;
    uint32_t          w = 0, h = 0;
    WGPUTextureFormat fmt = WGPUTextureFormat_BGRA8Unorm;

    bool init(WGPUDevice device, uint32_t W, uint32_t H, WGPUTextureFormat format) {
        w = W; h = H; fmt = format;
        WGPUTextureDescriptor td{};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D;
        td.size = WGPUExtent3D{ W, H, 1 };
        td.format = format;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        tex = wgpuDeviceCreateTexture(device, &td);
        if (!tex) return false;
        WGPUTextureViewDescriptor vd{};
        vd.format = format;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.baseMipLevel = 0; vd.mipLevelCount = 1;
        vd.baseArrayLayer = 0; vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        view = wgpuTextureCreateView(tex, &vd);
        return view != nullptr;
    }

    // Upload a full BGRA8 frame (w*h*4 bytes).
    void upload(WGPUQueue queue, const uint8_t* bgra) {
        if (!tex || !bgra) return;
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = tex; dst.mipLevel = 0; dst.origin = WGPUOrigin3D{ 0, 0, 0 }; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay{};
        lay.offset = 0; lay.bytesPerRow = w * 4; lay.rowsPerImage = h;
        WGPUExtent3D ext{ w, h, 1 };
        wgpuQueueWriteTexture(queue, &dst, bgra, static_cast<size_t>(w) * h * 4, &lay, &ext);
    }

    void release() {
        if (view) { wgpuTextureViewRelease(view); view = nullptr; }
        if (tex)  { wgpuTextureRelease(tex);       tex  = nullptr; }
    }
};

// A BGRA8 SMPTE-ish test pattern (color bars + vertical gradient + grid lines) —
// proves the CPU-pixel upload + sample path before the real decoders land.
inline std::vector<uint8_t> gen_test_pattern(uint32_t w, uint32_t h) {
    static const uint8_t bars[8][3] = {  // R,G,B
        {235,235,235},{235,235,16},{16,235,235},{16,235,16},
        {235,16,235},{235,16,16},{16,16,235},{30,30,30} };
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        const float gy = 0.35f + 0.65f * (static_cast<float>(y) / h);
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* c = bars[(x * 8) / w];
            uint8_t R = static_cast<uint8_t>(c[0] * gy), G = static_cast<uint8_t>(c[1] * gy), B = static_cast<uint8_t>(c[2] * gy);
            if (x % 32 == 0 || y % 32 == 0) { R = G = B = 50; }
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            px[i + 0] = B; px[i + 1] = G; px[i + 2] = R; px[i + 3] = 255;  // BGRA
        }
    }
    return px;
}

}  // namespace vivid
