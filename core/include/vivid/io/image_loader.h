#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace vivid::io {

/// Result of loading an LDR image (8-bit per channel)
struct ImageData {
    std::vector<uint8_t> pixels;  ///< RGBA pixel data
    int width = 0;
    int height = 0;
    int channels = 0;             ///< Original channels before forced RGBA

    bool valid() const { return !pixels.empty() && width > 0 && height > 0; }
};

/// Result of loading an HDR image (32-bit float per channel)
struct ImageDataHDR {
    std::vector<float> pixels;    ///< RGB float pixel data (no alpha)
    int width = 0;
    int height = 0;
    int channels = 0;             ///< Original channels

    bool valid() const { return !pixels.empty() && width > 0 && height > 0; }
};

/// Load an LDR image (PNG, JPG, BMP, TGA, etc.)
/// @param path Path to the image file
/// @return ImageData with RGBA pixels, or empty ImageData on failure
ImageData loadImage(const std::string& path);

/// Load an LDR image from a memory buffer (for embedded GLTF textures)
/// @param data Pointer to encoded image file data in memory (PNG, JPG, etc.)
/// @param size Size of the data in bytes
/// @return ImageData with RGBA pixels, or empty on failure
ImageData loadImageFromMemory(const uint8_t* data, size_t size);

/// Load an HDR image (.hdr, .exr)
/// @param path Path to the HDR image file
/// @return ImageDataHDR with RGB float pixels, or empty ImageDataHDR on failure
ImageDataHDR loadImageHDR(const std::string& path);

/// Load raw file bytes from a memory buffer (for HDR loading from memory)
/// @param data Pointer to file data in memory
/// @param size Size of the data in bytes
/// @return ImageDataHDR with RGB float pixels, or empty on failure
ImageDataHDR loadImageHDRFromMemory(const uint8_t* data, size_t size);

/// Check if a file exists
/// @param path Path to check
/// @return true if file exists and is readable
bool fileExists(const std::string& path);

/// Resolve a path by checking multiple search locations
/// @param path The path to resolve (can be relative or absolute)
/// @param searchPaths Additional directories to search (after current directory)
/// @return The resolved path if found, or empty string if not found
std::string resolvePath(const std::string& path, const std::vector<std::string>& searchPaths = {});

} // namespace vivid::io
