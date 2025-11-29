#pragma once
#include <vivid/types.h>
#include <string>
#include <memory>
#include <cstdint>

namespace vivid {

class Context;
class Renderer;

/**
 * @brief Video codec type detection.
 */
enum class VideoCodecType {
    Unknown,
    Standard,   // H.264, H.265, ProRes, VP9, etc. (platform decode)
    HAP,        // HAP (DXT1/BC1)
    HAPAlpha,   // HAP Alpha (DXT5/BC3)
    HAPQ,       // HAP Q (Scaled DXT5)
    HAPQAlpha   // HAP Q Alpha (Scaled DXT5 with alpha)
};

/**
 * @brief Video file metadata.
 */
struct VideoInfo {
    int width = 0;
    int height = 0;
    double duration = 0.0;      // Total duration in seconds
    double frameRate = 0.0;     // Frames per second
    int64_t frameCount = 0;     // Total frame count
    VideoCodecType codecType = VideoCodecType::Unknown;
    bool hasAudio = false;
    std::string codecName;      // Human-readable codec name
};

/**
 * @brief Abstract interface for video loading and playback.
 *
 * Platform-specific implementations:
 * - macOS: AVFoundation (VideoLoaderMacOS)
 * - Windows: Media Foundation (VideoLoaderWindows)
 * - Linux: FFmpeg (VideoLoaderLinux)
 *
 * HAP codec is handled separately via HAPDecoder on all platforms.
 */
class VideoLoader {
public:
    virtual ~VideoLoader() = default;

    // Non-copyable
    VideoLoader(const VideoLoader&) = delete;
    VideoLoader& operator=(const VideoLoader&) = delete;

    /**
     * @brief Open a video file.
     * @param path Path to the video file.
     * @return true if opened successfully.
     */
    virtual bool open(const std::string& path) = 0;

    /**
     * @brief Close the video file and release resources.
     */
    virtual void close() = 0;

    /**
     * @brief Check if a video is currently open.
     */
    virtual bool isOpen() const = 0;

    /**
     * @brief Get video metadata.
     * @return VideoInfo struct with dimensions, duration, etc.
     */
    virtual const VideoInfo& info() const = 0;

    /**
     * @brief Seek to a specific time.
     * @param timeSeconds Time in seconds from start.
     * @return true if seek succeeded.
     */
    virtual bool seek(double timeSeconds) = 0;

    /**
     * @brief Seek to a specific frame number.
     * @param frameNumber Zero-based frame index.
     * @return true if seek succeeded.
     */
    virtual bool seekToFrame(int64_t frameNumber) = 0;

    /**
     * @brief Get the next frame and upload to texture.
     * @param output Texture to receive the frame.
     * @param renderer Renderer for texture operations.
     * @return true if a new frame was decoded and uploaded.
     */
    virtual bool getFrame(Texture& output, Renderer& renderer) = 0;

    /**
     * @brief Get current playback position.
     * @return Current time in seconds.
     */
    virtual double currentTime() const = 0;

    /**
     * @brief Get current frame number.
     * @return Current frame index.
     */
    virtual int64_t currentFrame() const = 0;

    /**
     * @brief Check if this is a HAP-encoded video.
     * HAP videos use GPU decompression and are handled specially.
     */
    bool isHAP() const {
        auto type = info().codecType;
        return type == VideoCodecType::HAP ||
               type == VideoCodecType::HAPAlpha ||
               type == VideoCodecType::HAPQ ||
               type == VideoCodecType::HAPQAlpha;
    }

    /**
     * @brief Check if a file extension is supported.
     * @param path File path to check.
     * @return true if the extension is a known video format.
     */
    static bool isSupported(const std::string& path);

    /**
     * @brief Create a platform-appropriate VideoLoader instance.
     * @return Unique pointer to a VideoLoader implementation.
     *
     * Returns:
     * - VideoLoaderMacOS on macOS (AVFoundation)
     * - VideoLoaderWindows on Windows (Media Foundation)
     * - VideoLoaderLinux on Linux (FFmpeg)
     */
    static std::unique_ptr<VideoLoader> create();

protected:
    VideoLoader() = default;
};

/**
 * @brief Detect video codec type from file.
 * @param path Path to video file.
 * @return Detected codec type, or Unknown if detection fails.
 *
 * This is used to determine whether to use platform decode or HAP path.
 */
VideoCodecType detectVideoCodec(const std::string& path);

} // namespace vivid
