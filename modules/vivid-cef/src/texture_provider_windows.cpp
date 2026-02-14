/**
 * @file texture_provider_windows.cpp
 * @brief Windows texture provider - CPU fallback (D3D11 interop TODO)
 *
 * Windows is the ONLY platform where CEF's OnAcceleratedPaint works.
 * CEF provides a D3D11 shared texture handle that can be imported.
 *
 * Current: CPU upload via OnPaint (functional but slower)
 *
 * TODO: Implement D3D11 shared texture support for zero-copy:
 * 1. Receive D3D11 shared texture handle from CEF's OnAcceleratedPaint
 * 2. Open via ID3D11Device::OpenSharedResource to get ID3D11Texture2D
 * 3. Either:
 *    a) If wgpu uses D3D11: Share texture directly
 *    b) If wgpu uses D3D12: Use D3D11on12 interop or staging texture
 * 4. Copy to wgpu texture using GPU operations
 *
 * This is the only platform where zero-copy is possible with standard CEF.
 * macOS and Linux do NOT support OnAcceleratedPaint (CEF limitation).
 */

#include "texture_provider.h"

#include <array>
#include <atomic>
#include <cstring>
#include <vector>

namespace vivid::cef {

/**
 * @brief Windows texture provider - STUB implementation (CPU fallback only)
 *
 * This stub provides functional but non-optimal rendering via CPU upload.
 * See file header for the ideal zero-copy implementation.
 */
class TextureProviderWindows : public TextureProvider {
public:
  ~TextureProviderWindows() override { cleanup(); }

  bool init(WGPUDevice device, int width, int height) override {
    m_device = device;
    m_queue = wgpuDeviceGetQueue(device);
    m_width = width;
    m_height = height;

    // TODO: Initialize D3D11 device for shared texture import
    // D3D11CreateDevice(...) for interop with CEF's D3D11 textures

    return createTextures();
  }

  void resize(int width, int height) override {
    if (width == m_width && height == m_height)
      return;

    m_width = width;
    m_height = height;

    releaseTextures();
    createTextures();
  }

  void importFromCEF(void *shared_handle) override {
    // TODO: Implement D3D11 shared texture import
    //
    // On Windows, shared_handle is a D3D11 shared texture handle
    // The implementation should:
    // 1. Call ID3D11Device::OpenSharedResource to get ID3D11Texture2D
    // 2. Copy to staging texture with ID3D11DeviceContext::CopyResource
    // 3. Map staging texture and upload to wgpu
    //
    // For now, this is a no-op - CEF will fall back to OnPaint

    (void)shared_handle; // Unused in stub
    static bool warned = false;
    if (!warned) {
      fprintf(stderr, "[TextureProviderWindows] OnAcceleratedPaint not "
                      "implemented - using CPU fallback\n");
      warned = true;
    }
  }

  void importFromCPU(const void *buffer, int width, int height) override {
    if (!buffer)
      return;

    // Resize if dimensions changed
    if (width != m_width || height != m_height) {
      resize(width, height);
    }

    // Get the back buffer for writing
    int backIdx = 1 - m_frontBufferIndex.load();
    WGPUTexture targetTexture = m_textures[backIdx];

    if (!targetTexture)
      return;

    // CEF provides BGRA data
    size_t bytesPerRow = width * 4;
    size_t bufferSize = bytesPerRow * height;

    // Upload to GPU
    WGPUTexelCopyTextureInfo dst{};
    dst.texture = targetTexture;
    dst.mipLevel = 0;
    dst.origin = {0, 0, 0};
    dst.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout layout{};
    layout.offset = 0;
    layout.bytesPerRow = static_cast<uint32_t>(bytesPerRow);
    layout.rowsPerImage = static_cast<uint32_t>(height);

    WGPUExtent3D extent = {static_cast<uint32_t>(width),
                           static_cast<uint32_t>(height), 1};

    wgpuQueueWriteTexture(m_queue, &dst, buffer, bufferSize, &layout, &extent);

    // Swap buffers
    m_frontBufferIndex.store(backIdx);
  }

  WGPUTexture getTexture() const override {
    int frontIdx = m_frontBufferIndex.load();
    return m_textures[frontIdx];
  }

  WGPUTextureView getCurrentTextureView() override {
    int frontIdx = m_frontBufferIndex.load();
    return m_textureViews[frontIdx];
  }

  void cleanup() override { releaseTextures(); }

private:
  bool createTextures() {
    // Create double-buffered textures
    for (int i = 0; i < 2; ++i) {
      WGPUTextureDescriptor desc{};
      desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
                   WGPUTextureUsage_CopySrc;
      desc.dimension = WGPUTextureDimension_2D;
      desc.size = {static_cast<uint32_t>(m_width),
                   static_cast<uint32_t>(m_height), 1};
      // Use BGRA format to match CEF's output
      desc.format = WGPUTextureFormat_BGRA8Unorm;
      desc.mipLevelCount = 1;
      desc.sampleCount = 1;

      m_textures[i] = wgpuDeviceCreateTexture(m_device, &desc);
      if (!m_textures[i]) {
        fprintf(stderr,
                "[TextureProviderWindows] Failed to create texture %d\n", i);
        return false;
      }

      WGPUTextureViewDescriptor viewDesc{};
      viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
      viewDesc.dimension = WGPUTextureViewDimension_2D;
      viewDesc.mipLevelCount = 1;
      viewDesc.arrayLayerCount = 1;

      m_textureViews[i] = wgpuTextureCreateView(m_textures[i], &viewDesc);
      if (!m_textureViews[i]) {
        fprintf(stderr,
                "[TextureProviderWindows] Failed to create texture view %d\n",
                i);
        return false;
      }
    }

    return true;
  }

  void releaseTextures() {
    for (int i = 0; i < 2; ++i) {
      if (m_textureViews[i]) {
        wgpuTextureViewRelease(m_textureViews[i]);
        m_textureViews[i] = nullptr;
      }
      if (m_textures[i]) {
        wgpuTextureRelease(m_textures[i]);
        m_textures[i] = nullptr;
      }
    }
  }

  // Double-buffered textures
  std::array<WGPUTexture, 2> m_textures = {nullptr, nullptr};
  std::array<WGPUTextureView, 2> m_textureViews = {nullptr, nullptr};
  std::atomic<int> m_frontBufferIndex{0};
};

// Factory function
std::unique_ptr<TextureProvider> TextureProvider::create() {
  return std::make_unique<TextureProviderWindows>();
}

} // namespace vivid::cef
