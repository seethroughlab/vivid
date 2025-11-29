// Image Loader Example
// Demonstrates loading an image with alpha and applying animated noise displacement
// Place a PNG image with transparency in the assets/ folder

#include <vivid/vivid.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using namespace vivid;

class ImageNoiseDisplacement : public Operator {
public:
    void init(Context& ctx) override {
        // Find an image file in the assets folder
        imagePath_ = findImageFile("examples/image-loader/assets");

        if (imagePath_.empty()) {
            std::cerr << "[ImageNoiseDisplacement] No image found in assets/\n";
            std::cerr << "  Place a PNG or JPG image in examples/image-loader/assets/\n";
            std::cerr << "  Try an image with transparency for best results!\n";
        } else {
            std::cout << "[ImageNoiseDisplacement] Loading: " << imagePath_ << "\n";
            needsLoad_ = true;
        }

        output_ = ctx.createTexture();
    }

    static std::string findImageFile(const std::string& directory) {
        static const std::vector<std::string> imageExtensions = {
            ".png", ".jpg", ".jpeg", ".PNG", ".JPG", ".JPEG", ".bmp", ".BMP"
        };

        try {
            for (const auto& entry : fs::directory_iterator(directory)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                for (const auto& imgExt : imageExtensions) {
                    if (ext == imgExt) {
                        return entry.path().string();
                    }
                }
            }
        } catch (...) {}
        return "";
    }

    void process(Context& ctx) override {
        // Load image if needed
        if (needsLoad_ && !imagePath_.empty()) {
            imageTexture_ = ctx.loadImageAsTexture(imagePath_);
            if (imageTexture_.valid()) {
                std::cout << "[ImageNoiseDisplacement] Loaded " << imagePath_
                          << " (" << imageTexture_.width << "x" << imageTexture_.height << ")\n";

                // Resize output to match image
                output_ = ctx.createTexture(imageTexture_.width, imageTexture_.height);
            }
            needsLoad_ = false;
        }

        if (!imageTexture_.valid()) {
            return;
        }

        // Apply animated noise displacement with gradient background
        // The shader generates a colorful animated gradient, then composites
        // the displaced image over it - proving alpha transparency works!
        Context::ShaderParams params;
        params.param0 = displacementAmount_;  // How much to displace
        params.param1 = noiseScale_;          // Noise pattern size
        params.param2 = noiseSpeed_;          // Animation speed
        params.param3 = gradientSpeed_;       // Gradient animation speed

        ctx.runShader("examples/image-loader/shaders/image_over_gradient.wgsl",
                      &imageTexture_, output_, params);

        ctx.setOutput("out", output_);
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string imagePath_;
    Texture imageTexture_;
    Texture output_;
    bool needsLoad_ = false;

    // Displacement parameters - adjust these for different effects!
    float displacementAmount_ = 0.03f;   // Displacement strength (0.01 - 0.1 typical)
    float noiseScale_ = 4.0f;            // Noise pattern size (1.0 - 10.0)
    float noiseSpeed_ = 0.5f;            // Animation speed (0.1 - 2.0)
    float gradientSpeed_ = 1.0f;         // Background gradient animation speed
};

VIVID_OPERATOR(ImageNoiseDisplacement)
