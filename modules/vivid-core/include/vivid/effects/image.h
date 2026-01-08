#pragma once

// Vivid Effects 2D - Image Operator
// Loads texture from an image file (PNG, JPG, etc.)

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <vivid/operator_registry.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace vivid::effects {

/**
 * @brief Load and display an image file
 *
 * Loads PNG, JPG, BMP, TGA, or EXR image files and outputs them as textures.
 * Supports optional CPU-side pixel access for color sampling.
 *
 * @par Example
 * @code
 * auto& img = chain.add<Image>("bg");
 * img.file = "assets/background.png";
 *
 * // For pixel sampling, enable CPU data:
 * img.keepCpuData = true;
 * // Then in update():
 * glm::vec4 color = img.getPixel(100, 100);
 * @endcode
 *
 * @see Composite, Transform, VideoPlayer
 */
class Image : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Self-Description
    /// @{

    static OperatorDescriptor describe() {
        return OperatorDescriptor("Image", "Generators", "Load image file")
            .withUsage(
                "auto& img = chain.add<Image>(\"bg\");\n"
                "img.file = \"assets/background.png\";\n"
                "// For pixel sampling:\n"
                "img.keepCpuData = true;\n"
                "glm::vec4 color = img.getPixel(100, 100);\n"
            )
            .withExamples({{"examples/image-pipeline"}});
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    FilePathParam file{"file", "", "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.exr", "image"};

    /// Keep CPU pixel data for sampling (disabled by default for performance)
    Param<bool> keepCpuData{"keepCpuData", false};

    /// @}
    // -------------------------------------------------------------------------

    Image() {
        registerParam(file);
        registerParam(keepCpuData);
    }
    ~Image() override;

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Image"; }

    // -------------------------------------------------------------------------
    /// @name Pixel Access (requires keepCpuData = true)
    /// @{

    /// Get pixel color at coordinates (0,0 is top-left)
    /// Returns black if coordinates are out of bounds or CPU data not available
    glm::vec4 getPixel(int x, int y) const;

    /// Get average color of a rectangular region
    /// Coordinates are in image space (0,0 is top-left)
    glm::vec4 getAverageColor(int x, int y, int w, int h) const;

    /// Check if CPU pixel data is available
    bool hasCpuData() const { return !m_cpuPixels.empty(); }

    /// Get image dimensions (0 if not loaded)
    int imageWidth() const { return m_cpuWidth; }
    int imageHeight() const { return m_cpuHeight; }

    /// @}
    // -------------------------------------------------------------------------

private:
    void loadImage(Context& ctx);
    void createPipeline(Context& ctx);

    std::string m_loadedPath;  // Track which path is currently loaded

    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
    WGPUTexture m_loadedTexture = nullptr;
    WGPUTextureView m_loadedTextureView = nullptr;

    // CPU pixel data for sampling (when keepCpuData is true)
    std::vector<uint8_t> m_cpuPixels;  // RGBA, 4 bytes per pixel
    int m_cpuWidth = 0;
    int m_cpuHeight = 0;
};

} // namespace vivid::effects
