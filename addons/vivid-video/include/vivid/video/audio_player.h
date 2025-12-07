#pragma once

#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdint>

namespace vivid::video {

/**
 * @brief Audio playback using miniaudio.
 *
 * Provides a simple interface for playing audio samples decoded from video.
 * Uses a ring buffer to allow the video decoder to push samples while
 * the audio device pulls them asynchronously.
 */
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    // Non-copyable
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    /**
     * @brief Initialize audio device for playback.
     * @param sampleRate Sample rate in Hz (e.g., 44100, 48000).
     * @param channels Number of channels (1 for mono, 2 for stereo).
     * @return true if initialization succeeded.
     */
    bool init(uint32_t sampleRate, uint32_t channels);

    /**
     * @brief Shutdown audio device and release resources.
     */
    void shutdown();

    /**
     * @brief Check if audio player is initialized.
     */
    bool isInitialized() const { return initialized_; }

    /**
     * @brief Start audio playback.
     */
    void play();

    /**
     * @brief Pause audio playback.
     */
    void pause();

    /**
     * @brief Check if currently playing.
     */
    bool isPlaying() const { return playing_; }

    /**
     * @brief Clear the audio buffer and reset position.
     * Call this when seeking in video.
     */
    void flush();

    /**
     * @brief Push audio samples to the playback buffer.
     * @param samples Interleaved float samples (range -1.0 to 1.0).
     * @param frameCount Number of frames (samples / channels).
     */
    void pushSamples(const float* samples, uint32_t frameCount);

    /**
     * @brief Get the current playback position in seconds.
     */
    double getPlaybackPosition() const;

    /**
     * @brief Get number of buffered frames available for playback.
     */
    uint32_t getBufferedFrames() const;

    /**
     * @brief Set the playback volume (0.0 to 1.0).
     */
    void setVolume(float volume);

    /**
     * @brief Get current volume.
     */
    float getVolume() const { return volume_; }

private:
    static void dataCallback(struct ma_device* device, void* output, const void* input, unsigned int frameCount);
    void fillBuffer(float* output, uint32_t frameCount);

    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Ring buffer for audio samples
    std::vector<float> ringBuffer_;
    std::atomic<uint32_t> writePos_{0};
    std::atomic<uint32_t> readPos_{0};
    uint32_t bufferSize_ = 0;
    mutable std::mutex bufferMutex_;

    uint32_t sampleRate_ = 0;
    uint32_t channels_ = 0;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> playing_{false};
    std::atomic<float> volume_{1.0f};

    std::atomic<uint64_t> samplesPlayed_{0};

    static constexpr uint32_t BUFFER_FRAMES = 48000;  // ~1 second at 48kHz
};

} // namespace vivid::video
