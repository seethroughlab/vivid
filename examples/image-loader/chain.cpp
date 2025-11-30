// Image Loader Example
// Demonstrates loading an image with alpha and applying noise displacement
// Uses: ImageFile + Noise → Displacement + Gradient → Composite pipeline

#include <vivid/vivid.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using namespace vivid;

class ImageNoiseDisplacement : public Operator {
public:
    void init(Context& ctx) override {
        // Find an image file in the assets folder (relative to project folder)
        imagePath_ = findImageFile("assets");

        if (imagePath_.empty()) {
            std::cerr << "[ImageNoiseDisplacement] No image found in assets/\n";
            std::cerr << "  Place a PNG or JPG image in assets/\n";
            std::cerr << "  Try an image with transparency for best results!\n";
        } else {
            std::cout << "[ImageNoiseDisplacement] Loading: " << imagePath_ << "\n";
            needsLoad_ = true;
        }

        // Configure noise generator for displacement map
        noise_
            .scale(4.0f)      // Pattern size
            .speed(0.5f)      // Animation speed
            .octaves(2);      // Keep it simple

        output_ = ctx.createTexture();
        gradient_ = ctx.createTexture();
        displaced_ = ctx.createTexture();
        noiseTexture_ = ctx.createTexture();
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

                // Resize outputs to match image
                output_ = ctx.createTexture(imageTexture_.width, imageTexture_.height);
                gradient_ = ctx.createTexture(imageTexture_.width, imageTexture_.height);
                displaced_ = ctx.createTexture(imageTexture_.width, imageTexture_.height);
            }
            needsLoad_ = false;
        }

        if (!imageTexture_.valid()) {
            return;
        }

        // Step 1: Generate animated gradient background
        Context::ShaderParams gradientParams;
        gradientParams.mode = 4;  // Mode 4 = animated HSV gradient
        ctx.runShader("shaders/gradient.wgsl", nullptr, gradient_, gradientParams);

        // Step 2: Generate noise texture for displacement map
        noise_.process(ctx);
        Texture* noiseTex = ctx.getInputTexture("out", "");

        // Step 3: Apply displacement to the image using noise as map
        Context::ShaderParams displacementParams;
        displacementParams.mode = 0;                      // Luminance mode
        displacementParams.param0 = displacementAmount_;  // Displacement strength
        displacementParams.vec0X = 1.0f;
        displacementParams.vec0Y = 1.0f;

        ctx.runShader("shaders/displacement.wgsl", &imageTexture_, noiseTex, displaced_, displacementParams);

        // Step 4: Composite displaced image over gradient
        Context::ShaderParams compositeParams;
        compositeParams.mode = 0;     // Mode 0 = alpha over
        compositeParams.param0 = 1.0f;  // Full opacity

        ctx.runShader("shaders/composite.wgsl", &gradient_, &displaced_, output_, compositeParams);

        ctx.setOutput("out", output_);
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string imagePath_;
    Texture imageTexture_;
    Texture gradient_;
    Texture displaced_;
    Texture noiseTexture_;
    Texture output_;
    Noise noise_;
    bool needsLoad_ = false;

    // Displacement strength
    float displacementAmount_ = 0.03f;
};

VIVID_OPERATOR(ImageNoiseDisplacement)
