#pragma once

#ifdef VIVID_HAS_FFMPEG

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

// Forward declarations for FFmpeg types
struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;
struct SwrContext;

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

class AudioPlayer;

/**
 * @brief General-purpose video decoder using FFmpeg.
 *
 * Decodes video formats that AVFoundation cannot handle properly,
 * including HAP (DXT compressed) and HEVC (which returns NULL image buffers).
 * FFmpeg decodes to standard pixel formats, then we convert to BGRA
 * using swscale and upload to GPU texture.
 *
 * Also decodes audio and plays it via AudioPlayer.
 */
class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    // Non-copyable
    FFmpegDecoder(const FFmpegDecoder&) = delete;
    FFmpegDecoder& operator=(const FFmpegDecoder&) = delete;

    /**
     * @brief Check if a file needs FFmpeg decoder (HAP, HEVC, etc).
     * Some codecs work poorly with AVFoundation and need FFmpeg.
     */
    static bool needsFFmpegDecoder(const std::string& path);

    /**
     * @brief Open a video file.
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
    void pause();

    /**
     * @brief Resume playback.
     */
    void play();

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
     * @brief Check if file has audio.
     */
    bool hasAudio() const { return audioStreamIndex_ >= 0; }

    /**
     * @brief Set audio volume (0.0 - 1.0).
     */
    void setVolume(float volume);

    /**
     * @brief Get audio volume.
     */
    float getVolume() const;

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
    bool decodeAudioFrame();
    void processAudioPacket();

    // FFmpeg state - Video
    AVFormatContext* formatCtx_ = nullptr;
    AVCodecContext* videoCodecCtx_ = nullptr;
    AVPacket* packet_ = nullptr;
    AVFrame* frame_ = nullptr;
    SwsContext* swsCtx_ = nullptr;
    int videoStreamIndex_ = -1;
    double videoTimeBase_ = 0.0;

    // FFmpeg state - Audio
    AVCodecContext* audioCodecCtx_ = nullptr;
    AVFrame* audioFrame_ = nullptr;
    SwrContext* swrCtx_ = nullptr;
    int audioStreamIndex_ = -1;
    double audioTimeBase_ = 0.0;
    int audioSampleRate_ = 0;
    int audioChannels_ = 0;

    // Audio player
    std::unique_ptr<AudioPlayer> audioPlayer_;
    std::vector<float> audioBuffer_;

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

// Alias for backward compatibility
using HAPDecoder = FFmpegDecoder;

} // namespace vivid::video

#endif // VIVID_HAS_FFMPEG
