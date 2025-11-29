// Video Displacement Example
// Demonstrates video playback with animated noise displacement
// The noise pattern distorts the video in a fluid, organic way

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
        // Process video to get current frame
        video_.process(ctx);

        // Get video output texture (VideoFile sets "out" key)
        Texture* videoTex = ctx.getInputTexture("out", "");
        if (!videoTex || !videoTex->valid()) {
            return;
        }

        // Resize output to match video
        if (!output_.valid() || output_.width != videoTex->width || output_.height != videoTex->height) {
            output_ = ctx.createTexture(videoTex->width, videoTex->height);
        }

        // Apply displacement shader with animated noise
        // The shader computes simplex noise internally and uses it to distort the video
        Context::ShaderParams params;
        params.param0 = displacementAmount_;  // How much to displace
        params.param1 = noiseScale_;          // Noise pattern size
        params.param2 = noiseSpeed_;          // Animation speed

        ctx.runShader("examples/video-displacement/shaders/video_displacement.wgsl",
                      videoTex, output_, params);

        // Overwrite "out" with our displaced result
        ctx.setOutput("out", output_);
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    VideoFile video_;
    Texture output_;

    // Displacement parameters - tweak these for different effects!
    float displacementAmount_ = 0.04f;   // How much to displace (0.01-0.1 typical)
    float noiseScale_ = 3.0f;            // Noise scale (larger = bigger patterns)
    float noiseSpeed_ = 0.3f;            // Noise animation speed
};

VIVID_OPERATOR(VideoDisplacement)
