#pragma once

// Vivid Effects 2D - TextureOperator
// Base class for operators that output a texture

#include <vivid/operator.h>
#include <webgpu/webgpu.h>

namespace vivid::effects {

// Common texture format for effects pipeline
constexpr WGPUTextureFormat EFFECTS_FORMAT = WGPUTextureFormat_RGBA16Float;

// Helper to create WGPUStringView from C string (wgpu-native style)
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

// Base class for texture-outputting operators
class TextureOperator : public Operator {
public:
    virtual ~TextureOperator();

    // Operator interface
    OutputKind outputKind() const override { return OutputKind::Texture; }

    // Texture output
    WGPUTextureView outputView() const { return m_outputView; }
    WGPUTexture outputTexture() const { return m_output; }

    // Resolution
    int outputWidth() const { return m_width; }
    int outputHeight() const { return m_height; }
    TextureOperator& resolution(int w, int h) { m_width = w; m_height = h; return *this; }

    // Get input texture view from connected operator
    WGPUTextureView inputView(int index = 0) const;

protected:
    // Create output texture with specified dimensions
    void createOutput(Context& ctx);
    void createOutput(Context& ctx, int width, int height);

    // Release output texture
    void releaseOutput();

    // Render to output using a full-screen triangle pass
    void beginRenderPass(WGPURenderPassEncoder& pass, WGPUCommandEncoder& encoder);
    void endRenderPass(WGPURenderPassEncoder pass, WGPUCommandEncoder encoder, Context& ctx);

    WGPUTexture m_output = nullptr;
    WGPUTextureView m_outputView = nullptr;

    int m_width = 1280;
    int m_height = 720;
};

} // namespace vivid::effects
