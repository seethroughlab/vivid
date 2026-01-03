#pragma once

/**
 * @file delay_line.h
 * @brief Circular buffer delay line for audio processing
 *
 * DelayLine provides sample-accurate delay with optional interpolation
 * for fractional delay times. Used by Delay, Echo, Reverb, Chorus, Flanger.
 */

#include <vector>
#include <cstdint>
#include <cmath>

namespace vivid::audio::dsp {

/**
 * @brief Circular buffer delay line with interpolation
 *
 * Provides efficient delay of audio samples with:
 * - Integer sample delays (fast)
 * - Fractional delays with linear interpolation
 * - Thread-safe single-writer, single-reader design
 */
class DelayLine {
public:
    DelayLine() = default;

    /**
     * @brief Initialize delay line with maximum delay
     * @param maxDelaySamples Maximum delay in samples
     */
    void init(uint32_t maxDelaySamples) {
        m_buffer.resize(maxDelaySamples + 1, 0.0f);
        m_writePos = 0;
        m_size = static_cast<uint32_t>(m_buffer.size());
    }

    /**
     * @brief Clear the delay line to silence
     */
    void clear() {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
    }

    /**
     * @brief Write a sample into the delay line
     * @param sample Input sample value
     */
    void write(float sample) {
        m_buffer[m_writePos] = sample;
        m_writePos = (m_writePos + 1) % m_size;
    }

    /**
     * @brief Read a sample at integer delay
     * @param delaySamples Delay in samples (must be <= max delay)
     * @return Delayed sample
     */
    float read(uint32_t delaySamples) const {
        uint32_t readPos = (m_writePos + m_size - delaySamples - 1) % m_size;
        return m_buffer[readPos];
    }

    /**
     * @brief Read a sample at fractional delay with linear interpolation
     * @param delaySamples Fractional delay in samples
     * @return Interpolated delayed sample
     */
    float readInterpolated(float delaySamples) const {
        float intPart;
        float frac = std::modf(delaySamples, &intPart);
        uint32_t delay1 = static_cast<uint32_t>(intPart);
        uint32_t delay2 = delay1 + 1;

        float sample1 = read(delay1);
        float sample2 = read(delay2);

        return sample1 + frac * (sample2 - sample1);
    }

    /**
     * @brief Write and read in one operation (for feedback loops)
     * @param sample Input sample
     * @param delaySamples Delay in samples
     * @return Delayed sample (before writing new sample)
     */
    float process(float sample, uint32_t delaySamples) {
        float output = read(delaySamples);
        write(sample);
        return output;
    }

    /**
     * @brief Get maximum delay in samples
     */
    uint32_t maxDelay() const { return m_size > 0 ? m_size - 1 : 0; }

private:
    std::vector<float> m_buffer;
    uint32_t m_writePos = 0;
    uint32_t m_size = 0;
};

/**
 * @brief Stereo delay line (two channels)
 */
class StereoDelayLine {
public:
    void init(uint32_t maxDelaySamples) {
        m_left.init(maxDelaySamples);
        m_right.init(maxDelaySamples);
    }

    void clear() {
        m_left.clear();
        m_right.clear();
    }

    void write(float left, float right) {
        m_left.write(left);
        m_right.write(right);
    }

    void read(uint32_t delaySamples, float& left, float& right) const {
        left = m_left.read(delaySamples);
        right = m_right.read(delaySamples);
    }

    void readInterpolated(float delaySamples, float& left, float& right) const {
        left = m_left.readInterpolated(delaySamples);
        right = m_right.readInterpolated(delaySamples);
    }

    DelayLine& left() { return m_left; }
    DelayLine& right() { return m_right; }

private:
    DelayLine m_left;
    DelayLine m_right;
};

} // namespace vivid::audio::dsp
