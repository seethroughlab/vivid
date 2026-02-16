#pragma once

/**
 * @file clang.h
 * @brief Cowbell/clave/woodblock synthesizer
 *
 * Two square oscillators at inharmonic ratios for metallic percussion.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/dsp/filters.h>
#include <vivid/audio/midi_receiver.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Cowbell/clave/woodblock synthesizer
 *
 * Generates cowbell, clave, and woodblock sounds using two square wave
 * oscillators at inharmonic frequency ratios, filtered to shape the
 * metallic character.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | pitch | float | 200-2000 | 800 | Base frequency in Hz |
 * | toneA | float | 0-1 | 0.7 | Level of oscillator A |
 * | toneB | float | 0-1 | 0.6 | Level of oscillator B |
 * | ratio | float | 1-4 | 1.5 | Frequency ratio A/B |
 * | filter | float | 0-1 | 0.5 | Bandpass filter amount |
 * | noise | float | 0-1 | 0.1 | Noise mix for body |
 * | decay | float | 0.02-0.5 | 0.1 | Amplitude decay |
 * | volume | float | 0-1 | 0.8 | Output volume |
 *
 * @par Example
 * @code
 * // Cowbell
 * auto& cowbell = chain.add<Clang>("cowbell");
 * cowbell.pitch = 800.0f;
 * cowbell.ratio = 1.5f;
 * cowbell.decay = 0.15f;
 *
 * // Clave
 * auto& clave = chain.add<Clang>("clave");
 * clave.pitch = 2000.0f;
 * clave.ratio = 1.0f;
 * clave.noise = 0.0f;
 * clave.decay = 0.05f;
 *
 * // Woodblock
 * auto& block = chain.add<Clang>("block");
 * block.pitch = 600.0f;
 * block.noise = 0.2f;
 * block.decay = 0.08f;
 *
 * // Trigger via setTriggerSource (audio-thread, sample-accurate):
 * chain.get<Clang>("cowbell")->setTriggerSource("seq");
 * @endcode
 *
 * @see FMDrum, Tom, HiHat, DrumStack
 */
class Clang : public AudioOperator, public MidiReceiver {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> pitch{"pitch", 800.0f, 200.0f, 2000.0f};    ///< Base frequency
    Param<float> toneA{"toneA", 0.7f, 0.0f, 1.0f};           ///< Oscillator A level
    Param<float> toneB{"toneB", 0.6f, 0.0f, 1.0f};           ///< Oscillator B level
    Param<float> ratio{"ratio", 1.5f, 1.0f, 4.0f};           ///< Frequency ratio
    Param<float> filter{"filter", 0.5f, 0.0f, 1.0f};         ///< Filter amount
    Param<float> noise{"noise", 0.1f, 0.0f, 1.0f};           ///< Noise mix
    Param<float> decay{"decay", 0.1f, 0.02f, 0.5f};          ///< Decay time
    Param<float> volume{"volume", 0.8f, 0.0f, 1.0f};         ///< Output volume

    /// @}
    // -------------------------------------------------------------------------

    Clang() {
        registerParam(pitch);
        registerParam(toneA);
        registerParam(toneB);
        registerParam(ratio);
        registerParam(filter);
        registerParam(noise);
        registerParam(decay);
        registerParam(volume);
    }
    ~Clang() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void reset();
    bool isActive() const { return m_env > 0.0001f; }

    float envelope() const { return m_env; }

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
    std::string name() const override { return "Clang"; }

    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    bool drawVisualization(VizDrawList* drawList,
                           float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

private:
    float generateNoise();

    // Velocity
    float m_velocity = 1.0f;

    // Oscillator phases
    float m_phaseA = 0.0f;
    float m_phaseB = 0.0f;

    // Envelope
    float m_env = 0.0f;

    // Noise state
    uint32_t m_seed = 22222;

    // Bandpass filter
    dsp::SVFFilter m_filter;

    uint32_t m_sampleRate = 48000;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
