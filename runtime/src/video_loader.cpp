#include "video_loader.h"
#ifdef VIVID_HAS_FFMPEG
#include "hap_decoder.h"
#endif
#include <algorithm>
#include <cctype>
#include <iostream>

namespace vivid {

#ifdef VIVID_HAS_FFMPEG
/**
 * @brief VideoLoader implementation that wraps HAPDecoder.
 */
class VideoLoaderHAP : public VideoLoader {
public:
    bool open(const std::string& path) override {
        return decoder_.open(path);
    }

    void close() override {
        decoder_.close();
    }

    bool isOpen() const override {
        return decoder_.isOpen();
    }

    const VideoInfo& info() const override {
        return decoder_.info();
    }

    bool seek(double timeSeconds) override {
        return decoder_.seek(timeSeconds);
    }

    bool seekToFrame(int64_t frameNumber) override {
        double time = frameNumber / decoder_.info().frameRate;
        return decoder_.seek(time);
    }

    bool getFrame(Texture& output, Renderer& renderer) override {
        return decoder_.getFrame(output, renderer);
    }

    double currentTime() const override {
        return decoder_.currentTime();
    }

    int64_t currentFrame() const override {
        return static_cast<int64_t>(decoder_.currentTime() * decoder_.info().frameRate);
    }

private:
    HAPDecoder decoder_;
};
#endif

bool VideoLoader::isSupported(const std::string& path) {
    // Get file extension
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) {
        return false;
    }

    std::string ext = path.substr(dotPos + 1);
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Common video formats
    return ext == "mp4" || ext == "mov" || ext == "m4v" ||
           ext == "avi" || ext == "mkv" || ext == "webm" ||
           ext == "wmv" || ext == "flv" || ext == "mxf" ||
           ext == "ts" || ext == "mts" || ext == "m2ts";
}

// Platform-specific factory implementation
// Each platform will define its own version in video_loader_<platform>.cpp
#if defined(__APPLE__)
    // Forward declare macOS implementation
    std::unique_ptr<VideoLoader> createVideoLoaderMacOS();

    std::unique_ptr<VideoLoader> VideoLoader::create() {
        return createVideoLoaderMacOS();
    }
#elif defined(_WIN32)
    // Forward declare Windows implementation
    std::unique_ptr<VideoLoader> createVideoLoaderWindows();

    std::unique_ptr<VideoLoader> VideoLoader::create() {
        return createVideoLoaderWindows();
    }
#else
    // Forward declare Linux/FFmpeg implementation
    std::unique_ptr<VideoLoader> createVideoLoaderLinux();

    std::unique_ptr<VideoLoader> VideoLoader::create() {
        return createVideoLoaderLinux();
    }
#endif

VideoCodecType detectVideoCodec(const std::string& path) {
    // Quick check: if file doesn't exist or isn't a video, return Unknown
    if (!VideoLoader::isSupported(path)) {
        return VideoCodecType::Unknown;
    }

#ifdef VIVID_HAS_FFMPEG
    // Check if it's a HAP file
    if (HAPDecoder::isHAPFile(path)) {
        // We detected it's HAP, but don't know the exact variant yet
        // The HAPDecoder will determine the specific type when opened
        return VideoCodecType::HAP;
    }
#endif

    // Default to Standard for platform-native decoding
    return VideoCodecType::Standard;
}

// Factory that checks for HAP first
std::unique_ptr<VideoLoader> createVideoLoaderForPath(const std::string& path) {
#ifdef VIVID_HAS_FFMPEG
    // Check if it's a HAP file
    if (HAPDecoder::isHAPFile(path)) {
        std::cout << "[VideoLoader] Detected HAP codec, using HAPDecoder\n";
        return std::make_unique<VideoLoaderHAP>();
    }
#endif
    // Fall back to platform-native loader
    return VideoLoader::create();
}

} // namespace vivid
