#pragma once

/**
 * @file envelope.h
 * @brief Envelope follower and detector for dynamics processing
 *
 * EnvelopeFollower tracks the amplitude envelope of an audio signal
 * for use in Compressor, Limiter, and Gate effects.
 */

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace vivid::audio::dsp {

/**
 * @brief Envelope detection mode
 */
enum class EnvelopeMode {
    Peak,   ///< Track peak levels (fast response)
    RMS     ///< Track RMS levels (average power, smoother)
};

/**
 * @brief Envelope follower with attack/release smoothing
 *
 * Tracks the amplitude envelope of an audio signal with:
 * - Configurable attack and release times
 * - Peak or RMS detection modes
 * - Linear or logarithmic output
 */
class EnvelopeFollower {
public:
    EnvelopeFollower() = default;

    /**
     * @brief Initialize envelope follower
     * @param sampleRate Audio sample rate
     * @param attackMs Attack time in milliseconds
     * @param releaseMs Release time in milliseconds
     * @param mode Detection mode (Peak or RMS)
     */
    void init(uint32_t sampleRate, float attackMs = 10.0f,
              float releaseMs = 100.0f, EnvelopeMode mode = EnvelopeMode::Peak) {
        m_sampleRate = sampleRate;
        m_mode = mode;
        setAttack(attackMs);
        setRelease(releaseMs);
    }

    /**
     * @brief Set attack time
     * @param ms Attack time in milliseconds
     */
    void setAttack(float ms) {
        m_attackMs = ms;
        m_attackCoef = calculateCoefficient(ms);
    }

    /**
     * @brief Set release time
     * @param ms Release time in milliseconds
     */
    void setRelease(float ms) {
        m_releaseMs = ms;
        m_releaseCoef = calculateCoefficient(ms);
    }

    /**
     * @brief Set detection mode
     */
    void setMode(EnvelopeMode mode) {
        m_mode = mode;
    }

    /**
     * @brief Reset envelope state
     */
    void reset() {
        m_envelope = 0.0f;
        m_rmsSum = 0.0f;
    }

    /**
     * @brief Process a sample and return envelope value
     * @param sample Input sample
     * @return Envelope value (linear amplitude)
     */
    float process(float sample) {
        float input;

        if (m_mode == EnvelopeMode::Peak) {
            input = std::abs(sample);
        } else {
            // RMS: running average of squared samples
            m_rmsSum = m_rmsSum * 0.999f + sample * sample * 0.001f;
            input = std::sqrt(m_rmsSum);
        }

        // Attack/release envelope
        if (input > m_envelope) {
            m_envelope = m_attackCoef * (m_envelope - input) + input;
        } else {
            m_envelope = m_releaseCoef * (m_envelope - input) + input;
        }

        return m_envelope;
    }

    /**
     * @brief Process stereo samples and return envelope
     * @param left Left channel sample
     * @param right Right channel sample
     * @return Envelope value (max of both channels)
     */
    float processStereo(float left, float right) {
        float input;

        if (m_mode == EnvelopeMode::Peak) {
            input = std::max(std::abs(left), std::abs(right));
        } else {
            float maxSq = std::max(left * left, right * right);
            m_rmsSum = m_rmsSum * 0.999f + maxSq * 0.001f;
            input = std::sqrt(m_rmsSum);
        }

        if (input > m_envelope) {
            m_envelope = m_attackCoef * (m_envelope - input) + input;
        } else {
            m_envelope = m_releaseCoef * (m_envelope - input) + input;
        }

        return m_envelope;
    }

    /**
     * @brief Get current envelope value in dB
     */
    float envelopeDb() const {
        return linearToDb(m_envelope);
    }

    /**
     * @brief Get current envelope value (linear)
     */
    float envelope() const { return m_envelope; }

    /**
     * @brief Convert linear amplitude to dB
     */
    static float linearToDb(float linear) {
        if (linear <= 0.0f) return -100.0f;
        return 20.0f * std::log10(linear);
    }

    /**
     * @brief Convert dB to linear amplitude
     */
    static float dbToLinear(float db) {
        return std::pow(10.0f, db / 20.0f);
    }

private:
    float calculateCoefficient(float ms) {
        if (ms <= 0.0f) return 0.0f;
        // Time constant for exponential decay to reach ~37% (1/e)
        return std::exp(-1.0f / (ms * 0.001f * static_cast<float>(m_sampleRate)));
    }

    uint32_t m_sampleRate = 48000;
    EnvelopeMode m_mode = EnvelopeMode::Peak;
    float m_attackMs = 10.0f;
    float m_releaseMs = 100.0f;
    float m_attackCoef = 0.0f;
    float m_releaseCoef = 0.0f;
    float m_envelope = 0.0f;
    float m_rmsSum = 0.0f;
};

} // namespace vivid::audio::dsp
