#pragma once

/**
 * @file filters.h
 * @brief Audio filter primitives for effects processing
 *
 * Provides basic filter building blocks:
 * - OnePole: Simple lowpass/highpass
 * - AllPass: For phaser and reverb
 * - Comb: For reverb
 */

#include <cmath>
#include <cstdint>

namespace vivid::audio::dsp {

/**
 * @brief Simple one-pole lowpass/highpass filter
 *
 * First-order IIR filter with a single coefficient.
 * Used for smoothing, damping, and simple filtering.
 */
class OnePoleFilter {
public:
    OnePoleFilter() = default;

    /**
     * @brief Initialize as lowpass filter
     * @param sampleRate Audio sample rate
     * @param cutoffHz Cutoff frequency in Hz
     */
    void initLowpass(uint32_t sampleRate, float cutoffHz) {
        m_sampleRate = sampleRate;
        setLowpassCutoff(cutoffHz);
        m_isLowpass = true;
    }

    /**
     * @brief Initialize as highpass filter
     * @param sampleRate Audio sample rate
     * @param cutoffHz Cutoff frequency in Hz
     */
    void initHighpass(uint32_t sampleRate, float cutoffHz) {
        m_sampleRate = sampleRate;
        setHighpassCutoff(cutoffHz);
        m_isLowpass = false;
    }

    void setLowpassCutoff(float hz) {
        float w = 2.0f * PI_VAL * hz / static_cast<float>(m_sampleRate);
        m_a = 1.0f - std::exp(-w);
        m_isLowpass = true;
    }

    void setHighpassCutoff(float hz) {
        float w = 2.0f * PI_VAL * hz / static_cast<float>(m_sampleRate);
        m_a = std::exp(-w);
        m_isLowpass = false;
    }

    /**
     * @brief Process a sample through the filter
     */
    float process(float sample) {
        if (m_isLowpass) {
            m_z = m_z + m_a * (sample - m_z);
            return m_z;
        } else {
            float out = m_a * (m_prevOut + sample - m_prevIn);
            m_prevIn = sample;
            m_prevOut = out;
            return out;
        }
    }

    void reset() {
        m_z = 0.0f;
        m_prevIn = 0.0f;
        m_prevOut = 0.0f;
    }

private:
    uint32_t m_sampleRate = 48000;
    float m_a = 0.0f;
    float m_z = 0.0f;
    float m_prevIn = 0.0f;
    float m_prevOut = 0.0f;
    bool m_isLowpass = true;

    static constexpr float PI_VAL = 3.14159265358979323846f;
};

/**
 * @brief All-pass filter for phaser effect
 *
 * First-order all-pass filter that shifts phase without
 * changing amplitude. Used in phaser for notch creation.
 */
class AllPassFilter {
public:
    AllPassFilter() = default;

    /**
     * @brief Set the all-pass coefficient
     * @param a Coefficient in range [-1, 1]
     */
    void setCoefficient(float a) {
        m_a = a;
    }

    /**
     * @brief Set cutoff frequency
     * @param sampleRate Audio sample rate
     * @param hz Cutoff frequency
     */
    void setCutoff(uint32_t sampleRate, float hz) {
        float w = 2.0f * PI_VAL * hz / static_cast<float>(sampleRate);
        m_a = (std::tan(w / 2.0f) - 1.0f) / (std::tan(w / 2.0f) + 1.0f);
    }

    /**
     * @brief Process a sample
     */
    float process(float sample) {
        float y = m_a * sample + m_z;
        m_z = sample - m_a * y;
        return y;
    }

    void reset() {
        m_z = 0.0f;
    }

private:
    float m_a = 0.0f;
    float m_z = 0.0f;

    static constexpr float PI_VAL = 3.14159265358979323846f;
};

/**
 * @brief Comb filter for reverb
 *
 * IIR comb filter with feedback and optional lowpass damping.
 * Used in Freeverb and other algorithmic reverbs.
 */
class CombFilter {
public:
    CombFilter() = default;

    /**
     * @brief Initialize comb filter
     * @param delaySamples Delay length in samples
     */
    void init(uint32_t delaySamples) {
        m_buffer.resize(delaySamples, 0.0f);
        m_bufferSize = delaySamples;
        m_writePos = 0;
    }

    /**
     * @brief Set feedback amount
     * @param feedback Feedback amount (0-1, typically 0.7-0.99)
     */
    void setFeedback(float feedback) {
        m_feedback = feedback;
    }

    /**
     * @brief Set damping (lowpass in feedback loop)
     * @param damping Damping amount (0-1, higher = more damping)
     */
    void setDamping(float damping) {
        m_damp1 = damping;
        m_damp2 = 1.0f - damping;
    }

    /**
     * @brief Process a sample
     */
    float process(float sample) {
        float output = m_buffer[m_writePos];

        // Apply damping (lowpass filter in feedback loop)
        m_filterStore = output * m_damp2 + m_filterStore * m_damp1;

        // Write new sample with feedback
        m_buffer[m_writePos] = sample + m_filterStore * m_feedback;

        // Advance write position
        m_writePos = (m_writePos + 1) % m_bufferSize;

        return output;
    }

    void reset() {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
        m_filterStore = 0.0f;
    }

private:
    std::vector<float> m_buffer;
    uint32_t m_bufferSize = 0;
    uint32_t m_writePos = 0;
    float m_feedback = 0.0f;
    float m_damp1 = 0.0f;
    float m_damp2 = 1.0f;
    float m_filterStore = 0.0f;
};

/**
 * @brief All-pass filter with delay for reverb
 *
 * Schroeder all-pass filter used in reverb for diffusion.
 */
class AllPassDelay {
public:
    AllPassDelay() = default;

    /**
     * @brief Initialize all-pass delay
     * @param delaySamples Delay length in samples
     */
    void init(uint32_t delaySamples) {
        m_buffer.resize(delaySamples, 0.0f);
        m_bufferSize = delaySamples;
        m_writePos = 0;
    }

    /**
     * @brief Set feedback amount
     * @param feedback Feedback (typically 0.5)
     */
    void setFeedback(float feedback) {
        m_feedback = feedback;
    }

    /**
     * @brief Process a sample
     */
    float process(float sample) {
        float bufferOutput = m_buffer[m_writePos];
        float output = -sample + bufferOutput;
        m_buffer[m_writePos] = sample + bufferOutput * m_feedback;
        m_writePos = (m_writePos + 1) % m_bufferSize;
        return output;
    }

    void reset() {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
    }

private:
    std::vector<float> m_buffer;
    uint32_t m_bufferSize = 0;
    uint32_t m_writePos = 0;
    float m_feedback = 0.5f;
};

} // namespace vivid::audio::dsp
