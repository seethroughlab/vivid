#pragma once

#ifdef VIVID_HAS_FFMPEG

#include <string>
#include <vector>
#include <cstdint>

// Forward declarations for FFmpeg types
struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;

// Forward declarations for Diligent types
namespace Diligent {
    struct IRenderDevice;
    struct IDeviceContext;
    struct ITexture;
    struct ITextureView;
}

namespace vivid {
class Context;
}

namespace vivid::video {

/**
 * @brief HAP video decoder using FFmpeg.
 *
 * HAP is a GPU-accelerated video codec that stores DXT compressed data.
 * FFmpeg decodes HAP to standard pixel formats, then we convert to BGRA
 * using swscale and upload to GPU texture.
 */
class HAPDecoder {
public:
    HAPDecoder();
    ~HAPDecoder();

    // Non-copyable
    HAPDecoder(const HAPDecoder&) = delete;
    HAPDecoder& operator=(const HAPDecoder&) = delete;

    /**
     * @brief Check if a file contains HAP codec.
     */
    static bool isHAPFile(const std::string& path);

    /**
     * @brief Open a HAP video file.
     */
    bool open(Context& ctx, const std::string& path, bool loop = false);

    /**
     * @brief Close and release resources.
     */
    void close();

    /**
     * @brief Check if open.
     */
    bool isOpen() const;

    /**
     * @brief Update - decode next frame and upload to texture.
     */
    void update(Context& ctx);

    /**
     * @brief Seek to time.
     */
    void seek(float seconds);

    /**
     * @brief Pause playback.
     */
    void pause() { isPlaying_ = false; }

    /**
     * @brief Resume playback.
     */
    void play() { if (!isFinished_) isPlaying_ = true; }

    /**
     * @brief Check if playing.
     */
    bool isPlaying() const { return isPlaying_; }

    /**
     * @brief Check if finished.
     */
    bool isFinished() const { return isFinished_; }

    /**
     * @brief Get current time in seconds.
     */
    float currentTime() const { return currentTime_; }

    /**
     * @brief Get duration in seconds.
     */
    float duration() const { return duration_; }

    /**
     * @brief Get video width.
     */
    int width() const { return width_; }

    /**
     * @brief Get video height.
     */
    int height() const { return height_; }

    /**
     * @brief Get frame rate.
     */
    float frameRate() const { return frameRate_; }

    /**
     * @brief Get texture.
     */
    Diligent::ITexture* texture() const;

    /**
     * @brief Get texture view.
     */
    Diligent::ITextureView* textureView() const;

private:
    bool decodeFrame();
    void uploadFrame();

    // FFmpeg state
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* codecCtx_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    int videoStreamIndex_ = -1;
    double timeBase_ = 0.0;

    // Video info
    int width_ = 0;
    int height_ = 0;
    float duration_ = 0.0f;
    float frameRate_ = 30.0f;

    // Playback state
    bool isPlaying_ = false;
    bool isFinished_ = false;
    bool isLooping_ = false;
    float currentTime_ = 0.0f;
    float playbackTime_ = 0.0f;
    float nextFrameTime_ = 0.0f;
    std::string filePath_;

    // Pixel buffer for BGRA conversion
    std::vector<uint8_t> pixelBuffer_;

    // GPU resources
    Diligent::IRenderDevice* device_ = nullptr;
    Diligent::IDeviceContext* context_ = nullptr;
    Diligent::ITexture* texture_ = nullptr;
    Diligent::ITextureView* srv_ = nullptr;
};

} // namespace vivid::video

#endif // VIVID_HAS_FFMPEG
