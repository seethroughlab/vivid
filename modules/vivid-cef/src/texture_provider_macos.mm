/**
 * @file texture_provider_macos.mm
 * @brief macOS texture provider - CPU upload path
 *
 * IMPORTANT: OnAcceleratedPaint with shared textures is NOT available on macOS
 * in standard CEF builds. This is a Chromium/CEF limitation - OnAcceleratedPaint
 * only works on Windows with D3D11 shared textures.
 *
 * See: https://bitbucket.org/chromiumembedded/cef/issues/3216
 *
 * This implementation uses the OnPaint (CPU) callback path:
 * 1. CEF renders to a CPU buffer
 * 2. OnPaint is called with BGRA pixel data
 * 3. We upload to WebGPU texture via wgpuQueueWriteTexture
 *
 * On Apple Silicon, the unified memory architecture makes this upload
 * relatively efficient (~0.5-1ms for 1080p content).
 *
 * The IOSurface code is kept for potential future use if:
 * - Custom CEF builds with Mac shared texture patches are used
 * - CEF adds native macOS shared texture support
 */

#include "texture_provider.h"

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>

#include <cstring>
#include <mutex>
#include <atomic>
#include <array>

namespace vivid::cef {

/**
 * @brief macOS texture provider with IOSurface support
 */
class TextureProviderMacOS : public TextureProvider {
public:
    ~TextureProviderMacOS() override {
        cleanup();
    }

    bool init(WGPUDevice device, int width, int height) override {
        m_device = device;
        m_queue = wgpuDeviceGetQueue(device);
        m_width = width;
        m_height = height;

        // Initialize Metal device for potential future optimizations
        m_metalDevice = MTLCreateSystemDefaultDevice();
        if (!m_metalDevice) {
            fprintf(stderr, "[TextureProviderMacOS] Failed to create Metal device\n");
            // Continue anyway - we can still use CPU path
        }

        return createTextures();
    }

    void resize(int width, int height) override {
        if (width == m_width && height == m_height) return;

        m_width = width;
        m_height = height;

        // Release old textures
        releaseTextures();

        // Create new textures at new size
        createTextures();
    }

    void importFromCEF(void* shared_handle) override {
        @autoreleasepool {
            // On macOS, shared_handle is an IOSurfaceRef
            // when shared_texture_enabled is true in CefWindowInfo
            if (shared_handle) {
                // Cast to IOSurfaceRef - CEF passes the IOSurface handle directly
                IOSurfaceRef surface = reinterpret_cast<IOSurfaceRef>(shared_handle);
                if (surface) {
                    importFromIOSurface(surface);
                    return;
                }
            }

            // If no shared texture, fall back will be handled by OnPaint callback
            // which calls importFromCPU
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

        // CEF provides BGRA data, we store as BGRA (no swizzle needed for BGRA8Unorm)
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
        m_hasNewFrame.store(true);
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
        m_metalDevice = nil;
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
            // Use BGRA format to match CEF's output - no swizzle needed
            desc.format = WGPUTextureFormat_BGRA8Unorm;
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;

            m_textures[i] = wgpuDeviceCreateTexture(m_device, &desc);
            if (!m_textures[i]) {
                fprintf(stderr, "[TextureProviderMacOS] Failed to create texture %d\n", i);
                return false;
            }

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.mipLevelCount = 1;
            viewDesc.arrayLayerCount = 1;

            m_textureViews[i] = wgpuTextureCreateView(m_textures[i], &viewDesc);
            if (!m_textureViews[i]) {
                fprintf(stderr, "[TextureProviderMacOS] Failed to create texture view %d\n", i);
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

    void importFromIOSurface(IOSurfaceRef surface) {
        if (!surface) return;

        // Get surface properties
        size_t surfaceWidth = IOSurfaceGetWidth(surface);
        size_t surfaceHeight = IOSurfaceGetHeight(surface);
        size_t bytesPerRow = IOSurfaceGetBytesPerRow(surface);
        OSType pixelFormat = IOSurfaceGetPixelFormat(surface);

        // Verify format - CEF should provide BGRA
        bool isBGRA = (pixelFormat == kCVPixelFormatType_32BGRA ||
                       pixelFormat == 'BGRA');

        if (!isBGRA) {
            // Log once per different format
            static OSType lastFormat = 0;
            if (lastFormat != pixelFormat) {
                fprintf(stderr, "[TextureProviderMacOS] Unexpected IOSurface format: '%c%c%c%c' (0x%08x)\n",
                        (char)((pixelFormat >> 24) & 0xFF),
                        (char)((pixelFormat >> 16) & 0xFF),
                        (char)((pixelFormat >> 8) & 0xFF),
                        (char)(pixelFormat & 0xFF),
                        pixelFormat);
                lastFormat = pixelFormat;
            }
        }

        // Resize if needed
        if (static_cast<int>(surfaceWidth) != m_width ||
            static_cast<int>(surfaceHeight) != m_height) {
            resize(static_cast<int>(surfaceWidth), static_cast<int>(surfaceHeight));
        }

        // Get the back buffer for writing
        int backIdx = 1 - m_frontBufferIndex.load();
        WGPUTexture targetTexture = m_textures[backIdx];

        if (!targetTexture) return;

        // Lock the IOSurface for reading
        // kIOSurfaceLockReadOnly ensures we don't block CEF from writing to the next surface
        kern_return_t lockResult = IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr);
        if (lockResult != kIOReturnSuccess) {
            fprintf(stderr, "[TextureProviderMacOS] Failed to lock IOSurface: %d\n", lockResult);
            return;
        }

        // Get the base address - on Apple Silicon this is in unified memory
        // so the subsequent wgpuQueueWriteTexture is very fast
        void* baseAddress = IOSurfaceGetBaseAddress(surface);

        if (baseAddress) {
            WGPUTexelCopyTextureInfo dst{};
            dst.texture = targetTexture;
            dst.mipLevel = 0;
            dst.origin = {0, 0, 0};
            dst.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyBufferLayout layout{};
            layout.offset = 0;
            layout.bytesPerRow = static_cast<uint32_t>(bytesPerRow);
            layout.rowsPerImage = static_cast<uint32_t>(surfaceHeight);

            WGPUExtent3D extent = {
                static_cast<uint32_t>(surfaceWidth),
                static_cast<uint32_t>(surfaceHeight),
                1
            };

            // Upload from IOSurface memory to wgpu texture
            // On Apple Silicon, IOSurface uses unified memory so this is essentially
            // a GPU-to-GPU copy with minimal CPU involvement
            size_t dataSize = bytesPerRow * surfaceHeight;
            wgpuQueueWriteTexture(m_queue, &dst, baseAddress, dataSize, &layout, &extent);
        }

        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);

        // Swap buffers
        m_frontBufferIndex.store(backIdx);
        m_hasNewFrame.store(true);
    }

    // Metal device (for potential future optimizations)
    id<MTLDevice> m_metalDevice = nil;

    // Double-buffered textures
    std::array<WGPUTexture, 2> m_textures = {nullptr, nullptr};
    std::array<WGPUTextureView, 2> m_textureViews = {nullptr, nullptr};
    std::atomic<int> m_frontBufferIndex{0};
    std::atomic<bool> m_hasNewFrame{false};
};

// Factory function
std::unique_ptr<TextureProvider> TextureProvider::create() {
    return std::make_unique<TextureProviderMacOS>();
}

} // namespace vivid::cef
