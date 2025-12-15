#pragma once

// Vivid Effects 2D - Image Operator
// Loads texture from an image file (PNG, JPG, etc.)

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <string>

namespace vivid::effects {

class Image : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    FilePathParam file{"file", "", "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.exr", "image"};

    /// @}
    // -------------------------------------------------------------------------

    Image() {
        registerParam(file);
    }
    ~Image() override;

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Image"; }

private:
    void loadImage(Context& ctx);
    void createPipeline(Context& ctx);

    std::string m_loadedPath;  // Track which path is currently loaded
    bool m_initialized = false;

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
    WGPUTexture m_loadedTexture = nullptr;
    WGPUTextureView m_loadedTextureView = nullptr;
};

} // namespace vivid::effects
