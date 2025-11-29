// Video Playback Example
// Demonstrates the built-in VideoFile operator with multiple videos
// Press SPACE to switch between videos

#include <vivid/vivid.h>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;
using namespace vivid;

class VideoChain : public Operator {
public:
    void init(Context& ctx) override {
        // Find all video files in the assets folder
        videoPaths_ = findVideoFiles("examples/video-playback/assets");

        if (videoPaths_.empty()) {
            std::cerr << "[VideoChain] No video files found in assets folder\n";
            return;
        }

        std::cout << "[VideoChain] Found " << videoPaths_.size() << " video(s):\n";
        for (const auto& path : videoPaths_) {
            std::cout << "  - " << path << "\n";
        }

        currentIndex_ = 0;
        loadCurrentVideo();
    }

    static std::vector<std::string> findVideoFiles(const std::string& directory) {
        std::vector<std::string> videos;

        // Video extensions to look for
        static const std::vector<std::string> videoExtensions = {
            ".mp4", ".mov", ".m4v", ".avi", ".mkv", ".webm", ".MP4", ".MOV"
        };

        try {
            for (const auto& entry : fs::directory_iterator(directory)) {
                if (!entry.is_regular_file()) continue;

                std::string ext = entry.path().extension().string();
                for (const auto& videoExt : videoExtensions) {
                    if (ext == videoExt) {
                        videos.push_back(entry.path().string());
                        break;
                    }
                }
            }

            // Sort alphabetically for consistent ordering
            std::sort(videos.begin(), videos.end());
        } catch (const std::exception& e) {
            std::cerr << "[VideoChain] Error scanning directory: " << e.what() << "\n";
        }

        return videos;
    }

    void process(Context& ctx) override {
        if (videoPaths_.empty()) return;

        // Switch video on spacebar press
        if (ctx.wasKeyPressed(Key::Space)) {
            currentIndex_ = (currentIndex_ + 1) % videoPaths_.size();
            loadCurrentVideo();
            std::cout << "[VideoChain] Switched to: " << videoPaths_[currentIndex_] << "\n";
        }

        video_.process(ctx);
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    void loadCurrentVideo() {
        video_.path(videoPaths_[currentIndex_])
              .loop(true)
              .speed(1.0f)
              .play();
    }

    VideoFile video_;
    std::vector<std::string> videoPaths_;
    size_t currentIndex_ = 0;
};

VIVID_OPERATOR(VideoChain)
