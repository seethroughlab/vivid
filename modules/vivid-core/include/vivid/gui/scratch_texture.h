#pragma once

// ScratchTexture - Utility for uploading CPU pixels to GPU texture
// Provides reusable scratch texture that resizes as needed

#include <vivid/operator.h>
#include <webgpu/webgpu.h>

namespace vivid {

/**
 * @brief Scratch texture for uploading CPU pixel data to GPU
 *
 * Maintains a reusable GPU texture that is recreated when size changes.
 * Used for displaying CpuPixels operators in visualization UIs.
 */
class ScratchTexture {
public:
    ScratchTexture() = default;
    ~ScratchTexture();

    // Non-copyable
    ScratchTexture(const ScratchTexture&) = delete;
    ScratchTexture& operator=(const ScratchTexture&) = delete;

    // Move-constructible
    ScratchTexture(ScratchTexture&& other) noexcept;
    ScratchTexture& operator=(ScratchTexture&& other) noexcept;

    /**
     * @brief Initialize with device and queue
     * @param device WebGPU device
     * @param queue WebGPU queue
     */
    void init(WGPUDevice device, WGPUQueue queue);

    /**
     * @brief Upload CPU pixels to scratch texture
     * @param view CPU pixel view from operator
     * @return Texture view for rendering, or nullptr if invalid
     *
     * Creates or resizes texture as needed. The returned view is valid
     * until the next upload() call or release().
     */
    WGPUTextureView upload(const Operator::CpuPixelView& view);

    /**
     * @brief Release GPU resources
     */
    void release();

    /**
     * @brief Check if texture is allocated
     */
    bool valid() const { return m_texture != nullptr; }

    /**
     * @brief Get current texture dimensions
     */
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;
    WGPUTexture m_texture = nullptr;
    WGPUTextureView m_view = nullptr;
    int m_width = 0;
    int m_height = 0;
};

} // namespace vivid
