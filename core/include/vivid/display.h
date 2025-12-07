#pragma once

// Vivid V3 - Display
// Handles blitting textures to screen and text rendering

#include <webgpu/webgpu.h>
#include <string>

namespace vivid {

class Display {
public:
    Display(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat surfaceFormat);
    ~Display();

    // Non-copyable
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;

    // Blit a texture to the render target
    void blit(WGPURenderPassEncoder pass, WGPUTextureView texture);

    // Render text (for error messages)
    void renderText(WGPURenderPassEncoder pass, const std::string& text,
                    float x, float y, float scale = 1.0f);

    // Update screen size for text rendering
    void setScreenSize(int width, int height);

    // Check if initialized successfully
    bool isValid() const { return m_valid; }

private:
    bool createBlitPipeline();
    bool createTextPipeline();

    WGPUDevice m_device;
    WGPUQueue m_queue;
    WGPUTextureFormat m_surfaceFormat;

    // Blit resources
    WGPURenderPipeline m_blitPipeline = nullptr;
    WGPUSampler m_sampler = nullptr;
    WGPUBindGroupLayout m_blitBindGroupLayout = nullptr;

    // Text resources
    WGPURenderPipeline m_textPipeline = nullptr;
    WGPUTexture m_fontTexture = nullptr;
    WGPUTextureView m_fontTextureView = nullptr;
    WGPUBindGroupLayout m_textBindGroupLayout = nullptr;
    WGPUBindGroup m_textBindGroup = nullptr;
    WGPUBuffer m_textUniformBuffer = nullptr;
    WGPUBuffer m_textVertexBuffer = nullptr;
    WGPUSampler m_fontSampler = nullptr;

    static constexpr int FONT_CHAR_WIDTH = 8;
    static constexpr int FONT_CHAR_HEIGHT = 8;
    static constexpr int FONT_CHARS_PER_ROW = 16;
    static constexpr int FONT_TEXTURE_SIZE = 128;  // 16x8 = 128 pixels
    static constexpr int MAX_TEXT_CHARS = 1024;

    int m_screenWidth = 1280;
    int m_screenHeight = 720;

    bool m_valid = false;
};

} // namespace vivid
