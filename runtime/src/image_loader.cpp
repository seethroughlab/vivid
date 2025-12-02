#include "image_loader.h"
#include "renderer.h"
#include <iostream>
#include <algorithm>
#include <cctype>

// stb_image implementation
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO  // We'll use our own file loading
#include <stb_image.h>

namespace vivid {

ImageData ImageLoader::load(const std::string& path) {
    ImageData result;

    // Load file into memory first
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        std::cerr << "[ImageLoader] Failed to open file: " << path << "\n";
        return result;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> fileData(fileSize);
    size_t bytesRead = fread(fileData.data(), 1, fileSize, f);
    fclose(f);

    if (bytesRead != static_cast<size_t>(fileSize)) {
        std::cerr << "[ImageLoader] Failed to read file: " << path << "\n";
        return result;
    }

    // Decode with stb_image
    int width, height, channels;
    // Request 4 channels (RGBA) for consistency
    uint8_t* pixels = stbi_load_from_memory(
        fileData.data(),
        static_cast<int>(fileData.size()),
        &width, &height, &channels, 4
    );

    if (!pixels) {
        std::cerr << "[ImageLoader] Failed to decode image: " << path
                  << " - " << stbi_failure_reason() << "\n";
        return result;
    }

    result.width = width;
    result.height = height;
    result.channels = channels;
    result.pixels.assign(pixels, pixels + (width * height * 4));
    // Note: valid() method checks for non-empty pixels, width > 0, height > 0

    stbi_image_free(pixels);

    std::cout << "[ImageLoader] Loaded " << path
              << " (" << width << "x" << height
              << ", " << channels << " channels)\n";

    return result;
}

Texture ImageLoader::loadAsTexture(const std::string& path, Renderer& renderer) {
    ImageData data = load(path);
    if (!data.valid()) {
        return Texture{};
    }

    // Create texture and upload pixels
    Texture tex = renderer.createTexture(data.width, data.height);
    if (tex.valid()) {
        renderer.uploadTexturePixels(tex, data.pixels.data(), data.width, data.height);
    }

    return tex;
}

bool ImageLoader::isSupported(const std::string& path) {
    // Get file extension
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) {
        return false;
    }

    std::string ext = path.substr(dotPos + 1);
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // stb_image supported formats
    return ext == "png" || ext == "jpg" || ext == "jpeg" ||
           ext == "bmp" || ext == "tga" || ext == "gif" ||
           ext == "psd" || ext == "hdr" || ext == "pic" || ext == "pnm";
}

} // namespace vivid
