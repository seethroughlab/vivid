// Image Loader Example
// Demonstrates loading an image with alpha and applying noise displacement
// Uses: ImageFile + Noise → Displacement + Gradient → Composite pipeline

#include <vivid/vivid.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using namespace vivid;

// Track image files and current index
static std::vector<std::string> imagePaths;
static size_t currentIndex = 0;

std::vector<std::string> findImageFiles(const std::string& directory) {
    std::vector<std::string> images;
    static const std::vector<std::string> imageExtensions = {
        ".png", ".jpg", ".jpeg", ".PNG", ".JPG", ".JPEG", ".bmp", ".BMP"
    };

    try {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            for (const auto& imgExt : imageExtensions) {
                if (ext == imgExt) {
                    images.push_back(entry.path().string());
                    break;
                }
            }
        }
        std::sort(images.begin(), images.end());
    } catch (const std::exception& e) {
        std::cerr << "[ImageLoader] Error scanning directory: " << e.what() << "\n";
    }
    return images;
}

void setup(Chain& chain) {
    // Find all image files (relative to project folder)
    imagePaths = findImageFiles("assets");

    if (imagePaths.empty()) {
        std::cerr << "[ImageLoader] No image files found in assets folder\n";
        std::cerr << "[ImageLoader] Place .png/.jpg files in assets/\n";
        return;
    }

    std::cout << "[ImageLoader] Found " << imagePaths.size() << " image(s):\n";
    for (const auto& path : imagePaths) {
        std::cout << "  - " << path << "\n";
    }

    // Load first image
    chain.add<ImageFile>("image")
        .path(imagePaths[0]);

    // Create animated gradient background
    chain.add<Gradient>("gradient")
        .mode(4);  // HSV animated gradient

    // Create noise for displacement
    chain.add<Noise>("noise")
        .scale(4.0f)
        .speed(0.5f)
        .octaves(2);

    // Apply displacement to image using noise as map
    chain.add<Displacement>("displaced")
        .input("image")
        .map("noise")
        .amount(0.03f);

    // Composite displaced image over gradient (alpha blend)
    chain.add<Composite>("output")
        .a("gradient")
        .b("displaced")
        .mode(0)      // Alpha over
        .mix(1.0f);

    chain.setOutput("output");
}

void update(Chain& chain, Context& ctx) {
    if (imagePaths.empty()) return;

    // SPACE: Switch to next image
    if (ctx.wasKeyPressed(Key::Space)) {
        currentIndex = (currentIndex + 1) % imagePaths.size();
        chain.get<ImageFile>("image").path(imagePaths[currentIndex]);
        std::cout << "[ImageLoader] Now showing: " << imagePaths[currentIndex] << "\n";
    }

    // Animate gradient with rotating angle
    float angle = ctx.time() * 0.2f;
    chain.get<Gradient>("gradient").angle(angle);
}

VIVID_CHAIN(setup, update)
