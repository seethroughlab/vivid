#pragma once

/**
 * @file font_atlas.h
 * @brief Font atlas for efficient text rendering
 *
 * Generates a texture atlas from a TTF font file using stb_truetype.
 * Used by Canvas operator for text rendering.
 */

#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace vivid {

class Context;

/**
 * @brief Glyph information for a single character
 */
struct GlyphInfo {
    float u0, v0, u1, v1;  ///< Texture coordinates (normalized 0-1)
    float xoff, yoff;       ///< Offset from cursor position
    float xadvance;         ///< How much to advance cursor
    float width, height;    ///< Glyph dimensions in pixels
};

/**
 * @brief Font atlas for efficient text rendering
 *
 * Generates a texture atlas from a TTF font file using stb_truetype.
 * Supports ASCII characters 32-126.
 *
 * @par Example
 * @code
 * FontAtlas font;
 * font.load(ctx, "assets/fonts/arial.ttf", 32.0f);
 * glm::vec2 size = font.measureText("Hello");
 * @endcode
 */
class FontAtlas {
public:
    FontAtlas() = default;
    ~FontAtlas();

    /**
     * @brief Load a TTF font and generate atlas texture
     * @param ctx Context for GPU access
     * @param fontPath Path to TTF file
     * @param fontSize Font size in pixels
     * @param atlasSize Size of atlas texture (power of 2)
     * @return true on success
     */
    bool load(Context& ctx, const std::string& fontPath, float fontSize, int atlasSize = 512);

    /**
     * @brief Load font from memory buffer
     */
    bool loadFromMemory(Context& ctx, const uint8_t* data, size_t size,
                        float fontSize, int atlasSize = 512);

    /**
     * @brief Get glyph info for a character
     * @param c Character to look up
     * @return Pointer to glyph info, or nullptr if not found
     */
    const GlyphInfo* getGlyph(char c) const;

    /**
     * @brief Measure text dimensions
     * @param text Text string to measure
     * @return Width and height in pixels
     */
    glm::vec2 measureText(const std::string& text) const;

    /// @brief Get the atlas texture view
    WGPUTextureView textureView() const { return m_textureView; }

    /// @brief Check if font is loaded
    bool valid() const { return m_texture != nullptr; }

    /// @brief Get font size
    float fontSize() const { return m_fontSize; }

    /// @brief Get line height
    float lineHeight() const { return m_lineHeight; }

    /// @brief Get ascent (distance from baseline to top)
    float ascent() const { return m_ascent; }

    /// @brief Get descent (distance from baseline to bottom, negative)
    float descent() const { return m_descent; }

    /// @brief Release GPU resources
    void cleanup();

private:
    WGPUTexture m_texture = nullptr;
    WGPUTextureView m_textureView = nullptr;

    std::unordered_map<char, GlyphInfo> m_glyphs;
    float m_fontSize = 0;
    float m_lineHeight = 0;
    float m_ascent = 0;
    float m_descent = 0;
    int m_atlasSize = 0;
};

} // namespace vivid
