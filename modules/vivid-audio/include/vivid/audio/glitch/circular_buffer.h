#pragma once

/**
 * @file circular_buffer.h
 * @brief Circular audio buffer for glitch effects
 *
 * Provides continuous recording with interpolated playback for effects like
 * beat repeat, reverse, scratch, and stutter.
 */

#include <vivid/audio_buffer.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

/**
 * @brief Circular audio buffer with interpolated read
 *
 * Continuously records audio into a ring buffer while allowing interpolated
 * playback at arbitrary positions, including reverse playback.
 *
 * @par Example
 * @code
 * CircularAudioBuffer buffer(24.0f);  // 24 seconds capacity
 *
 * // In generateBlock():
 * // Always write incoming audio
 * buffer.write(inputL, inputR);
 *
 * // Read back with interpolation
 * float L, R;
 * buffer.read(0.5f, L, R);  // Read at 50% through buffer
 * @endcode
 */
class CircularAudioBuffer {
public:
    /**
     * @brief Construct buffer with given duration
     * @param seconds Buffer duration in seconds
     * @param sampleRate Sample rate (default 48kHz)
     */
    explicit CircularAudioBuffer(float seconds = 24.0f, uint32_t sampleRate = AUDIO_SAMPLE_RATE)
        : m_sampleRate(sampleRate)
    {
        resize(seconds);
    }

    /**
     * @brief Resize buffer to given duration
     * @param seconds New duration in seconds
     */
    void resize(float seconds) {
        m_capacity = static_cast<size_t>(seconds * m_sampleRate);
        m_buffer.resize(m_capacity * 2, 0.0f);  // Stereo interleaved
        m_writePos = 0;
    }

    /**
     * @brief Get buffer capacity in frames
     */
    size_t capacity() const { return m_capacity; }

    /**
     * @brief Get buffer duration in seconds
     */
    float duration() const { return static_cast<float>(m_capacity) / m_sampleRate; }

    /**
     * @brief Get buffer duration in beats at given BPM
     */
    float durationBeats(float bpm) const {
        return duration() * bpm / 60.0f;
    }

    /**
     * @brief Write a stereo sample to the buffer
     * @param left Left channel sample
     * @param right Right channel sample
     *
     * Advances the write position circularly.
     */
    void write(float left, float right) {
        m_buffer[m_writePos * 2] = left;
        m_buffer[m_writePos * 2 + 1] = right;
        m_writePos = (m_writePos + 1) % m_capacity;
    }

    /**
     * @brief Write a block of stereo samples
     * @param input Interleaved stereo samples
     * @param frames Number of frames to write
     */
    void writeBlock(const float* input, uint32_t frames) {
        for (uint32_t i = 0; i < frames; ++i) {
            write(input[i * 2], input[i * 2 + 1]);
        }
    }

    /**
     * @brief Get current write position (frames from start)
     */
    size_t writePosition() const { return m_writePos; }

    /**
     * @brief Read with linear interpolation at fractional frame position
     * @param framePos Frame position (can be fractional)
     * @param[out] left Left channel output
     * @param[out] right Right channel output
     *
     * Position is relative to start of buffer (not write position).
     * Use getReadPosition() to convert from relative positions.
     */
    void read(double framePos, float& left, float& right) const {
        // Wrap to buffer bounds
        while (framePos < 0) framePos += m_capacity;
        while (framePos >= m_capacity) framePos -= m_capacity;

        size_t idx0 = static_cast<size_t>(framePos) % m_capacity;
        size_t idx1 = (idx0 + 1) % m_capacity;
        float frac = static_cast<float>(framePos - std::floor(framePos));

        // Linear interpolation
        float l0 = m_buffer[idx0 * 2];
        float r0 = m_buffer[idx0 * 2 + 1];
        float l1 = m_buffer[idx1 * 2];
        float r1 = m_buffer[idx1 * 2 + 1];

        left = l0 + frac * (l1 - l0);
        right = r0 + frac * (r1 - r0);
    }

    /**
     * @brief Read backwards with linear interpolation
     * @param framePos Frame position (reads backwards from here)
     * @param[out] left Left channel output
     * @param[out] right Right channel output
     */
    void readReverse(double framePos, float& left, float& right) const {
        // Wrap to buffer bounds
        while (framePos < 0) framePos += m_capacity;
        while (framePos >= m_capacity) framePos -= m_capacity;

        size_t idx0 = static_cast<size_t>(framePos) % m_capacity;
        size_t idx1 = (idx0 == 0) ? (m_capacity - 1) : (idx0 - 1);
        float frac = static_cast<float>(framePos - std::floor(framePos));

        // Linear interpolation (reversed direction)
        float l0 = m_buffer[idx0 * 2];
        float r0 = m_buffer[idx0 * 2 + 1];
        float l1 = m_buffer[idx1 * 2];
        float r1 = m_buffer[idx1 * 2 + 1];

        left = l0 + frac * (l1 - l0);
        right = r0 + frac * (r1 - r0);
    }

    /**
     * @brief Get absolute frame position relative to current write position
     * @param framesBack Number of frames before current write position
     * @return Absolute frame position in buffer
     *
     * Use this to get the start position for a slice that was just written.
     */
    size_t getReadPosition(size_t framesBack) const {
        if (framesBack >= m_capacity) framesBack = m_capacity - 1;
        return (m_writePos + m_capacity - framesBack) % m_capacity;
    }

    /**
     * @brief Clear the buffer to silence
     */
    void clear() {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
        m_writePos = 0;
    }

    /**
     * @brief Get sample rate
     */
    uint32_t sampleRate() const { return m_sampleRate; }

private:
    std::vector<float> m_buffer;  // Interleaved stereo
    size_t m_capacity = 0;        // Capacity in frames
    size_t m_writePos = 0;        // Current write position in frames
    uint32_t m_sampleRate;
};

} // namespace vivid::audio
