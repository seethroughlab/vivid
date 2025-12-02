#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"

namespace vivid {

// Forward declaration
class FontAtlas;

/**
 * @brief Text rendering operator.
 *
 * Renders text to a texture using stb_truetype font rendering.
 *
 * Alignment modes:
 * - 0: Left aligned
 * - 1: Center aligned
 * - 2: Right aligned
 *
 * Example:
 * @code
 * Text txt_;
 * txt_.text("Hello World").font("fonts/arial.ttf", 32.0f).color({1, 1, 1, 1});
 * @endcode
 */
class Text : public Operator {
public:
    Text() = default;

    /// Set the text string to render
    Text& text(const std::string& t) { text_ = t; return *this; }
    /// Set font file path and size
    Text& font(const std::string& path, float size = 24.0f) {
        fontPath_ = path;
        fontSize_ = size;
        fontNeedsLoad_ = true;
        return *this;
    }
    /// Set font size
    Text& fontSize(float s) { fontSize_ = s; fontNeedsLoad_ = true; return *this; }
    /// Set text color (RGBA)
    Text& color(glm::vec4 c) { color_ = c; return *this; }
    /// Set text color (RGB, alpha = 1)
    Text& color(glm::vec3 c) { color_ = glm::vec4(c, 1.0f); return *this; }
    /// Set position (pixels from top-left)
    Text& position(glm::vec2 p) { position_ = p; return *this; }
    /// Set position (pixels from top-left)
    Text& position(float x, float y) { position_ = glm::vec2(x, y); return *this; }
    /// Set alignment (0=left, 1=center, 2=right)
    Text& align(int a) { align_ = a; return *this; }
    /// Set background color (alpha < 0 = transparent)
    Text& background(glm::vec4 bg) { background_ = bg; return *this; }
    /// Set output resolution
    Text& size(int w, int h) { width_ = w; height_ = h; return *this; }

    void init(Context& ctx) override {
        if (width_ > 0 && height_ > 0) {
            output_ = ctx.createTexture(width_, height_);
        } else {
            output_ = ctx.createTexture();
        }
    }

    void process(Context& ctx) override {
        // Load font if needed
        if (!fontAtlas_ || fontNeedsLoad_) {
            std::string resolvedPath = ctx.resolvePath(fontPath_);
            fontAtlas_ = ctx.loadFont(resolvedPath, fontSize_);
            fontNeedsLoad_ = false;
            if (!fontAtlas_) {
                std::cerr << "[Text] Failed to load font: " << fontPath_ << "\n";
            }
        }

        if (!fontAtlas_) {
            // No font available, output transparent texture
            ctx.setOutput("out", output_);
            return;
        }

        // Calculate position based on alignment
        glm::vec2 renderPos = position_;
        if (align_ == 1 || align_ == 2) {
            glm::vec2 textSize = ctx.measureText(*fontAtlas_, text_);
            if (align_ == 1) {
                // Center: position is center point
                renderPos.x = position_.x - textSize.x * 0.5f;
            } else {
                // Right: position is right edge
                renderPos.x = position_.x - textSize.x;
            }
        }

        // Render text
        ctx.renderText(*fontAtlas_, text_, renderPos.x, renderPos.y, color_, output_, background_);

        ctx.setOutput("out", output_);
    }

    void cleanup() override {
        // Font is cached by Context, don't need to clean it up here
        fontAtlas_ = nullptr;
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("align", align_, 0, 2),
            vec2Param("position", position_)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string text_ = "Text";
    std::string fontPath_ = "fonts/Pixeled.ttf";
    float fontSize_ = 24.0f;
    glm::vec4 color_ = glm::vec4(1.0f);  // White
    glm::vec2 position_ = glm::vec2(10.0f, 10.0f);
    int align_ = 0;  // 0=left, 1=center, 2=right
    glm::vec4 background_ = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);  // Transparent by default
    int width_ = 0;
    int height_ = 0;

    Texture output_;
    FontAtlas* fontAtlas_ = nullptr;
    bool fontNeedsLoad_ = true;
};

} // namespace vivid
