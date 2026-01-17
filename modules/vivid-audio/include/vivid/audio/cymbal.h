#pragma once

/**
 * @file cymbal.h
 * @brief Crash/Ride cymbal synthesizer
 *
 * Extended cymbal with shimmer modulation and long decay.
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
 * @brief Crash/Ride cymbal synthesizer
 *
 * Generates crash and ride cymbal sounds using 12 inharmonic ring
 * oscillators and filtered noise. Features shimmer modulation for
 * characteristic cymbal wobble and very long decay times.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | decay | float | 0.5-10 | 3.0 | Decay time in seconds |
 * | tone | float | 0-1 | 0.5 | Brightness/high freq emphasis |
 * | pitch | float | 0.5-2 | 1.0 | Overall pitch multiplier |
 * | shimmer | float | 0-1 | 0.5 | Amplitude modulation amount |
 * | sizzle | float | 0-1 | 0.3 | High-freq noise content |
 * | volume | float | 0-1 | 0.7 | Output volume |
 *
 * @par Example
 * @code
 * // Crash cymbal
 * auto& crash = chain.add<Cymbal>("crash");
 * crash.decay = 4.0f;
 * crash.shimmer = 0.6f;
 *
 * // Ride cymbal (shorter, less shimmer)
 * auto& ride = chain.add<Cymbal>("ride");
 * ride.decay = 1.5f;
 * ride.shimmer = 0.2f;
 * ride.sizzle = 0.5f;
 *
 * chain.get<Cymbal>("crash")->trigger();
 * @endcode
 *
 * @see HiHat, Kick, Snare, DrumStack
 */
class Cymbal : public AudioOperator, public MidiReceiver {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> decay{"decay", 3.0f, 0.5f, 10.0f};       ///< Decay time
    Param<float> tone{"tone", 0.5f, 0.0f, 1.0f};          ///< Brightness
    Param<float> pitch{"pitch", 1.0f, 0.5f, 2.0f};        ///< Pitch multiplier
    Param<float> shimmer{"shimmer", 0.5f, 0.0f, 1.0f};    ///< Amplitude modulation
    Param<float> sizzle{"sizzle", 0.3f, 0.0f, 1.0f};      ///< High-freq noise
    Param<float> volume{"volume", 0.7f, 0.0f, 1.0f};      ///< Output volume

    /// @}
    // -------------------------------------------------------------------------

    Cymbal() {
        registerParam(decay);
        registerParam(tone);
        registerParam(pitch);
        registerParam(shimmer);
        registerParam(sizzle);
        registerParam(volume);
    }
    ~Cymbal() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void choke();  // Instantly stop
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
    std::string name() const override { return "Cymbal"; }

    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    bool drawVisualization(VizDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

protected:
    void onTrigger() override;

private:
    static constexpr int NUM_RINGS = 12;

    float generateNoise();

    // State
    float m_velocity = 1.0f;
    float m_env = 0.0f;
    uint32_t m_seed = 54321;

    // 12 ring oscillators for rich metallic sound
    float m_ringPhase[NUM_RINGS] = {0};

    // Shimmer LFO
    float m_shimmerPhase = 0.0f;

    // SVF filter for tone shaping
    dsp::SVFFilter m_filter;

    uint32_t m_sampleRate = 48000;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
