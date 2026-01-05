#pragma once

/**
 * @file avf_playback_decoder.h
 * @brief AVFoundation-based video playback using AVPlayer
 *
 * This decoder uses AVPlayer + AVPlayerLooper for proper A/V sync and looping.
 * Unlike AVAssetReader (designed for offline processing), AVPlayer handles
 * real-time playback with OS-level audio/video synchronization.
 *
 * Video frames are extracted via AVPlayerItemVideoOutput and uploaded to
 * a WebGPU texture for rendering in the chain.
 */

#include <webgpu/webgpu.h>
#include <string>
#include <memory>
#include <vector>

namespace vivid {
class Context;
}

namespace vivid::video {

/**
 * @brief Video decoder using AVPlayer for synchronized playback
 *
 * Key features:
 * - OS-level A/V synchronization (no manual sync code needed)
 * - Seamless looping via AVPlayerLooper
 * - Audio plays through system speakers automatically
 * - Video frames extracted via AVPlayerItemVideoOutput
 */
class AVFPlaybackDecoder {
public:
    AVFPlaybackDecoder();
    ~AVFPlaybackDecoder();

    // Non-copyable
    AVFPlaybackDecoder(const AVFPlaybackDecoder&) = delete;
    AVFPlaybackDecoder& operator=(const AVFPlaybackDecoder&) = delete;

    /**
     * @brief Open a video file for playback
     * @param ctx Vivid context for WebGPU resources
     * @param path Path to video file
     * @param loop Enable seamless looping
     * @return true if successful
     */
    bool open(Context& ctx, const std::string& path, bool loop = false);

    /**
     * @brief Close and release resources
     */
    void close();

    /**
     * @brief Check if video is open
     */
    bool isOpen() const;

    /**
     * @brief Update - check for new frame and upload to texture
     * Call this every frame from the render loop.
     */
    void update(Context& ctx);

    /**
     * @brief Seek to specific time
     * @param seconds Time in seconds
     */
    void seek(float seconds);

    /**
     * @brief Start/resume playback
     */
    void play();

    /**
     * @brief Pause playback
     */
    void pause();

    /**
     * @brief Check if currently playing
     */
    bool isPlaying() const;

    /**
     * @brief Check if playback finished (only relevant if not looping)
     */
    bool isFinished() const;

    /**
     * @brief Get current playback time in seconds
     */
    float currentTime() const;

    /**
     * @brief Get video duration in seconds
     */
    float duration() const;

    /**
     * @brief Get video width in pixels
     */
    int width() const { return width_; }

    /**
     * @brief Get video height in pixels
     */
    int height() const { return height_; }

    /**
     * @brief Get video frame rate
     */
    float frameRate() const { return frameRate_; }

    /**
     * @brief Check if video has audio track
     */
    bool hasAudio() const { return hasAudio_; }

    /**
     * @brief Set audio volume (0.0 to 1.0) - controls internal playback volume
     */
    void setVolume(float volume);

    /**
     * @brief Get current volume
     */
    float getVolume() const;

    /**
     * @brief Read audio samples for external routing
     * @param buffer Output buffer for interleaved float samples
     * @param maxFrames Maximum frames to read
     * @return Number of frames actually read
     */
    uint32_t readAudioSamples(float* buffer, uint32_t maxFrames);

    /**
     * @brief Enable/disable internal audio playback via AVPlayer
     * @param enable If false, audio only goes through readAudioSamples()
     */
    void setInternalAudioEnabled(bool enable);

    /**
     * @brief Check if internal audio playback is enabled
     */
    bool isInternalAudioEnabled() const;

    /**
     * @brief Get audio sample rate
     */
    uint32_t audioSampleRate() const { return audioSampleRate_; }

    /**
     * @brief Get audio channel count
     */
    uint32_t audioChannels() const { return audioChannels_; }

    /**
     * @brief Get the WebGPU texture containing the current frame
     */
    WGPUTexture texture() const { return texture_; }

    /**
     * @brief Get the WebGPU texture view
     */
    WGPUTextureView textureView() const { return textureView_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Video info
    int width_ = 0;
    int height_ = 0;
    float duration_ = 0.0f;
    float frameRate_ = 30.0f;
    bool hasAudio_ = false;

    // Audio info
    uint32_t audioSampleRate_ = 48000;
    uint32_t audioChannels_ = 2;
    bool internalAudioEnabled_ = true;

    // GPU resources
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;

    // Pixel buffer for CPU->GPU transfer
    std::vector<uint8_t> pixelBuffer_;

    void createTexture();
    void uploadFrame(const uint8_t* pixels, size_t bytesPerRow);
};

} // namespace vivid::video
