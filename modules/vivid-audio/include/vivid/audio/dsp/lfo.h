#pragma once

/**
 * @file lfo.h
 * @brief Low Frequency Oscillator for modulation effects
 *
 * LFO generates periodic waveforms for modulating parameters
 * in Chorus, Flanger, Phaser, and other modulation effects.
 */

#include <cmath>
#include <cstdint>

namespace vivid::audio::dsp {

/**
 * @brief LFO waveform types
 */
enum class LFOWaveform {
    Sine,
    Triangle,
    Square,
    Saw
};

/**
 * @brief Low Frequency Oscillator
 *
 * Generates periodic waveforms for parameter modulation:
 * - Sine: Smooth, natural modulation
 * - Triangle: Linear sweep
 * - Square: Abrupt on/off modulation
 * - Saw: Ramp modulation
 */
class LFO {
public:
    LFO() = default;

    /**
     * @brief Initialize LFO
     * @param sampleRate Audio sample rate in Hz
     * @param frequency LFO frequency in Hz
     * @param waveform Waveform type
     */
    void init(uint32_t sampleRate, float frequency = 1.0f,
              LFOWaveform waveform = LFOWaveform::Sine) {
        m_sampleRate = sampleRate;
        m_waveform = waveform;
        setFrequency(frequency);
    }

    /**
     * @brief Set LFO frequency
     * @param hz Frequency in Hz (typically 0.1 - 10 Hz)
     */
    void setFrequency(float hz) {
        m_frequency = hz;
        m_phaseIncrement = hz / static_cast<float>(m_sampleRate);
    }

    /**
     * @brief Set LFO waveform
     */
    void setWaveform(LFOWaveform waveform) {
        m_waveform = waveform;
    }

    /**
     * @brief Reset phase to zero
     */
    void reset() {
        m_phase = 0.0f;
    }

    /**
     * @brief Generate next sample and advance phase
     * @return LFO value in range [-1, 1]
     */
    float process() {
        float value = 0.0f;

        switch (m_waveform) {
            case LFOWaveform::Sine:
                value = std::sin(m_phase * 2.0f * PI_VAL);
                break;

            case LFOWaveform::Triangle:
                // Triangle: 0->1->0->-1->0
                if (m_phase < 0.25f) {
                    value = m_phase * 4.0f;
                } else if (m_phase < 0.75f) {
                    value = 2.0f - m_phase * 4.0f;
                } else {
                    value = m_phase * 4.0f - 4.0f;
                }
                break;

            case LFOWaveform::Square:
                value = m_phase < 0.5f ? 1.0f : -1.0f;
                break;

            case LFOWaveform::Saw:
                value = 2.0f * m_phase - 1.0f;
                break;
        }

        // Advance phase
        m_phase += m_phaseIncrement;
        if (m_phase >= 1.0f) {
            m_phase -= 1.0f;
        }

        return value;
    }

    /**
     * @brief Get current phase [0, 1)
     */
    float phase() const { return m_phase; }

    /**
     * @brief Get current frequency in Hz
     */
    float frequency() const { return m_frequency; }

private:
    uint32_t m_sampleRate = 48000;
    LFOWaveform m_waveform = LFOWaveform::Sine;
    float m_frequency = 1.0f;
    float m_phase = 0.0f;
    float m_phaseIncrement = 0.0f;

    static constexpr float PI_VAL = 3.14159265358979323846f;
};

} // namespace vivid::audio::dsp
