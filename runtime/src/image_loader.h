#pragma once
#include <vivid/types.h>
#include <string>
#include <vector>
#include <cstdint>

namespace vivid {

class Renderer;

/**
 * @brief Image data loaded from a file.
 */
struct ImageData {
    std::vector<uint8_t> pixels;  ///< Pixel data (always RGBA after loading)
    int width = 0;
    int height = 0;
    int channels = 0;             ///< Original channel count (1, 3, or 4)
    bool valid = false;
};

/**
 * @brief Loads images from disk using stb_image.
 *
 * Supports PNG, JPG, BMP, TGA, GIF, PSD, HDR, PIC formats.
 */
class ImageLoader {
public:
    ImageLoader() = default;

    /**
     * @brief Load an image from a file.
     * @param path Path to the image file.
     * @return ImageData with pixels, or invalid ImageData on failure.
     *
     * All images are converted to RGBA format for consistency.
     */
    ImageData load(const std::string& path);

    /**
     * @brief Load an image and create a GPU texture.
     * @param path Path to the image file.
     * @param renderer The renderer to create the texture with.
     * @return A valid Texture, or invalid Texture on failure.
     */
    Texture loadAsTexture(const std::string& path, Renderer& renderer);

    /**
     * @brief Check if a file is a supported image format.
     * @param path Path to check.
     * @return true if the extension is supported.
     */
    static bool isSupported(const std::string& path);
};

} // namespace vivid
