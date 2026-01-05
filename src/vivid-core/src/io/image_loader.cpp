// Vivid I/O - Image Loader Implementation

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <vivid/io/image_loader.h>
#include <vivid/asset_loader.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace vivid::io {

ImageData loadImage(const std::string& path) {
    ImageData result;

    // Use AssetLoader for consistent path resolution across bundled/dev builds
    fs::path resolved = AssetLoader::instance().resolve(path);
    std::string resolvedPath;

    if (!resolved.empty()) {
        resolvedPath = resolved.string();
    } else {
        // Fallback to legacy resolution
        resolvedPath = resolvePath(path, {"assets/images", "assets/materials", "assets/textures"});
    }

    if (resolvedPath.empty()) {
        std::cerr << "vivid-io: Image not found: " << path << std::endl;
        return result;
    }

    // Load with stb_image, forcing RGBA output
    int width, height, channels;
    unsigned char* data = stbi_load(resolvedPath.c_str(), &width, &height, &channels, 4);

    if (!data) {
        std::cerr << "vivid-io: Failed to load image: " << resolvedPath
                  << " - " << stbi_failure_reason() << std::endl;
        return result;
    }

    // Copy to result
    result.width = width;
    result.height = height;
    result.channels = channels;
    result.pixels.assign(data, data + (width * height * 4));

    stbi_image_free(data);
    return result;
}

ImageDataHDR loadImageHDR(const std::string& path) {
    ImageDataHDR result;

    // Use AssetLoader for consistent path resolution across bundled/dev builds
    fs::path resolved = AssetLoader::instance().resolve(path);
    std::string resolvedPath;

    if (!resolved.empty()) {
        resolvedPath = resolved.string();
    } else {
        // Fallback to legacy resolution
        resolvedPath = resolvePath(path, {"assets/hdri", "assets/environments", "assets"});
    }

    if (resolvedPath.empty()) {
        std::cerr << "vivid-io: HDR image not found: " << path << std::endl;
        return result;
    }

    // Load with stb_image, requesting 3 channels (RGB)
    int width, height, channels;
    float* data = stbi_loadf(resolvedPath.c_str(), &width, &height, &channels, 3);

    if (!data) {
        std::cerr << "vivid-io: Failed to load HDR image: " << resolvedPath
                  << " - " << stbi_failure_reason() << std::endl;
        return result;
    }

    // Copy to result
    result.width = width;
    result.height = height;
    result.channels = channels;
    result.pixels.assign(data, data + (width * height * 3));

    stbi_image_free(data);
    return result;
}

ImageData loadImageFromMemory(const uint8_t* data, size_t size) {
    ImageData result;

    if (!data || size == 0) {
        std::cerr << "vivid-io: Invalid memory buffer for image loading" << std::endl;
        return result;
    }

    int width, height, channels;
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);

    if (!pixels) {
        std::cerr << "vivid-io: Failed to decode image from memory - "
                  << stbi_failure_reason() << std::endl;
        return result;
    }

    result.width = width;
    result.height = height;
    result.channels = channels;
    result.pixels.assign(pixels, pixels + (width * height * 4));

    stbi_image_free(pixels);
    return result;
}

ImageDataHDR loadImageHDRFromMemory(const uint8_t* data, size_t size) {
    ImageDataHDR result;

    if (!data || size == 0) {
        std::cerr << "vivid-io: Invalid memory buffer for HDR loading" << std::endl;
        return result;
    }

    int width, height, channels;
    float* pixels = stbi_loadf_from_memory(data, static_cast<int>(size), &width, &height, &channels, 3);

    if (!pixels) {
        std::cerr << "vivid-io: Failed to decode HDR from memory - "
                  << stbi_failure_reason() << std::endl;
        return result;
    }

    result.width = width;
    result.height = height;
    result.channels = channels;
    result.pixels.assign(pixels, pixels + (width * height * 3));

    stbi_image_free(pixels);
    return result;
}

bool fileExists(const std::string& path) {
    return fs::exists(path) && fs::is_regular_file(path);
}

std::string resolvePath(const std::string& path, const std::vector<std::string>& searchPaths) {
    // Check if path is absolute and exists
    fs::path p = path;
    if (p.is_absolute()) {
        if (fs::exists(p)) {
            return path;
        }
        return "";  // Absolute path doesn't exist
    }

    // Check current directory first
    if (fs::exists(p)) {
        return fs::absolute(p).string();
    }

    // Check search paths
    for (const auto& searchDir : searchPaths) {
        fs::path candidate = fs::path(searchDir) / path;
        if (fs::exists(candidate)) {
            return fs::absolute(candidate).string();
        }
    }

    // Not found
    return "";
}

} // namespace vivid::io
