// Video Displacement Example
// Demonstrates video playback with noise displacement
// Uses: VideoFile → Noise → Displacement pipeline

#include <vivid/vivid.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using namespace vivid;

class VideoDisplacement : public Operator {
public:
    void init(Context& ctx) override {
        // Find a video file in the assets folder
        std::string videoPath = findVideoFile("examples/video-displacement/assets");

        if (videoPath.empty()) {
            // Try the video-playback assets as fallback
            videoPath = findVideoFile("examples/video-playback/assets");
        }

        if (!videoPath.empty()) {
            std::cout << "[VideoDisplacement] Loading: " << videoPath << "\n";
            video_.path(videoPath).loop(true).play();
        } else {
            std::cerr << "[VideoDisplacement] No video file found!\n";
            std::cerr << "  Place a video in examples/video-displacement/assets/\n";
            std::cerr << "  or examples/video-playback/assets/\n";
        }

        // Configure noise generator for displacement map
        noise_
            .scale(3.0f)      // Pattern size
            .speed(0.3f)      // Animation speed
            .octaves(2);      // Keep it simple for displacement

        output_ = ctx.createTexture();
    }

    static std::string findVideoFile(const std::string& directory) {
        static const std::vector<std::string> videoExtensions = {
            ".mp4", ".mov", ".m4v", ".avi", ".mkv", ".webm", ".MP4", ".MOV"
        };

        try {
            for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                for (const auto& videoExt : videoExtensions) {
                    if (ext == videoExt) {
                        return entry.path().string();
                    }
                }
            }
        } catch (...) {}
        return "";
    }

    void process(Context& ctx) override {
        // Step 1: Process video to get current frame
        video_.process(ctx);
        Texture* videoTex = ctx.getInputTexture("out", "");
        if (!videoTex || !videoTex->valid()) {
            return;
        }

        // Resize output to match video
        if (!output_.valid() || output_.width != videoTex->width || output_.height != videoTex->height) {
            output_ = ctx.createTexture(videoTex->width, videoTex->height);
        }

        // Step 2: Generate animated noise texture for displacement map
        noise_.process(ctx);
        Texture* noiseTex = ctx.getInputTexture("out", "");

        // Step 3: Apply displacement using video as source, noise as map
        Context::ShaderParams params;
        params.mode = 0;                      // Luminance mode
        params.param0 = displacementAmount_;  // Displacement strength
        params.vec0X = 1.0f;                  // Direction X
        params.vec0Y = 1.0f;                  // Direction Y

        ctx.runShader("shaders/displacement.wgsl", videoTex, noiseTex, output_, params);

        ctx.setOutput("out", output_);
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    VideoFile video_;
    Noise noise_;
    Texture output_;

    // Displacement strength - tweak for different effects
    float displacementAmount_ = 0.04f;
};

VIVID_OPERATOR(VideoDisplacement)
