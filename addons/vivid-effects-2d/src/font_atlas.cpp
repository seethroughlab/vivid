#include "font_atlas.h"
#include <vivid/context.h>
#include <fstream>
#include <iostream>
#include <cstring>

// stb_truetype implementation
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

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
    m_glyphs.clear();
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

    // Initialize stb_truetype
    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, data, 0)) {
        std::cerr << "[FontAtlas] Failed to initialize font\n";
        return false;
    }

    // Get font metrics
    float scale = stbtt_ScaleForPixelHeight(&fontInfo, fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);

    m_ascent = ascent * scale;
    m_descent = descent * scale;
    m_lineHeight = (ascent - descent + lineGap) * scale;

    // Allocate atlas bitmap
    std::vector<uint8_t> atlasBitmap(atlasSize * atlasSize, 0);

    // Pack characters into atlas
    stbtt_pack_context packContext;
    if (!stbtt_PackBegin(&packContext, atlasBitmap.data(), atlasSize, atlasSize, 0, 1, nullptr)) {
        std::cerr << "[FontAtlas] Failed to begin packing\n";
        return false;
    }

    // Use 2x oversampling for smoother fonts
    stbtt_PackSetOversampling(&packContext, 2, 2);

    // Pack ASCII characters 32-126
    const int firstChar = 32;
    const int numChars = 95;
    std::vector<stbtt_packedchar> charData(numChars);

    if (!stbtt_PackFontRange(&packContext, data, 0, fontSize, firstChar, numChars, charData.data())) {
        std::cerr << "[FontAtlas] Failed to pack font range\n";
        stbtt_PackEnd(&packContext);
        return false;
    }

    stbtt_PackEnd(&packContext);

    // Store glyph info
    float invAtlasSize = 1.0f / atlasSize;
    for (int i = 0; i < numChars; i++) {
        char c = static_cast<char>(firstChar + i);
        const stbtt_packedchar& pc = charData[i];

        GlyphInfo glyph;
        glyph.u0 = pc.x0 * invAtlasSize;
        glyph.v0 = pc.y0 * invAtlasSize;
        glyph.u1 = pc.x1 * invAtlasSize;
        glyph.v1 = pc.y1 * invAtlasSize;
        glyph.xoff = pc.xoff;
        glyph.yoff = pc.yoff;
        glyph.xadvance = pc.xadvance;
        glyph.width = static_cast<float>(pc.x1 - pc.x0);
        glyph.height = static_cast<float>(pc.y1 - pc.y0);

        m_glyphs[c] = glyph;
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

    std::cout << "[FontAtlas] Loaded font (" << fontSize << "px, " << atlasSize << "x" << atlasSize << " atlas)\n";
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

glm::vec2 FontAtlas::measureText(const std::string& text) const {
    float maxWidth = 0;
    float currentWidth = 0;
    int lineCount = 1;

    for (char c : text) {
        if (c == '\n') {
            maxWidth = std::max(maxWidth, currentWidth);
            currentWidth = 0;
            lineCount++;
            continue;
        }

        const GlyphInfo* glyph = getGlyph(c);
        if (glyph) {
            currentWidth += glyph->xadvance;
        }
    }

    maxWidth = std::max(maxWidth, currentWidth);
    return glm::vec2(maxWidth, lineCount * m_lineHeight);
}

} // namespace vivid
