#include "font_atlas.h"
#include <vivid/context.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

#include <ft2build.h>
#include FT_FREETYPE_H

namespace vivid {

FontAtlas::~FontAtlas() {
    cleanup();
}

void FontAtlas::cleanup() {
    if (m_textureView) {
        wgpuTextureViewRelease(m_textureView);
        m_textureView = nullptr;
    }
    if (m_texture) {
        wgpuTextureRelease(m_texture);
        m_texture = nullptr;
    }
    if (m_ftFace) {
        FT_Done_Face(m_ftFace);
        m_ftFace = nullptr;
    }
    if (m_ftLibrary) {
        FT_Done_FreeType(m_ftLibrary);
        m_ftLibrary = nullptr;
    }
    m_glyphs.clear();
    m_glyphIndices.clear();
    m_hasKerning = false;
}

bool FontAtlas::load(Context& ctx, const std::string& fontPath, float fontSize, int atlasSize) {
    // Read font file
    std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[FontAtlas] Failed to open font: " << fontPath << "\n";
        return false;
    }

    size_t fileSize = file.tellg();
    file.seekg(0);

    std::vector<uint8_t> fontData(fileSize);
    file.read(reinterpret_cast<char*>(fontData.data()), fileSize);
    file.close();

    return loadFromMemory(ctx, fontData.data(), fontData.size(), fontSize, atlasSize);
}

