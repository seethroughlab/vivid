#pragma once

/**
 * @file kick.h
 * @brief 808-style kick drum synthesizer
 *
 * Self-contained kick drum with pitch envelope and click transient.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief 808-style kick drum synthesizer
 *
 * Generates classic analog-style kick drums using a sine oscillator with
 * pitch envelope (sweep from high to low frequency) and optional click
 * transient for attack definition.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | pitch | float | 20-200 | 50 | Base pitch in Hz |
 * | pitchEnv | float | 0-500 | 100 | Pitch envelope amount (added to base) |
 * | pitchDecay | float | 0.01-0.5 | 0.1 | Pitch envelope decay time |
 * | decay | float | 0.05-2 | 0.5 | Amplitude decay time |
 * | click | float | 0-1 | 0.3 | Click/transient amount |
 * | drive | float | 0-1 | 0 | Soft saturation amount |
 *
 * @par Example
 * @code
 * chain.add<Kick>("kick")
 *     .pitch(50.0f)
 *     .pitchEnv(150.0f)
 *     .decay(0.5f)
 *     .click(0.3f);
 *
 * chain.get<Kick>("kick")->trigger();
 * @endcode
 */
class Kick : public AudioOperator {
public:
    Kick() = default;
    ~Kick() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    Kick& pitch(float hz) { m_pitch = hz; return *this; }
    Kick& pitchEnv(float hz) { m_pitchEnv = hz; return *this; }
    Kick& pitchDecay(float s) { m_pitchDecay = s; return *this; }
    Kick& decay(float s) { m_decay = s; return *this; }
    Kick& click(float amt) { m_click = amt; return *this; }
    Kick& drive(float amt) { m_drive = amt; return *this; }
    Kick& volume(float v) { m_volume = v; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void trigger();
    void reset();
    bool isActive() const { return m_ampEnv > 0.0001f; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Kick"; }

    std::vector<ParamDecl> params() override {
        return { m_pitch.decl(), m_pitchEnv.decl(), m_pitchDecay.decl(),
                 m_decay.decl(), m_click.decl(), m_drive.decl(), m_volume.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    float softClip(float x) const;

    // Parameters
    Param<float> m_pitch{"pitch", 50.0f, 20.0f, 200.0f};
    Param<float> m_pitchEnv{"pitchEnv", 100.0f, 0.0f, 500.0f};
    Param<float> m_pitchDecay{"pitchDecay", 0.1f, 0.01f, 0.5f};
    Param<float> m_decay{"decay", 0.5f, 0.05f, 2.0f};
    Param<float> m_click{"click", 0.3f, 0.0f, 1.0f};
    Param<float> m_drive{"drive", 0.0f, 0.0f, 1.0f};
    Param<float> m_volume{"volume", 0.8f, 0.0f, 1.0f};

    // State
    float m_phase = 0.0f;
    float m_ampEnv = 0.0f;
    float m_pitchEnvValue = 0.0f;
    float m_clickEnv = 0.0f;
    uint32_t m_sampleRate = 48000;

    bool m_initialized = false;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
