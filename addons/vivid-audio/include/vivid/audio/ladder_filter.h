#pragma once

/**
 * @file ladder_filter.h
 * @brief Moog-style ladder filter with 24dB/octave slope
 *
 * Classic analog-modeled filter with warm character and self-oscillation
 * capability at high resonance settings.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/param.h>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

/**
 * @brief Moog-style ladder filter
 *
 * A 4-pole (24dB/octave) lowpass filter with the warm, musical character
 * of the classic Moog transistor ladder design. Features:
 * - Smooth resonance up to self-oscillation
 * - Nonlinear saturation for analog warmth
 * - Stable at all cutoff frequencies and resonance settings
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | cutoff | float | 20-20000 | 1000 | Cutoff frequency in Hz |
 * | resonance | float | 0-1 | 0 | Resonance (1 = self-oscillation) |
 * | drive | float | 1-4 | 1 | Input drive/saturation |
 *
 * @par Example
 * @code
 * auto& ladder = chain.add<LadderFilter>("ladder");
 * ladder.input("synth");
 * ladder.cutoff = 800.0f;
 * ladder.resonance = 0.7f;
 * ladder.drive = 1.5f;
 * @endcode
 */
class LadderFilter : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> cutoff{"cutoff", 1000.0f, 20.0f, 20000.0f};    ///< Cutoff frequency in Hz
    Param<float> resonance{"resonance", 0.0f, 0.0f, 1.0f};       ///< Resonance (0-1, 1=self-osc)
    Param<float> drive{"drive", 1.0f, 1.0f, 4.0f};               ///< Input drive/saturation

    /// @}
    // -------------------------------------------------------------------------

    LadderFilter() {
        registerParam(cutoff);
        registerParam(resonance);
        registerParam(drive);
    }

    ~LadderFilter() override = default;

    std::string name() const override { return "LadderFilter"; }

protected:
    void initEffect(Context& ctx) override {
        // Initialize filter state
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < 4; ++i) {
                m_stage[ch][i] = 0.0f;
            }
            m_delay[ch] = 0.0f;
        }
        m_sampleRate = AUDIO_SAMPLE_RATE;
    }

    void processEffect(const float* input, float* output, uint32_t frames) override {
        float fc = static_cast<float>(cutoff);
        float res = static_cast<float>(resonance);
        float drv = static_cast<float>(drive);

        // Convert cutoff to normalized frequency
        // Use tanh approximation for stability at high frequencies
        float wc = 2.0f * PI * fc / static_cast<float>(m_sampleRate);
        float g = 0.9892f * wc - 0.4342f * wc * wc + 0.1381f * wc * wc * wc - 0.0202f * wc * wc * wc * wc;
        g = std::max(0.0f, std::min(1.0f, g));

        // Resonance compensation (prevent volume loss at high resonance)
        float k = res * 4.0f;  // Scale resonance to feedback coefficient

        for (uint32_t i = 0; i < frames; ++i) {
            for (int ch = 0; ch < 2; ++ch) {
                float in = input[i * 2 + ch];

                // Apply drive/saturation
                in *= drv;

                // Feedback with delay (improves stability)
                float fb = m_delay[ch] * k;

                // Nonlinear feedback saturation
                fb = fastTanh(fb);

                // Input with feedback subtraction
                float u = in - fb;

                // Soft clip input for analog character
                u = fastTanh(u);

                // Four cascaded one-pole lowpass stages
                m_stage[ch][0] += g * (u - m_stage[ch][0]);
                m_stage[ch][1] += g * (m_stage[ch][0] - m_stage[ch][1]);
                m_stage[ch][2] += g * (m_stage[ch][1] - m_stage[ch][2]);
                m_stage[ch][3] += g * (m_stage[ch][2] - m_stage[ch][3]);

                // Store for next sample's feedback
                m_delay[ch] = m_stage[ch][3];

                // Output from 4th stage (24dB/oct)
                output[i * 2 + ch] = m_stage[ch][3];
            }
        }
    }

    void cleanupEffect() override {
        // Reset filter state
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < 4; ++i) {
                m_stage[ch][i] = 0.0f;
            }
            m_delay[ch] = 0.0f;
        }
    }

private:
    // Fast tanh approximation for saturation
    static float fastTanh(float x) {
        // Pade approximation, accurate for |x| < 3
        if (x < -3.0f) return -1.0f;
        if (x > 3.0f) return 1.0f;
        float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // Filter state: 4 stages per channel
    float m_stage[2][4] = {{0, 0, 0, 0}, {0, 0, 0, 0}};

    // One-sample delay for feedback
    float m_delay[2] = {0, 0};

    uint32_t m_sampleRate = 48000;

    static constexpr float PI = 3.14159265358979323846f;
};

} // namespace vivid::audio
