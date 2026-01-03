#pragma once

/**
 * @file tape_effect.h
 * @brief Tape emulation effect
 *
 * Simulates the characteristics of analog tape recording:
 * - Wow: Slow pitch drift from tape speed variations
 * - Flutter: Fast pitch jitter from mechanical instabilities
 * - Saturation: Warm harmonic distortion from magnetic tape
 * - Hiss: High-frequency noise floor
 *
 * The audio equivalent of CRTEffect for visuals - adds vintage
 * character and warmth to any audio source.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/delay_line.h>
#include <vivid/audio/dsp/lfo.h>
#include <vivid/audio/dsp/filters.h>
#include <vivid/param.h>
#include <random>

namespace vivid::audio {

/**
 * @brief Tape emulation effect
 *
 * Simulates the warm, organic character of analog tape recording.
 * Combines pitch modulation (wow/flutter), soft saturation, and
 * tape hiss for authentic vintage sound.
 *
 * @par Parameters
 * - `wow` - Slow pitch drift amount (0-1, typical 0.1-0.3)
 * - `flutter` - Fast pitch jitter amount (0-1, typical 0.1-0.2)
 * - `saturation` - Tape saturation/warmth (0-1)
 * - `hiss` - Tape hiss level (0-1)
 * - `age` - Overall tape degradation (0-1, affects all parameters)
 * - `mix` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * auto& tape = chain.add<TapeEffect>("tape");
 * tape.input("synth");
 * tape.wow = 0.3f;        // Noticeable pitch drift
 * tape.flutter = 0.2f;    // Subtle fast wobble
 * tape.saturation = 0.5f; // Warm compression
 * tape.hiss = 0.1f;       // Light tape noise
 * tape.mix = 1.0f;        // Fully wet
 * @endcode
 *
 * @par Boards of Canada Style
 * @code
 * // For that classic BoC detuned, nostalgic sound:
 * tape.wow = 0.4f;
 * tape.flutter = 0.15f;
 * tape.saturation = 0.6f;
 * tape.hiss = 0.05f;
 * tape.age = 0.3f;
 * @endcode
 */
class TapeEffect : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> wow{"wow", 0.2f, 0.0f, 1.0f};             ///< Slow pitch drift (0.5-2 Hz)
    Param<float> flutter{"flutter", 0.1f, 0.0f, 1.0f};     ///< Fast pitch jitter (5-15 Hz)
    Param<float> saturation{"saturation", 0.3f, 0.0f, 1.0f}; ///< Tape saturation/warmth
    Param<float> hiss{"hiss", 0.05f, 0.0f, 1.0f};          ///< Tape hiss level
    Param<float> age{"age", 0.0f, 0.0f, 1.0f};             ///< Overall degradation
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};             ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    TapeEffect() {
        registerParam(wow);
        registerParam(flutter);
        registerParam(saturation);
        registerParam(hiss);
        registerParam(age);
        registerParam(mix);
    }
    ~TapeEffect() override = default;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "TapeEffect"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    // Saturation using tanh soft clipping
    float saturate(float sample, float drive);

    // Generate tape hiss (filtered noise)
    float generateHiss();

    // DSP components
    dsp::DelayLine m_delayL;
    dsp::DelayLine m_delayR;

    // Wow LFO (slow, ~0.5-2 Hz)
    dsp::LFO m_wowLfoL;
    dsp::LFO m_wowLfoR;

    // Flutter LFO (fast, ~5-15 Hz with randomized depth)
    dsp::LFO m_flutterLfoL;
    dsp::LFO m_flutterLfoR;

    // Hiss filtering (lowpass to shape noise spectrum)
    dsp::OnePoleFilter m_hissFilterL;
    dsp::OnePoleFilter m_hissFilterR;

    // Anti-aliasing filter for saturation
    dsp::OnePoleFilter m_antiAliasL;
    dsp::OnePoleFilter m_antiAliasR;

    // Random number generator for hiss and flutter variation
    std::mt19937 m_rng{42};
    std::uniform_real_distribution<float> m_noiseDist{-1.0f, 1.0f};
    std::uniform_real_distribution<float> m_flutterDepthDist{0.5f, 1.5f};

    // Flutter depth modulation (randomized per cycle like Airwindows)
    float m_flutterDepthL = 1.0f;
    float m_flutterDepthR = 1.0f;
    float m_prevFlutterPhaseL = 0.0f;
    float m_prevFlutterPhaseR = 0.0f;

    uint32_t m_sampleRate = 48000;

    // Base delay for pitch modulation (small, ~5-10ms)
    static constexpr float BASE_DELAY_MS = 7.0f;
    // Maximum modulation depth in ms
    static constexpr float MAX_WOW_DEPTH_MS = 3.0f;
    static constexpr float MAX_FLUTTER_DEPTH_MS = 0.5f;
};

} // namespace vivid::audio
