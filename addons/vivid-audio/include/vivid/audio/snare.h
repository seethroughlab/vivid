#pragma once

/**
 * @file snare.h
 * @brief Snare drum synthesizer
 *
 * Snare with tone oscillator and noise burst.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Snare drum synthesizer
 *
 * Generates snare drums using a combination of tone (sine) and noise.
 * The tone provides body while the noise provides the snare rattle.
 * Separate envelopes for tone and noise allow precise shaping.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | tone | float | 0-1 | 0.5 | Tone/body mix amount |
 * | noise | float | 0-1 | 0.7 | Noise/snare mix amount |
 * | pitch | float | 100-400 | 200 | Tone pitch in Hz |
 * | toneDecay | float | 0.01-0.5 | 0.1 | Tone envelope decay |
 * | noiseDecay | float | 0.05-0.5 | 0.2 | Noise envelope decay |
 * | snappy | float | 0-1 | 0.5 | High-frequency emphasis |
 *
 * @par Example
 * @code
 * chain.add<Snare>("snare")
 *     .tone(0.4f)
 *     .noise(0.8f)
 *     .pitch(180.0f)
 *     .snappy(0.6f);
 *
 * chain.get<Snare>("snare")->trigger();
 * @endcode
 */
class Snare : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> tone{"tone", 0.5f, 0.0f, 1.0f};             ///< Tone/body amount
    Param<float> noise{"noise", 0.7f, 0.0f, 1.0f};           ///< Noise/snare amount
    Param<float> pitch{"pitch", 200.0f, 100.0f, 400.0f};     ///< Tone pitch in Hz
    Param<float> toneDecay{"toneDecay", 0.1f, 0.01f, 0.5f};  ///< Tone envelope decay
    Param<float> noiseDecay{"noiseDecay", 0.2f, 0.05f, 0.5f}; ///< Noise envelope decay
    Param<float> snappy{"snappy", 0.5f, 0.0f, 1.0f};         ///< High-freq emphasis
    Param<float> volume{"volume", 0.8f, 0.0f, 1.0f};         ///< Output volume

    /// @}
    // -------------------------------------------------------------------------

    Snare() {
        registerParam(tone);
        registerParam(noise);
        registerParam(pitch);
        registerParam(toneDecay);
        registerParam(noiseDecay);
        registerParam(snappy);
        registerParam(volume);
    }
    ~Snare() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void trigger();
    void reset();
    bool isActive() const { return m_toneEnv > 0.0001f || m_noiseEnv > 0.0001f; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Snare"; }

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    /// @}

private:
    void triggerInternal();  // Called from audio thread
    float generateNoise();
    float highpass(float in, int ch);

    // State
    float m_phase = 0.0f;
    float m_toneEnv = 0.0f;
    float m_noiseEnv = 0.0f;
    uint32_t m_seed = 12345;

    // Highpass filter state for snappy
    float m_hpState[2] = {0, 0};

    uint32_t m_sampleRate = 48000;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
