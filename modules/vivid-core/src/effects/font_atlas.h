#pragma once

/**
 * @file font_atlas.h
 * @brief Font atlas for efficient text rendering
 *
 * Generates a texture atlas from a TTF font file using FreeType.
 * Implements FontProvider interface for use with OverlayCanvas.
 */

#include <vivid/gui/font_provider.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <unordered_map>

// Forward declare FreeType types
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

namespace vivid {

class Context;

/**
 * @brief Font atlas for efficient text rendering
 *
 * Generates a texture atlas from a TTF font file using FreeType.
 * Implements FontProvider interface for use with OverlayCanvas.
 * Supports ASCII characters 32-126 with kerning support.
 *
 * @par Example
 * @code
 * FontAtlas font;
 * font.load(ctx, "assets/fonts/arial.ttf", 32.0f);
 * glm::vec2 size = font.measureText("Hello");
 * @endcode
 */
class FontAtlas : public FontProvider {
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

    // FontProvider interface implementation
    const GlyphInfo* getGlyph(char c) const override;
    const GlyphInfo* getGlyphUnicode(uint32_t codepoint) const override;
    glm::vec2 measureText(const std::string& text) const override;
    float getKerning(char left, char right) const override;
    float getKerningUnicode(uint32_t left, uint32_t right) const override;
    bool hasKerning() const override { return m_hasKerning; }
    WGPUTextureView textureView() const override { return m_textureView; }
    bool valid() const override { return m_texture != nullptr; }
    float fontSize() const override { return m_fontSize; }
    float lineHeight() const override { return m_lineHeight; }
    float ascent() const override { return m_ascent; }
    float descent() const override { return m_descent; }

    /// @brief Release GPU resources
    void cleanup();

private:
    WGPUTexture m_texture = nullptr;
    WGPUTextureView m_textureView = nullptr;

    std::unordered_map<uint32_t, GlyphInfo> m_glyphs;
    std::unordered_map<uint32_t, unsigned int> m_glyphIndices;  // For kerning lookups
    float m_fontSize = 0;
    float m_lineHeight = 0;
    float m_ascent = 0;
    float m_descent = 0;
    int m_atlasSize = 0;

    // FreeType handles (kept for kerning queries)
    FT_Library m_ftLibrary = nullptr;
    FT_Face m_ftFace = nullptr;
    bool m_hasKerning = false;
};

} // namespace vivid
