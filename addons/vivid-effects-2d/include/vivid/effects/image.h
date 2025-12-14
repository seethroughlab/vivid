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

    std::string m_loadedPath;  // Track which path is currently loaded
    bool m_initialized = false;
};

} // namespace vivid::effects
