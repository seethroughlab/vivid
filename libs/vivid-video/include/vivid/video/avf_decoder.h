#pragma once

#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace vivid {
class Context;
}

namespace vivid::video {

class AudioPlayer;

/**
 * @brief AVFoundation video decoder for standard codecs (H.264, MPEG2, ProRes, etc.)
 *
 * Uses AVFoundation to decode video frames to BGRA pixels, then uploads
 * to a GPU texture. Works with any codec that macOS supports natively.
 */
class AVFDecoder {
public:
    AVFDecoder();
    ~AVFDecoder();

    // Non-copyable
    AVFDecoder(const AVFDecoder&) = delete;
    AVFDecoder& operator=(const AVFDecoder&) = delete;

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

    bool isPlaying() const { return isPlaying_; }
    bool isFinished() const { return isFinished_; }
    float currentTime() const { return currentTime_; }
    float duration() const { return duration_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float frameRate() const { return frameRate_; }
    bool hasAudio() const { return hasAudio_; }

    void setVolume(float volume);
    float getVolume() const;

    /**
     * @brief Read audio samples into buffer (for external audio routing).
     * @param buffer Output buffer for interleaved float samples
     * @param maxFrames Maximum frames to read
     * @return Number of frames actually read
     */
    uint32_t readAudioSamples(float* buffer, uint32_t maxFrames);

    /**
     * @brief Enable/disable internal audio playback.
     */
    void setInternalAudioEnabled(bool enable);

    /**
     * @brief Check if internal audio is enabled.
     */
    bool isInternalAudioEnabled() const { return internalAudioEnabled_; }

    /**
     * @brief Get audio sample rate.
     */
    uint32_t audioSampleRate() const { return 48000; }

    /**
     * @brief Get audio channel count.
     */
    uint32_t audioChannels() const { return 2; }

    WGPUTexture texture() const { return texture_; }
    WGPUTextureView textureView() const { return textureView_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Video info
    int width_ = 0;
    int height_ = 0;
    float duration_ = 0.0f;
    float frameRate_ = 30.0f;

    // Playback state
    bool isPlaying_ = false;
    bool isFinished_ = false;
    bool isLooping_ = false;
    bool hasAudio_ = false;
    bool internalAudioEnabled_ = true;
    float currentTime_ = 0.0f;
    float playbackTime_ = 0.0f;
    float nextFrameTime_ = 0.0f;
    std::string filePath_;

    // Pixel buffer for decoded frames
    std::vector<uint8_t> pixelBuffer_;

    // GPU resources
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;

    // Audio player
    std::unique_ptr<AudioPlayer> audioPlayer_;

    void createTexture();
    void prebufferAudio();
};

} // namespace vivid::video
