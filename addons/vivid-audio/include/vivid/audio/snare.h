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
    Snare() = default;
    ~Snare() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    Snare& tone(float amt) { m_tone = amt; return *this; }
    Snare& noise(float amt) { m_noise = amt; return *this; }
    Snare& pitch(float hz) { m_pitch = hz; return *this; }
    Snare& toneDecay(float s) { m_toneDecay = s; return *this; }
    Snare& noiseDecay(float s) { m_noiseDecay = s; return *this; }
    Snare& snappy(float amt) { m_snappy = amt; return *this; }
    Snare& volume(float v) { m_volume = v; return *this; }

    /// @}
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

    std::vector<ParamDecl> params() override {
        return { m_tone.decl(), m_noise.decl(), m_pitch.decl(),
                 m_toneDecay.decl(), m_noiseDecay.decl(), m_snappy.decl(), m_volume.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    float generateNoise();
    float highpass(float in, int ch);

    // Parameters
    Param<float> m_tone{"tone", 0.5f, 0.0f, 1.0f};
    Param<float> m_noise{"noise", 0.7f, 0.0f, 1.0f};
    Param<float> m_pitch{"pitch", 200.0f, 100.0f, 400.0f};
    Param<float> m_toneDecay{"toneDecay", 0.1f, 0.01f, 0.5f};
    Param<float> m_noiseDecay{"noiseDecay", 0.2f, 0.05f, 0.5f};
    Param<float> m_snappy{"snappy", 0.5f, 0.0f, 1.0f};
    Param<float> m_volume{"volume", 0.8f, 0.0f, 1.0f};

    // State
    float m_phase = 0.0f;
    float m_toneEnv = 0.0f;
    float m_noiseEnv = 0.0f;
    uint32_t m_seed = 12345;

    // Highpass filter state for snappy
    float m_hpState[2] = {0, 0};

    uint32_t m_sampleRate = 48000;
    bool m_initialized = false;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
