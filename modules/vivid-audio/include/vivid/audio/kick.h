#pragma once

/**
 * @file kick.h
 * @brief 808-style kick drum synthesizer
 *
 * Self-contained kick drum with pitch envelope and click transient.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/midi_receiver.h>
#include <vivid/operator_registry.h>
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
 * | overtones | float | 0-1 | 0 | Harmonic content (2nd/3rd) |
 * | attack | float | 0-0.1 | 0 | Transient softening time |
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
 
 * @see Snare, HiHat, Clap, Sequencer, Clock, PitchEnv, Decay
 */
class Kick : public AudioOperator, public MidiReceiver {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> pitch{"pitch", 50.0f, 20.0f, 200.0f};           ///< Base pitch in Hz
    Param<float> pitchEnv{"pitchEnv", 100.0f, 0.0f, 500.0f};     ///< Pitch envelope amount
    Param<float> pitchDecay{"pitchDecay", 0.1f, 0.01f, 0.5f};    ///< Pitch envelope decay
    ADSRParam envelope{"envelope", 0.001f, 0.5f, 0.0f, 0.1f, 2.0f};  ///< Amplitude ADSR envelope
    Param<float> click{"click", 0.3f, 0.0f, 1.0f};               ///< Click/transient amount
    Param<float> drive{"drive", 0.0f, 0.0f, 1.0f};               ///< Soft saturation
    Param<float> overtones{"overtones", 0.0f, 0.0f, 1.0f};       ///< Harmonic content (2nd/3rd)
    Param<float> attack{"attack", 0.0f, 0.0f, 0.1f};             ///< Transient softening time
    Param<float> volume{"volume", 0.8f, 0.0f, 1.0f};             ///< Output volume

    /// Convenience: access decay time (alias for envelope.decay)
    float& decay;

    /// @}
    // -------------------------------------------------------------------------

    Kick()
        : decay(envelope.decayRef())
    {
        registerParam(pitch);
        registerParam(pitchEnv);
        registerParam(pitchDecay);
        registerParam(envelope);
        registerParam(click);
        registerParam(drive);
        registerParam(overtones);
        registerParam(attack);
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
    /// @name MidiReceiver Interface
    /// @{

    void midiNoteOn(uint8_t note, float velocity, uint8_t channel = 0) override;
    void midiNoteOff(uint8_t note, float velocity = 0.0f, uint8_t channel = 0) override;

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

    // Custom visualization for chain visualizer
    bool drawVisualization(VizDrawList* drawList,
                           float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

protected:
    void onTrigger() override;  // Called from audio thread

private:
    float softClip(float x) const;

    // State
    float m_velocity = 1.0f;
    float m_phase = 0.0f;
    float m_phase2 = 0.0f;      // 2nd harmonic phase
    float m_phase3 = 0.0f;      // 3rd harmonic phase
    float m_ampEnv = 0.0f;
    float m_pitchEnvValue = 0.0f;
    float m_clickEnv = 0.0f;
    float m_attackEnv = 1.0f;   // Attack fade-in (0->1)
    uint32_t m_attackSample = 0;
    uint32_t m_sampleRate = 48000;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
