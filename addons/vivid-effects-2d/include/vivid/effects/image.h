#pragma once

// Vivid Effects 2D - Image Operator
// Loads texture from an image file (PNG, JPG, etc.)

#include <vivid/effects/texture_operator.h>
#include <string>

namespace vivid::effects {

class Image : public TextureOperator {
public:
    Image() = default;
    ~Image() override;

    // Fluent API
    Image& file(const std::string& path) {
        if (m_filePath != path) {
            m_filePath = path;
            m_needsReload = true;
            markDirty();
        }
        return *this;
    }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Image"; }

private:
    void loadImage(Context& ctx);

    std::string m_filePath;
    bool m_needsReload = false;
    bool m_initialized = false;
};

} // namespace vivid::effects
