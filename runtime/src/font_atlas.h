#pragma once

#include <vivid/types.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace vivid {

class Renderer;

/**
 * @brief Glyph information for a single character
 */
struct GlyphInfo {
    float x0, y0, x1, y1;    // Texture coordinates (normalized 0-1)
    float xoff, yoff;         // Offset from cursor position
    float xadvance;           // How much to advance cursor
    float width, height;      // Glyph dimensions in pixels
};

/**
 * @brief Font atlas for efficient text rendering
 *
 * Generates a texture atlas from a TTF font file using stb_truetype.
 * Supports ASCII characters 32-126 by default.
 */
class FontAtlas {
public:
    FontAtlas() = default;
    ~FontAtlas();

    /**
     * @brief Load a TTF font and generate atlas texture
     * @param renderer The renderer for GPU texture creation
     * @param fontPath Path to TTF file
     * @param fontSize Font size in pixels
     * @param atlasSize Size of atlas texture (power of 2)
     * @return true on success
     */
    bool load(Renderer& renderer, const std::string& fontPath, float fontSize, int atlasSize = 512);

    /**
     * @brief Load font from memory buffer
     */
    bool loadFromMemory(Renderer& renderer, const uint8_t* data, size_t size,
                        float fontSize, int atlasSize = 512);

    /**
     * @brief Get glyph info for a character
     */
    const GlyphInfo* getGlyph(char c) const;

    /**
     * @brief Measure text dimensions
     * @return glm::vec2(width, height) in pixels
     */
    glm::vec2 measureText(const std::string& text) const;

    /**
     * @brief Get the atlas texture
     */
    Texture& texture() { return atlasTexture_; }
    const Texture& texture() const { return atlasTexture_; }

    /**
     * @brief Check if font is loaded
     */
    bool valid() const { return atlasTexture_.valid(); }

    /**
     * @brief Get font metrics
     */
    float fontSize() const { return fontSize_; }
    float lineHeight() const { return lineHeight_; }
    float ascent() const { return ascent_; }
    float descent() const { return descent_; }

private:
    Texture atlasTexture_;
    std::unordered_map<char, GlyphInfo> glyphs_;
    float fontSize_ = 0;
    float lineHeight_ = 0;
    float ascent_ = 0;
    float descent_ = 0;
    int atlasSize_ = 0;

    Renderer* renderer_ = nullptr;
};

} // namespace vivid
