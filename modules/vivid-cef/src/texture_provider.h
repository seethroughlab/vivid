#pragma once

/**
 * @file texture_provider.h
 * @brief Platform abstraction for GPU texture sharing with CEF
 *
 * TextureProvider handles the platform-specific work of importing
 * CEF's rendered content into WebGPU textures. Each platform has
 * a different optimal path:
 *
 * - macOS: IOSurface (OnAcceleratedPaint with shared_texture_io_surface)
 * - Windows: D3D11 shared textures (OnAcceleratedPaint with shared_texture_handle)
 * - Linux: DMABuf file descriptors (when available)
 *
 * All platforms also support a CPU fallback path via OnPaint.
 */

#include <webgpu/webgpu.h>
#include <include/cef_render_handler.h>
#include <memory>

namespace vivid::cef {

/**
 * @brief Abstract base class for platform-specific texture importing
 */
class TextureProvider {
public:
    virtual ~TextureProvider() = default;

    /**
     * @brief Initialize the texture provider
     * @param device WebGPU device
     * @param width Initial texture width
     * @param height Initial texture height
     * @return true if initialization succeeded
     */
    virtual bool init(WGPUDevice device, int width, int height) = 0;

    /**
     * @brief Resize the texture
     * @param width New width
     * @param height New height
     */
    virtual void resize(int width, int height) = 0;

    /**
     * @brief Import texture from CEF accelerated paint callback
     * @param shared_handle Platform-specific shared texture handle
     *
     * On macOS: IOSurfaceRef
     * On Windows: D3D11 shared texture handle
     * On Linux: DMABuf file descriptor
     *
     * This is the fast path that uses shared GPU resources when available.
     */
    virtual void importFromCEF(void* shared_handle) = 0;

    /**
     * @brief Import texture from CEF CPU paint callback
     * @param buffer BGRA pixel data
     * @param width Buffer width
     * @param height Buffer height
     *
     * This is the fallback path when shared textures aren't available.
     */
    virtual void importFromCPU(const void* buffer, int width, int height) = 0;

    /**
     * @brief Get the current WebGPU texture
     * @return Texture handle, or nullptr if not ready
     */
    virtual WGPUTexture getTexture() const = 0;

    /**
     * @brief Get the current WebGPU texture view
     * @return Texture view handle, or nullptr if not ready
     */
    virtual WGPUTextureView getCurrentTextureView() = 0;

    /**
     * @brief Release resources
     */
    virtual void cleanup() = 0;

    /**
     * @brief Create a platform-appropriate texture provider
     * @return Unique pointer to the provider
     */
    static std::unique_ptr<TextureProvider> create();

protected:
    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;
    int m_width = 0;
    int m_height = 0;
};

} // namespace vivid::cef
