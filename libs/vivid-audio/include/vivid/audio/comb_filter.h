#pragma once

/**
 * @file comb_filter.h
 * @brief Comb filter for metallic and resonant textures
 *
 * Creates pitched resonances from any audio input. Great for:
 * - Metallic percussion sounds
 * - Karplus-Strong string synthesis
 * - Flanging/chorus effects at short delay times
 * - Pitched reverb tails
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/param.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

/**
 * @brief Comb filter type
 */
enum class CombType {
    FeedForward,  ///< FIR comb (adds delayed signal)
    FeedBack,     ///< IIR comb (feeds output back, creates resonance)
    AllPass       ///< All-pass comb (phase shifting, preserves magnitude)
};

/**
 * @brief Comb filter for resonant/metallic textures
 *
 * A comb filter creates evenly-spaced notches or peaks in the frequency
 * spectrum, producing metallic, resonant, or pitched textures.
 *
 * The delay time determines the fundamental frequency of the resonance:
 * frequency = 1 / delayTime
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | frequency | float | 20-2000 | 200 | Resonant frequency in Hz |
 * | feedback | float | 0-0.99 | 0.8 | Feedback amount (higher = longer decay) |
 * | damping | float | 0-1 | 0.5 | High-frequency damping (string character) |
 *
 * @par Example
 * @code
 * // Karplus-Strong plucked string
 * auto& comb = chain.add<CombFilter>("string");
 * comb.input("noiseExciter");
 * comb.frequency = 440.0f;  // A4
 * comb.feedback = 0.995f;   // Long decay
 * comb.damping = 0.4f;      // Warm string tone
 * @endcode
 */
class CombFilter : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> frequency{"frequency", 200.0f, 20.0f, 2000.0f};  ///< Resonant frequency Hz
    Param<float> feedback{"feedback", 0.8f, 0.0f, 0.99f};          ///< Feedback amount
    Param<float> damping{"damping", 0.5f, 0.0f, 1.0f};             ///< HF damping (0=bright, 1=dark)

    /// @}
    // -------------------------------------------------------------------------

    CombFilter() {
        registerParam(frequency);
        registerParam(feedback);
        registerParam(damping);
    }

    ~CombFilter() override = default;

    /**
     * @brief Set comb filter type
     */
    void setType(CombType t) { m_type = t; }

    std::string name() const override { return "CombFilter"; }

protected:
    void initEffect(Context& ctx) override {
        m_sampleRate = AUDIO_SAMPLE_RATE;

        // Allocate delay buffer for minimum frequency (20Hz = 50ms = ~2400 samples at 48kHz)
        uint32_t maxDelaySamples = static_cast<uint32_t>(m_sampleRate / 20.0f) + 1;
        m_buffer[0].resize(maxDelaySamples, 0.0f);
        m_buffer[1].resize(maxDelaySamples, 0.0f);
        m_writePos = 0;

        // Initialize damping filter state
        m_dampState[0] = 0.0f;
        m_dampState[1] = 0.0f;
    }

    void processEffect(const float* input, float* output, uint32_t frames) override {
        float freq = static_cast<float>(frequency);
        float fb = static_cast<float>(feedback);
        float damp = static_cast<float>(damping);

        // Calculate delay in samples from frequency
        float delaySamples = static_cast<float>(m_sampleRate) / freq;
        uint32_t delaySamplesInt = static_cast<uint32_t>(delaySamples);
        float delayFrac = delaySamples - static_cast<float>(delaySamplesInt);

        // Clamp to buffer size
        uint32_t bufferSize = static_cast<uint32_t>(m_buffer[0].size());
        if (delaySamplesInt >= bufferSize) {
            delaySamplesInt = bufferSize - 1;
        }

        for (uint32_t i = 0; i < frames; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                float in = input[i * 2 + ch];

                // Read from delay buffer with linear interpolation
                int32_t readPos1 = static_cast<int32_t>(m_writePos) - static_cast<int32_t>(delaySamplesInt);
                if (readPos1 < 0) readPos1 += bufferSize;

                int32_t readPos2 = readPos1 - 1;
                if (readPos2 < 0) readPos2 += bufferSize;

                float delayed = m_buffer[ch][readPos1] * (1.0f - delayFrac) +
                               m_buffer[ch][readPos2] * delayFrac;

                // Apply damping (one-pole lowpass on feedback path)
                // This gives the "string" character - high frequencies decay faster
                m_dampState[ch] = delayed + damp * (m_dampState[ch] - delayed);
                float dampedDelayed = m_dampState[ch];

                float out;
                switch (m_type) {
                    case CombType::FeedForward:
                        // FIR comb: y[n] = x[n] + g * x[n-D]
                        out = in + fb * delayed;
                        m_buffer[ch][m_writePos] = in;
                        break;

                    case CombType::FeedBack:
                        // IIR comb: y[n] = x[n] + g * y[n-D]
                        out = in + fb * dampedDelayed;
                        m_buffer[ch][m_writePos] = out;
                        break;

                    case CombType::AllPass:
                        // All-pass: y[n] = -g*x[n] + x[n-D] + g*y[n-D]
                        out = -fb * in + delayed + fb * dampedDelayed;
                        m_buffer[ch][m_writePos] = in + fb * dampedDelayed;
                        break;
                }

                output[i * 2 + ch] = out;
            }

            // Advance write position
            m_writePos = (m_writePos + 1) % bufferSize;
        }
    }

    void cleanupEffect() override {
        m_buffer[0].clear();
        m_buffer[1].clear();
        m_dampState[0] = 0.0f;
        m_dampState[1] = 0.0f;
    }

private:
    CombType m_type = CombType::FeedBack;

    // Delay buffer per channel
    std::vector<float> m_buffer[2];
    uint32_t m_writePos = 0;

    // Damping filter state (one-pole lowpass)
    float m_dampState[2] = {0.0f, 0.0f};

    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
