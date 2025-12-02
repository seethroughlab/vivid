#include "font_atlas.h"
#include "renderer.h"
#include <fstream>
#include <iostream>
#include <cstring>

// stb_truetype implementation
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace vivid {

FontAtlas::~FontAtlas() {
    // Texture cleanup handled by Texture destructor
}

bool FontAtlas::load(Renderer& renderer, const std::string& fontPath, float fontSize, int atlasSize) {
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

    return loadFromMemory(renderer, fontData.data(), fontData.size(), fontSize, atlasSize);
}

bool FontAtlas::loadFromMemory(Renderer& renderer, const uint8_t* data, size_t size,
                                float fontSize, int atlasSize) {
    renderer_ = &renderer;
    fontSize_ = fontSize;
    atlasSize_ = atlasSize;

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

    ascent_ = ascent * scale;
    descent_ = descent * scale;
    lineHeight_ = (ascent - descent + lineGap) * scale;

    // Allocate atlas bitmap
    std::vector<uint8_t> atlasBitmap(atlasSize * atlasSize, 0);

    // Pack characters into atlas
    stbtt_pack_context packContext;
    if (!stbtt_PackBegin(&packContext, atlasBitmap.data(), atlasSize, atlasSize, 0, 1, nullptr)) {
        std::cerr << "[FontAtlas] Failed to begin packing\n";
        return false;
    }

    // Disable oversampling for crisp pixel fonts
    // (Use 1,1 for pixel fonts; could use 2,2 for smooth fonts)
    stbtt_PackSetOversampling(&packContext, 1, 1);

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
        glyph.x0 = pc.x0 * invAtlasSize;
        glyph.y0 = pc.y0 * invAtlasSize;
        glyph.x1 = pc.x1 * invAtlasSize;
        glyph.y1 = pc.y1 * invAtlasSize;
        glyph.xoff = pc.xoff;
        glyph.yoff = pc.yoff;
        glyph.xadvance = pc.xadvance;
        glyph.width = static_cast<float>(pc.x1 - pc.x0);
        glyph.height = static_cast<float>(pc.y1 - pc.y0);

        glyphs_[c] = glyph;
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
    WGPUDevice device = renderer.device();
    WGPUQueue queue = renderer.queue();

    WGPUTextureDescriptor texDesc = {};
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = {static_cast<uint32_t>(atlasSize), static_cast<uint32_t>(atlasSize), 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    WGPUTexture gpuTexture = wgpuDeviceCreateTexture(device, &texDesc);
    if (!gpuTexture) {
        std::cerr << "[FontAtlas] Failed to create GPU texture\n";
        return false;
    }

    // Upload texture data
    WGPUTexelCopyTextureInfo destInfo = {};
    destInfo.texture = gpuTexture;
    destInfo.mipLevel = 0;
    destInfo.origin = {0, 0, 0};

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

    WGPUTextureView view = wgpuTextureCreateView(gpuTexture, &viewDesc);

    // Store in Texture handle
    atlasTexture_.width = atlasSize;
    atlasTexture_.height = atlasSize;
    atlasTexture_.handle = renderer.registerFontTexture(gpuTexture, view, atlasSize, atlasSize);

    std::cout << "[FontAtlas] Loaded font (" << fontSize << "px, " << atlasSize << "x" << atlasSize << " atlas)\n";
    return true;
}

const GlyphInfo* FontAtlas::getGlyph(char c) const {
    auto it = glyphs_.find(c);
    if (it != glyphs_.end()) {
        return &it->second;
    }
    // Return space for unknown characters
    it = glyphs_.find(' ');
    return it != glyphs_.end() ? &it->second : nullptr;
}

glm::vec2 FontAtlas::measureText(const std::string& text) const {
    float maxWidth = 0;
    float currentWidth = 0;
    int lineCount = 1;

    for (char c : text) {
        if (c == '\n') {
            // Track widest line and start new line
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

    // Don't forget the last line
    maxWidth = std::max(maxWidth, currentWidth);

    return glm::vec2(maxWidth, lineCount * lineHeight_);
}

} // namespace vivid
