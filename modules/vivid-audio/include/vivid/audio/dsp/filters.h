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

/**
 * @brief State Variable Filter (SVF) with LP/HP/BP modes
 *
 * Multi-mode resonant filter with simultaneous lowpass, highpass,
 * and bandpass outputs. Can be cascaded for steeper slopes.
 *
 * Based on the Chamberlin SVF topology, with frequency warping
 * for accurate response at high frequencies.
 */
class SVFFilter {
public:
    enum class Mode {
        Lowpass,
        Highpass,
        Bandpass
    };

    SVFFilter() = default;

    /**
     * @brief Initialize the filter
     * @param sampleRate Audio sample rate
     */
    void init(uint32_t sampleRate) {
        m_sampleRate = sampleRate;
        reset();
    }

    /**
     * @brief Set filter mode
     * @param mode Lowpass, Highpass, or Bandpass
     */
    void setMode(Mode mode) {
        m_mode = mode;
    }

    /**
     * @brief Set cutoff frequency
     * @param hz Cutoff frequency in Hz
     */
    void setCutoff(float hz) {
        // Frequency warping for stability at high frequencies
        float w = 2.0f * PI_VAL * hz / static_cast<float>(m_sampleRate);
        m_f = 2.0f * std::sin(w * 0.5f);
        // Clamp to avoid instability
        if (m_f > 1.0f) m_f = 1.0f;
    }

    /**
     * @brief Set resonance (Q)
     * @param q Resonance amount (0-1, where 1 is near self-oscillation)
     */
    void setResonance(float q) {
        // Map 0-1 to damping factor (high Q = low damping)
        m_q = 1.0f - q * 0.99f;
        if (m_q < 0.01f) m_q = 0.01f;
    }

    /**
     * @brief Process a sample through the filter
     * @param sample Input sample
     * @return Filtered output based on current mode
     */
    float process(float sample) {
        // State Variable Filter update
        m_low += m_f * m_band;
        m_high = sample - m_low - m_q * m_band;
        m_band += m_f * m_high;

        switch (m_mode) {
            case Mode::Lowpass:  return m_low;
            case Mode::Highpass: return m_high;
            case Mode::Bandpass: return m_band;
        }
        return m_low;
    }

    /**
     * @brief Process with oversampling for better stability at high resonance
     * @param sample Input sample
     * @return Filtered output
     */
    float process2x(float sample) {
        // Two iterations for stability
        float out = 0.0f;
        for (int i = 0; i < 2; ++i) {
            m_low += m_f * 0.5f * m_band;
            m_high = sample - m_low - m_q * m_band;
            m_band += m_f * 0.5f * m_high;
        }

        switch (m_mode) {
            case Mode::Lowpass:  out = m_low; break;
            case Mode::Highpass: out = m_high; break;
            case Mode::Bandpass: out = m_band; break;
        }
        return out;
    }

    /**
     * @brief Get lowpass output (available regardless of mode)
     */
    float getLowpass() const { return m_low; }

    /**
     * @brief Get highpass output (available regardless of mode)
     */
    float getHighpass() const { return m_high; }

    /**
     * @brief Get bandpass output (available regardless of mode)
     */
    float getBandpass() const { return m_band; }

    void reset() {
        m_low = 0.0f;
        m_high = 0.0f;
        m_band = 0.0f;
    }

private:
    uint32_t m_sampleRate = 48000;
    Mode m_mode = Mode::Lowpass;
    float m_f = 0.1f;   // Frequency coefficient
    float m_q = 0.5f;   // Damping (inverse of resonance)

    // State variables
    float m_low = 0.0f;
    float m_high = 0.0f;
    float m_band = 0.0f;

    static constexpr float PI_VAL = 3.14159265358979323846f;
};

/**
 * @brief Cascaded SVF for 24dB/oct slopes
 *
 * Two SVF stages in series for steeper filter slopes.
 * Commonly used for more aggressive filtering in drum synthesis.
 */
class SVFFilter24 {
public:
    SVFFilter24() = default;

    /**
     * @brief Initialize both filter stages
     * @param sampleRate Audio sample rate
     */
    void init(uint32_t sampleRate) {
        m_stage1.init(sampleRate);
        m_stage2.init(sampleRate);
    }

    /**
     * @brief Set filter mode for both stages
     */
    void setMode(SVFFilter::Mode mode) {
        m_stage1.setMode(mode);
        m_stage2.setMode(mode);
    }

    /**
     * @brief Set cutoff frequency for both stages
     */
    void setCutoff(float hz) {
        m_stage1.setCutoff(hz);
        m_stage2.setCutoff(hz);
    }

    /**
     * @brief Set resonance for first stage only
     *
     * Second stage runs with minimal resonance to avoid
     * excessive ringing while still providing steep rolloff.
     */
    void setResonance(float q) {
        m_stage1.setResonance(q);
        m_stage2.setResonance(0.0f);  // No resonance on second stage
    }

    /**
     * @brief Process a sample through both stages
     */
    float process(float sample) {
        return m_stage2.process(m_stage1.process(sample));
    }

    void reset() {
        m_stage1.reset();
        m_stage2.reset();
    }

private:
    SVFFilter m_stage1;
    SVFFilter m_stage2;
};

} // namespace vivid::audio::dsp
