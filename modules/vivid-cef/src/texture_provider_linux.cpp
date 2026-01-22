/**
 * @file texture_provider_linux.cpp
 * @brief Linux texture provider - CPU upload path
 *
 * IMPORTANT: OnAcceleratedPaint is NOT available on Linux in standard CEF builds.
 * CEF only supports OnAcceleratedPaint on Windows with D3D11 shared textures.
 *
 * See: https://bitbucket.org/chromiumembedded/cef/issues/3216
 *
 * This implementation uses the OnPaint (CPU) callback path:
 * 1. CEF renders to a CPU buffer
 * 2. OnPaint is called with BGRA pixel data
 * 3. We upload to WebGPU texture via wgpuQueueWriteTexture
 *
 * Future options for zero-copy on Linux would require:
 * - Custom CEF patches for DMABuf/Ozone support
 * - Vulkan driver with VK_KHR_external_memory_fd
 * - Or EGL with EGL_EXT_image_dma_buf_import
 */

#include "texture_provider.h"

#include <cstring>
#include <vector>
#include <array>
#include <atomic>

namespace vivid::cef {

/**
 * @brief Linux texture provider - STUB implementation (CPU fallback only)
 *
 * This stub provides functional but non-optimal rendering via CPU upload.
 * See file header for the ideal zero-copy implementation.
 */
class TextureProviderLinux : public TextureProvider {
public:
    ~TextureProviderLinux() override {
        cleanup();
    }

    bool init(WGPUDevice device, int width, int height) override {
        m_device = device;
        m_queue = wgpuDeviceGetQueue(device);
        m_width = width;
        m_height = height;

        // TODO: Check for DMABuf/Vulkan external memory support
        // - Query Vulkan device for VK_KHR_external_memory_fd
        // - Or check EGL for EGL_EXT_image_dma_buf_import

        return createTextures();
    }

    void resize(int width, int height) override {
        if (width == m_width && height == m_height) return;

        m_width = width;
        m_height = height;

        releaseTextures();
        createTextures();
    }

    void importFromCEF(void* shared_handle) override {
        // TODO: Implement DMABuf import
        //
        // On Linux, shared_handle is a DMABuf file descriptor
        // The implementation should:
        // 1. Import via Vulkan:
        //    - vkGetMemoryFdPropertiesKHR to get memory type
        //    - VkImportMemoryFdInfoKHR with VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT
        //    - Create VkImage backed by imported memory
        // 2. Or import via EGL:
        //    - eglCreateImageKHR with EGL_LINUX_DMA_BUF_EXT
        //    - glEGLImageTargetTexture2DOES
        // 3. Upload to wgpu texture
        //
        // For now, this is a no-op - CEF will fall back to OnPaint

        (void)shared_handle;  // Unused in stub
        static bool warned = false;
        if (!warned) {
            fprintf(stderr, "[TextureProviderLinux] OnAcceleratedPaint not implemented - using CPU fallback\n");
            warned = true;
        }
    }

    void importFromCPU(const void* buffer, int width, int height) override {
        if (!buffer) return;

        // Resize if dimensions changed
        if (width != m_width || height != m_height) {
            resize(width, height);
        }

        // Get the back buffer for writing
        int backIdx = 1 - m_frontBufferIndex.load();
        WGPUTexture targetTexture = m_textures[backIdx];

        if (!targetTexture) return;

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

        WGPUExtent3D extent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            1
        };

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

    void cleanup() override {
        releaseTextures();
    }

private:
    bool createTextures() {
        // Create double-buffered textures
        for (int i = 0; i < 2; ++i) {
            WGPUTextureDescriptor desc{};
            desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            desc.dimension = WGPUTextureDimension_2D;
            desc.size = {
                static_cast<uint32_t>(m_width),
                static_cast<uint32_t>(m_height),
                1
            };
            // Use BGRA format to match CEF's output
            desc.format = WGPUTextureFormat_BGRA8Unorm;
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;

            m_textures[i] = wgpuDeviceCreateTexture(m_device, &desc);
            if (!m_textures[i]) {
                fprintf(stderr, "[TextureProviderLinux] Failed to create texture %d\n", i);
                return false;
            }

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.mipLevelCount = 1;
            viewDesc.arrayLayerCount = 1;

            m_textureViews[i] = wgpuTextureCreateView(m_textures[i], &viewDesc);
            if (!m_textureViews[i]) {
                fprintf(stderr, "[TextureProviderLinux] Failed to create texture view %d\n", i);
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
    return std::make_unique<TextureProviderLinux>();
}

} // namespace vivid::cef
