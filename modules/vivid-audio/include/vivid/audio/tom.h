#pragma once

/**
 * @file tom.h
 * @brief Tom drum synthesizer
 *
 * Tom drum with resonant body filter and pitch envelope.
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
 * @brief Tom drum synthesizer
 *
 * Generates tom drums using a sine oscillator with pitch envelope,
 * shaped by a resonant bandpass filter for characteristic tom timbre.
 * Can produce low floor toms to high rack toms.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | pitch | float | 40-400 | 100 | Base pitch in Hz |
 * | bend | float | 0-1 | 0.5 | Pitch envelope amount |
 * | bendTime | float | 0.01-0.3 | 0.08 | Pitch envelope time |
 * | color | float | 0-1 | 0.3 | Harmonic content |
 * | tone | float | 0-1 | 0.6 | Filter resonance/character |
 * | decay | float | 0.1-2 | 0.4 | Amplitude decay time |
 * | volume | float | 0-1 | 0.8 | Output volume |
 *
 * @par Example
 * @code
 * // Low floor tom
 * auto& floorTom = chain.add<Tom>("floor");
 * floorTom.pitch = 80.0f;
 * floorTom.bend = 0.6f;
 * floorTom.decay = 0.6f;
 *
 * // High rack tom
 * auto& rackTom = chain.add<Tom>("rack");
 * rackTom.pitch = 180.0f;
 * rackTom.bend = 0.4f;
 * rackTom.decay = 0.3f;
 *
 * chain.get<Tom>("floor")->trigger();
 * @endcode
 *
 * @see Kick, Snare, Cymbal, DrumStack
 */
class Tom : public AudioOperator, public MidiReceiver {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> pitch{"pitch", 100.0f, 40.0f, 400.0f};      ///< Base pitch in Hz
    Param<float> bend{"bend", 0.5f, 0.0f, 1.0f};             ///< Pitch envelope amount
    Param<float> bendTime{"bendTime", 0.08f, 0.01f, 0.3f};   ///< Pitch envelope decay
    Param<float> color{"color", 0.3f, 0.0f, 1.0f};           ///< Harmonic content
    Param<float> tone{"tone", 0.6f, 0.0f, 1.0f};             ///< Filter resonance
    Param<float> decay{"decay", 0.4f, 0.1f, 2.0f};           ///< Amplitude decay
    Param<float> volume{"volume", 0.8f, 0.0f, 1.0f};         ///< Output volume

    /// @}
    // -------------------------------------------------------------------------

    Tom() {
        registerParam(pitch);
        registerParam(bend);
        registerParam(bendTime);
        registerParam(color);
        registerParam(tone);
        registerParam(decay);
        registerParam(volume);
    }
    ~Tom() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void reset();
    bool isActive() const { return m_ampEnv > 0.0001f; }

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
    std::string name() const override { return "Tom"; }

    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    bool drawVisualization(VizDrawList* drawList,
                           float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

protected:
    void onTrigger() override;

private:
    // State
    float m_velocity = 1.0f;
    float m_phase = 0.0f;
    float m_phase2 = 0.0f;
    float m_phase3 = 0.0f;
    float m_ampEnv = 0.0f;
    float m_pitchEnvValue = 0.0f;
    uint32_t m_sampleRate = 48000;

    // Resonant bandpass filter for tom body
    dsp::SVFFilter m_filter;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
