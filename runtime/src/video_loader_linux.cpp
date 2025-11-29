#include "video_loader.h"
#include "renderer.h"
#include <iostream>

#if !defined(__APPLE__) && !defined(_WIN32)

// FFmpeg headers would go here:
// extern "C" {
// #include <libavformat/avformat.h>
// #include <libavcodec/avcodec.h>
// #include <libswscale/swscale.h>
// }

namespace vivid {

/**
 * @brief Linux video loader using FFmpeg.
 *
 * Stub implementation - will be completed in phase 12.2f.
 */
class VideoLoaderLinux : public VideoLoader {
public:
    VideoLoaderLinux() = default;
    ~VideoLoaderLinux() override { close(); }

    bool open(const std::string& path) override {
        std::cerr << "[VideoLoaderLinux] Not yet implemented\n";
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

std::unique_ptr<VideoLoader> createVideoLoaderLinux() {
    return std::make_unique<VideoLoaderLinux>();
}

} // namespace vivid

#endif // Linux
