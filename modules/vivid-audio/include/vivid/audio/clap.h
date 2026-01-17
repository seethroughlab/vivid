#pragma once

/**
 * @file clap.h
 * @brief Hand clap synthesizer
 *
 * Multiple noise bursts with slight timing variations.
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
 * @brief Hand clap synthesizer
 *
 * Generates clap sounds using multiple short noise bursts with slight
 * timing offsets to simulate multiple hands clapping. Bandpass filtered
 * for characteristic "clap" frequency range.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | decay | float | 0.05-1 | 0.3 | Overall decay time |
 * | tone | float | 0-1 | 0.5 | Brightness |
 * | sloppy | float | 0-1 | 0.5 | Timing spread between bursts |
 * | tail | float | 0-1 | 0.3 | Filtered noise tail amount |
 * | stereoWidth | float | 0-1 | 0 | Stereo spread of bursts |
 * | tune | float | 500-5000 | 1500 | Filter center frequency |
 *
 * @par Example
 * @code
 * chain.add<Clap>("clap")
 *     .decay(0.3f)
 *     .tone(0.6f)
 *     .spread(0.5f);
 *
 * chain.get<Clap>("clap")->trigger();
 * @endcode
 
 * @see Kick, Snare, HiHat, Sequencer, Clock, NoiseGen
 */
class Clap : public AudioOperator, public MidiReceiver {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> decay{"decay", 0.3f, 0.05f, 1.0f};            ///< Overall decay time
    Param<float> tone{"tone", 0.5f, 0.0f, 1.0f};               ///< Brightness
    Param<float> sloppy{"sloppy", 0.5f, 0.0f, 1.0f};           ///< Timing spread between bursts
    Param<float> tail{"tail", 0.3f, 0.0f, 1.0f};               ///< Filtered noise tail
    Param<float> stereoWidth{"stereoWidth", 0.0f, 0.0f, 1.0f}; ///< Stereo spread
    Param<float> tune{"tune", 1500.0f, 500.0f, 5000.0f};       ///< Filter center freq
    Param<float> volume{"volume", 0.8f, 0.0f, 1.0f};           ///< Output volume

    /// Alias for sloppy (backwards compatibility)
    Param<float>& spread = sloppy;

    /// @}
    // -------------------------------------------------------------------------

    Clap() {
        registerParam(decay);
        registerParam(tone);
        registerParam(sloppy);
        registerParam(tail);
        registerParam(stereoWidth);
        registerParam(tune);
        registerParam(volume);
    }
    ~Clap() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    // trigger() inherited from AudioOperator - queues to audio thread
    void reset();
    bool isActive() const { return m_env > 0.0001f; }

    // Envelope access for visualization
    float envelope() const { return m_env; }
    float burstEnvelope(int i) const { return (i >= 0 && i < NUM_BURSTS) ? m_burstEnv[i] : 0.0f; }

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
    std::string name() const override { return "Clap"; }

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    // Custom visualization
    bool drawVisualization(VizDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

protected:
    void onTrigger() override;  // Called from audio thread

private:
    float generateNoise();

    // State
    float m_velocity = 1.0f;
    float m_env = 0.0f;
    float m_tailEnv = 0.0f;
    uint32_t m_samplesSinceTrigger = 0;
    uint32_t m_seed = 11111;

    // Burst timing (4 quick bursts)
    static constexpr int NUM_BURSTS = 4;
    float m_burstEnv[NUM_BURSTS] = {0, 0, 0, 0};
    uint32_t m_burstDelay[NUM_BURSTS] = {0, 0, 0, 0};
    float m_burstPan[NUM_BURSTS] = {0, 0, 0, 0};  // -1 to 1 stereo position

    // SVF bandpass filter for clap character
    dsp::SVFFilter m_filter;
    // Separate filter for tail
    dsp::SVFFilter m_tailFilter;

    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