bool FontAtlas::loadFromMemory(Context& ctx, const uint8_t* data, size_t size,
                                float fontSize, int atlasSize) {
    // Clean up any existing resources
    cleanup();

    m_fontSize = fontSize;
    m_atlasSize = atlasSize;

    // Initialize FreeType
    FT_Error error = FT_Init_FreeType(&m_ftLibrary);
    if (error) {
        std::cerr << "[FontAtlas] Failed to initialize FreeType\n";
        return false;
    }

    // Load font from memory
    error = FT_New_Memory_Face(m_ftLibrary, data, static_cast<FT_Long>(size), 0, &m_ftFace);
    if (error) {
        std::cerr << "[FontAtlas] Failed to load font face\n";
        FT_Done_FreeType(m_ftLibrary);
        m_ftLibrary = nullptr;
        return false;
    }

    // Set pixel size
    error = FT_Set_Pixel_Sizes(m_ftFace, 0, static_cast<FT_UInt>(fontSize));
    if (error) {
        std::cerr << "[FontAtlas] Failed to set font size\n";
        cleanup();
        return false;
    }

    // Check for kerning support
    m_hasKerning = FT_HAS_KERNING(m_ftFace);

    // Get font metrics (FreeType metrics are in 1/64th of a pixel)
    m_ascent = static_cast<float>(m_ftFace->size->metrics.ascender) / 64.0f;
    m_descent = static_cast<float>(m_ftFace->size->metrics.descender) / 64.0f;
    m_lineHeight = static_cast<float>(m_ftFace->size->metrics.height) / 64.0f;

    // Allocate atlas bitmap (single channel for now, converted to RGBA later)
    std::vector<uint8_t> atlasBitmap(atlasSize * atlasSize, 0);

    // Simple shelf-based packing
    int shelfY = 0;      // Current shelf Y position
    int shelfHeight = 0; // Height of current shelf
    int cursorX = 0;     // X position in current shelf
    const int padding = 2;  // Padding between glyphs

    // Pack ASCII characters 32-126
    const int firstChar = 32;
    const int lastChar = 126;
    float invAtlasSize = 1.0f / atlasSize;

    for (int c = firstChar; c <= lastChar; c++) {
        // Load glyph
        FT_UInt glyphIndex = FT_Get_Char_Index(m_ftFace, c);
        error = FT_Load_Glyph(m_ftFace, glyphIndex, FT_LOAD_RENDER);
        if (error) {
            std::cerr << "[FontAtlas] Failed to load glyph for character " << c << "\n";
            continue;
        }

        FT_GlyphSlot g = m_ftFace->glyph;
        int glyphWidth = static_cast<int>(g->bitmap.width);
        int glyphHeight = static_cast<int>(g->bitmap.rows);

        // Check if we need to start a new shelf
        if (cursorX + glyphWidth + padding > atlasSize) {
            cursorX = 0;
            shelfY += shelfHeight + padding;
            shelfHeight = 0;
        }

        // Check if we've run out of vertical space
        if (shelfY + glyphHeight > atlasSize) {
            std::cerr << "[FontAtlas] Atlas too small for all glyphs\n";
            break;
        }

        // Copy glyph bitmap to atlas
        for (int row = 0; row < glyphHeight; row++) {
            for (int col = 0; col < glyphWidth; col++) {
                int atlasX = cursorX + col;
                int atlasY = shelfY + row;
                int atlasIdx = atlasY * atlasSize + atlasX;
                int bitmapIdx = row * g->bitmap.pitch + col;
                atlasBitmap[atlasIdx] = g->bitmap.buffer[bitmapIdx];
            }
        }

        // Store glyph info
        GlyphInfo glyph;
        glyph.u0 = static_cast<float>(cursorX) * invAtlasSize;
        glyph.v0 = static_cast<float>(shelfY) * invAtlasSize;
        glyph.u1 = static_cast<float>(cursorX + glyphWidth) * invAtlasSize;
        glyph.v1 = static_cast<float>(shelfY + glyphHeight) * invAtlasSize;
        glyph.xoff = static_cast<float>(g->bitmap_left);
        glyph.yoff = -static_cast<float>(g->bitmap_top);  // FreeType uses Y-up, we use Y-down
        glyph.xadvance = static_cast<float>(g->advance.x) / 64.0f;  // Convert from 1/64 pixels
        glyph.width = static_cast<float>(glyphWidth);
        glyph.height = static_cast<float>(glyphHeight);

        m_glyphs[static_cast<char>(c)] = glyph;
        m_glyphIndices[static_cast<char>(c)] = glyphIndex;

        // Advance cursor
        cursorX += glyphWidth + padding;
        shelfHeight = std::max(shelfHeight, glyphHeight);
    }

    // Convert single-channel to RGBA for GPU texture
    std::vector<uint8_t> rgbaData(atlasSize * atlasSize * 4);
    for (int i = 0; i < atlasSize * atlasSize; i++) {
        rgbaData[i * 4 + 0] = 255;  // R
        rgbaData[i * 4 + 1] = 255;  // G
        rgbaData[i * 4 + 2] = 255;  // B
        rgbaData[i * 4 + 3] = atlasBitmap[i];  // A
    }

    // Create GPU texture
    WGPUDevice device = ctx.device();
    WGPUQueue queue = ctx.queue();

    WGPUTextureDescriptor texDesc = {};
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {static_cast<uint32_t>(atlasSize), static_cast<uint32_t>(atlasSize), 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    m_texture = wgpuDeviceCreateTexture(device, &texDesc);
    if (!m_texture) {
        std::cerr << "[FontAtlas] Failed to create GPU texture\n";
        return false;
    }

    // Upload texture data
    WGPUTexelCopyTextureInfo destInfo = {};
    destInfo.texture = m_texture;
    destInfo.mipLevel = 0;
    destInfo.origin = {0, 0, 0};
    destInfo.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout srcLayout = {};
    srcLayout.offset = 0;
    srcLayout.bytesPerRow = atlasSize * 4;
    srcLayout.rowsPerImage = atlasSize;

    WGPUExtent3D copySize = {static_cast<uint32_t>(atlasSize), static_cast<uint32_t>(atlasSize), 1};
    wgpuQueueWriteTexture(queue, &destInfo, rgbaData.data(), rgbaData.size(), &srcLayout, &copySize);

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;

    m_textureView = wgpuTextureCreateView(m_texture, &viewDesc);

    std::cout << "[FontAtlas] Loaded font (" << fontSize << "px, " << atlasSize << "x" << atlasSize
              << " atlas, kerning: " << (m_hasKerning ? "yes" : "no") << ")\n";
    return true;
}

const GlyphInfo* FontAtlas::getGlyph(char c) const {
    auto it = m_glyphs.find(c);
    if (it != m_glyphs.end()) {
        return &it->second;
    }
    // Return space for unknown characters
    it = m_glyphs.find(' ');
    return it != m_glyphs.end() ? &it->second : nullptr;
}

float FontAtlas::getKerning(char left, char right) const {
    if (!m_hasKerning || !m_ftFace) {
        return 0.0f;
    }

    auto leftIt = m_glyphIndices.find(left);
    auto rightIt = m_glyphIndices.find(right);
    if (leftIt == m_glyphIndices.end() || rightIt == m_glyphIndices.end()) {
        return 0.0f;
    }

    FT_Vector kerning;
    FT_Error error = FT_Get_Kerning(m_ftFace, leftIt->second, rightIt->second,
                                      FT_KERNING_DEFAULT, &kerning);
    if (error) {
        return 0.0f;
    }

    // Convert from 1/64 pixels
    return static_cast<float>(kerning.x) / 64.0f;
}

glm::vec2 FontAtlas::measureText(const std::string& text) const {
    float maxWidth = 0;
    float currentWidth = 0;
    int lineCount = 1;
    char prevChar = 0;

    for (char c : text) {
        if (c == '\n') {
            maxWidth = std::max(maxWidth, currentWidth);
            currentWidth = 0;
            lineCount++;
            prevChar = 0;
            continue;
        }

        const GlyphInfo* glyph = getGlyph(c);
        if (glyph) {
            // Add kerning
            if (prevChar != 0) {
                currentWidth += getKerning(prevChar, c);
            }
            currentWidth += glyph->xadvance;
        }
        prevChar = c;
    }

    maxWidth = std::max(maxWidth, currentWidth);
    return glm::vec2(maxWidth, lineCount * m_lineHeight);
}

} // namespace vivid
