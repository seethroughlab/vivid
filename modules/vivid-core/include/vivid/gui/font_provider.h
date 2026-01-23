#pragma once

/**
 * @file font_provider.h
 * @brief Abstract interface for font rendering in GUI components
 *
 * FontProvider decouples GUI rendering from specific font implementations.
 * This allows OverlayCanvas to work with any font system (FreeType, stb_truetype, etc.)
 */

#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <string>

namespace vivid {

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
 * @brief Abstract interface for font rendering
 *
 * Implement this interface to provide fonts to OverlayCanvas.
 * The default implementation is FontAtlas (FreeType-based).
 */
class FontProvider {
public:
    virtual ~FontProvider() = default;

    /// @brief Get the atlas texture view for rendering
    virtual WGPUTextureView textureView() const = 0;

    /// @brief Get glyph info for a character (nullptr if not found)
    virtual const GlyphInfo* getGlyph(char c) const = 0;

    /// @brief Get glyph info for a Unicode codepoint (nullptr if not found)
    virtual const GlyphInfo* getGlyphUnicode(uint32_t codepoint) const { return getGlyph(static_cast<char>(codepoint)); }

    /// @brief Get kerning adjustment between two characters
    virtual float getKerning(char left, char right) const = 0;

    /// @brief Get kerning adjustment between two Unicode codepoints
    virtual float getKerningUnicode(uint32_t left, uint32_t right) const { return getKerning(static_cast<char>(left), static_cast<char>(right)); }

    /// @brief Check if font has kerning information
    virtual bool hasKerning() const = 0;

    /// @brief Measure text dimensions in pixels
    virtual glm::vec2 measureText(const std::string& text) const = 0;

    /// @brief Get font size in pixels
    virtual float fontSize() const = 0;

    /// @brief Get line height in pixels
    virtual float lineHeight() const = 0;

    /// @brief Get ascent (distance from baseline to top)
    virtual float ascent() const = 0;

    /// @brief Get descent (distance from baseline to bottom, typically negative)
    virtual float descent() const = 0;

    /// @brief Check if font is valid/loaded
    virtual bool valid() const = 0;
};

} // namespace vivid
