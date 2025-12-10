#pragma once

/**
 * @file audio_buffer.h
 * @brief Audio buffer types for the vivid audio system
 *
 * Provides the fundamental audio data structures used by audio operators.
 * All audio in vivid uses interleaved float samples at 48kHz stereo.
 */

#include <cstdint>
#include <vector>

namespace vivid {

/// Standard audio sample rate (48kHz, matches video export)
constexpr uint32_t AUDIO_SAMPLE_RATE = 48000;

/// Standard audio channel count (stereo)
constexpr uint32_t AUDIO_CHANNELS = 2;

/// Standard audio block size (~10.67ms at 48kHz)
constexpr uint32_t AUDIO_BLOCK_SIZE = 512;

/**
 * @brief Non-owning view of audio sample data
 *
 * AudioBuffer is a lightweight struct that points to audio data without
 * owning it. Use OwnedAudioBuffer when you need to allocate storage.
 *
 * Audio format:
 * - Interleaved float samples in range [-1.0, 1.0]
 * - Stereo: [L0, R0, L1, R1, L2, R2, ...]
 * - Mono: [S0, S1, S2, ...]
 *
 * @par Example
 * @code
 * AudioBuffer buf;
 * buf.samples = myData;
 * buf.frameCount = 512;
 * buf.channels = 2;
 * buf.sampleRate = 48000;
 *
 * // Access left channel of frame 10
 * float left = buf.samples[10 * buf.channels + 0];
 * @endcode
 */
struct AudioBuffer {
    float* samples = nullptr;      ///< Interleaved float samples [-1.0, 1.0]
    uint32_t frameCount = 0;       ///< Number of frames (samples per channel)
    uint32_t channels = 2;         ///< Channel count (1=mono, 2=stereo)
    uint32_t sampleRate = 48000;   ///< Sample rate in Hz

    /// @brief Get total sample count (frameCount * channels)
    uint32_t sampleCount() const { return frameCount * channels; }

    /// @brief Get buffer size in bytes
    size_t byteSize() const { return sampleCount() * sizeof(float); }

    /// @brief Check if buffer contains valid data
    bool isValid() const { return samples != nullptr && frameCount > 0; }

    /// @brief Get duration in seconds
    float duration() const {
        return sampleRate > 0 ? static_cast<float>(frameCount) / sampleRate : 0.0f;
    }

    /// @brief Clear all samples to zero
    void clear() {
        if (samples && frameCount > 0) {
            for (uint32_t i = 0; i < sampleCount(); ++i) {
                samples[i] = 0.0f;
            }
        }
    }
};

/**
 * @brief Owning audio buffer with automatic memory management
 *
 * OwnedAudioBuffer extends AudioBuffer with internal storage.
 * Use allocate() to create the buffer, release() to free it.
 *
 * @par Example
 * @code
 * OwnedAudioBuffer output;
 * output.allocate(512, 2, 48000);  // 512 stereo frames at 48kHz
 *
 * // Fill with audio data
 * for (uint32_t i = 0; i < output.sampleCount(); ++i) {
 *     output.samples[i] = generateSample();
 * }
 * @endcode
 */
class OwnedAudioBuffer : public AudioBuffer {
public:
    OwnedAudioBuffer() = default;
    ~OwnedAudioBuffer() { release(); }

    // Non-copyable (to prevent accidental copies of large buffers)
    OwnedAudioBuffer(const OwnedAudioBuffer&) = delete;
    OwnedAudioBuffer& operator=(const OwnedAudioBuffer&) = delete;

    // Movable
    OwnedAudioBuffer(OwnedAudioBuffer&& other) noexcept {
        storage_ = std::move(other.storage_);
        samples = storage_.data();
        frameCount = other.frameCount;
        channels = other.channels;
        sampleRate = other.sampleRate;
        other.samples = nullptr;
        other.frameCount = 0;
    }

    OwnedAudioBuffer& operator=(OwnedAudioBuffer&& other) noexcept {
        if (this != &other) {
            storage_ = std::move(other.storage_);
            samples = storage_.data();
            frameCount = other.frameCount;
            channels = other.channels;
            sampleRate = other.sampleRate;
            other.samples = nullptr;
            other.frameCount = 0;
        }
        return *this;
    }

    /**
     * @brief Allocate buffer storage
     * @param frames Number of frames to allocate
     * @param ch Number of channels (default: stereo)
     * @param rate Sample rate in Hz (default: 48kHz)
     */
    void allocate(uint32_t frames,
                  uint32_t ch = AUDIO_CHANNELS,
                  uint32_t rate = AUDIO_SAMPLE_RATE) {
        frameCount = frames;
        channels = ch;
        sampleRate = rate;
        storage_.resize(frames * ch, 0.0f);
        samples = storage_.data();
    }

    /**
     * @brief Release buffer storage
     */
    void release() {
        storage_.clear();
        storage_.shrink_to_fit();
        samples = nullptr;
        frameCount = 0;
    }

    /**
     * @brief Resize buffer (preserves existing data where possible)
     * @param frames New frame count
     */
    void resize(uint32_t frames) {
        frameCount = frames;
        storage_.resize(frames * channels, 0.0f);
        samples = storage_.data();
    }

private:
    std::vector<float> storage_;
};

} // namespace vivid
