#include "video_loader.h"
#include "renderer.h"
#include <iostream>

#if defined(_WIN32)

// Windows Media Foundation headers would go here:
// #include <mfapi.h>
// #include <mfidl.h>
// #include <mfreadwrite.h>

namespace vivid {

/**
 * @brief Windows video loader using Media Foundation.
 *
 * Stub implementation - will be completed in phase 12.2e.
 */
class VideoLoaderWindows : public VideoLoader {
public:
    VideoLoaderWindows() = default;
    ~VideoLoaderWindows() override { close(); }

    bool open(const std::string& path) override {
        std::cerr << "[VideoLoaderWindows] Not yet implemented\n";
        return false;
    }

    void close() override {
        isOpen_ = false;
        info_ = VideoInfo{};
    }

    bool isOpen() const override { return isOpen_; }
    const VideoInfo& info() const override { return info_; }

    bool seek(double timeSeconds) override { return false; }
    bool seekToFrame(int64_t frameNumber) override { return false; }

    bool getFrame(Texture& output, Renderer& renderer) override {
        return false;
    }

    double currentTime() const override { return 0; }
    int64_t currentFrame() const override { return 0; }

private:
    VideoInfo info_;
    bool isOpen_ = false;
};

std::unique_ptr<VideoLoader> createVideoLoaderWindows() {
    return std::make_unique<VideoLoaderWindows>();
}

} // namespace vivid

#endif // _WIN32
