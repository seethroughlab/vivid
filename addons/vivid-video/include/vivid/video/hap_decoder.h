#pragma once

#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <mutex>

namespace vivid {
class Context;
}

namespace vivid::video {

class AudioPlayer;

/**
 * @brief HAP video decoder using Vidvox HAP library.
 *
 * Uses AVFoundation to demux the MOV container and extract raw HAP frame data,
 * then uses the Vidvox HAP library to decompress to DXT-compressed textures.
 * DXT data is uploaded directly to GPU as BC1/BC3 compressed textures,
 * avoiding CPU pixel conversion entirely.
 */
class HAPDecoder {
public:
    HAPDecoder();
    ~HAPDecoder();

    // Non-copyable
    HAPDecoder(const HAPDecoder&) = delete;
    HAPDecoder& operator=(const HAPDecoder&) = delete;

    /**
     * @brief Check if a file is a HAP-encoded video.
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
    bool hasAudio() const { return hasAudio_; }

    /**
     * @brief Set audio volume (0.0 - 1.0).
     */
    void setVolume(float volume);

    /**
     * @brief Get audio volume.
     */
    float getVolume() const;

    /**
     * @brief Read audio samples into buffer (for external audio routing).
     * @param buffer Output buffer for interleaved float samples
     * @param maxFrames Maximum frames to read
     * @return Number of frames actually read
     *
     * This reads audio samples that would otherwise go to internal playback.
     * Use this when you want to route audio through the chain's audio system.
     */
    uint32_t readAudioSamples(float* buffer, uint32_t maxFrames);

    /**
     * @brief Read audio samples synchronized to a video PTS.
     * @param buffer Output buffer for interleaved float samples
     * @param videoPTS Target video presentation timestamp in seconds
     * @param maxFrames Maximum frames to read
     * @return Number of frames actually read
     *
     * This reads audio samples that correspond to the given video time.
     * Used for PTS-based audio/video synchronization during recording.
     */
    uint32_t readAudioSamplesForPTS(float* buffer, double videoPTS, uint32_t maxFrames);

    /**
     * @brief Get the PTS of the oldest audio sample in the buffer.
     */
    double audioAvailableStartPTS() const;

    /**
     * @brief Get the PTS of the newest audio sample in the buffer.
     */
    double audioAvailableEndPTS() const;

    /**
     * @brief Enable/disable internal audio playback.
     * @param enable If false, audio is not played through internal AudioPlayer
     *
     * Set to false when using readAudioSamples() for external audio routing.
     */
    void setInternalAudioEnabled(bool enable);

    /**
     * @brief Check if internal audio is enabled.
     */
    bool isInternalAudioEnabled() const { return internalAudioEnabled_; }

    /**
     * @brief Get audio sample rate.
     */
    uint32_t audioSampleRate() const { return audioSampleRate_; }

    /**
     * @brief Get audio channel count.
     */
    uint32_t audioChannels() const { return audioChannels_; }

    /**
     * @brief Get texture.
     */
    WGPUTexture texture() const { return texture_; }

    /**
     * @brief Get texture view.
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

    // Audio info
    uint32_t audioSampleRate_ = 48000;
    uint32_t audioChannels_ = 2;

    // DXT buffer for decoded frames
    std::vector<uint8_t> dxtBuffer_;

    // GPU resources
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;
    WGPUTextureFormat textureFormat_ = WGPUTextureFormat_Undefined;

    // Audio player (for internal playback)
    std::unique_ptr<AudioPlayer> audioPlayer_;

    // Audio ring buffer for external reading (VideoAudio)
    std::vector<float> audioRingBuffer_;
    uint32_t audioWritePos_ = 0;
    uint32_t audioReadPos_ = 0;
    mutable std::mutex audioMutex_;
    static constexpr uint32_t AUDIO_RING_SIZE = 48000 * 2;  // 1 second stereo

    // Audio PTS tracking for sync
    double audioStartPTS_ = 0.0;   // PTS of first sample in ring buffer (at readPos)
    double audioEndPTS_ = 0.0;     // PTS of last sample in ring buffer (at writePos)
    static constexpr double AUDIO_SAMPLE_RATE_D = 48000.0;
    static constexpr uint32_t AUDIO_CHANNELS = 2;

    // Audio loop tracking
    bool audioNeedsLoop_ = false;  // True when audio EOF reached and needs to loop

    void prebufferAudio();
    void createTexture();
    void resetReader();
    void feedAudioBuffer();  // Read from AVAssetReader into ring buffer
    void loopAudioReader();  // Reset audio reader to beginning for looping
};

} // namespace vivid::video
