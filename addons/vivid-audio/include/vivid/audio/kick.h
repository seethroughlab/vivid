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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> pitch{"pitch", 50.0f, 20.0f, 200.0f};           ///< Base pitch in Hz
    Param<float> pitchEnv{"pitchEnv", 100.0f, 0.0f, 500.0f};     ///< Pitch envelope amount
    Param<float> pitchDecay{"pitchDecay", 0.1f, 0.01f, 0.5f};    ///< Pitch envelope decay
    Param<float> decay{"decay", 0.5f, 0.05f, 2.0f};              ///< Amplitude decay time
    Param<float> click{"click", 0.3f, 0.0f, 1.0f};               ///< Click/transient amount
    Param<float> drive{"drive", 0.0f, 0.0f, 1.0f};               ///< Soft saturation
    Param<float> volume{"volume", 0.8f, 0.0f, 1.0f};             ///< Output volume

    /// @}
    // -------------------------------------------------------------------------

    Kick() {
        registerParam(pitch);
        registerParam(pitchEnv);
        registerParam(pitchDecay);
        registerParam(decay);
        registerParam(click);
        registerParam(drive);
        registerParam(volume);
    }
    ~Kick() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    // trigger() inherited from AudioOperator - queues to audio thread
    void reset();
    bool isActive() const { return m_ampEnv > 0.0001f; }

    // Envelope access for visualization
    float ampEnvelope() const { return m_ampEnv; }
    float pitchEnvelope() const { return m_pitchEnvValue; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Kick"; }

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    /// @}

protected:
    void onTrigger() override;  // Called from audio thread

private:
    float softClip(float x) const;

    // State
    float m_phase = 0.0f;
    float m_ampEnv = 0.0f;
    float m_pitchEnvValue = 0.0f;
    float m_clickEnv = 0.0f;
    uint32_t m_sampleRate = 48000;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
