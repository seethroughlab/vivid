#pragma once

/**
 * @file fm_drum.h
 * @brief 2-operator FM drum synthesizer
 *
 * FM percussion for metallic, bell-like sounds.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/dsp/filters.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief 2-operator FM drum synthesizer
 *
 * Generates metallic and bell-like percussion using 2-operator FM synthesis.
 * The modulator controls harmonic content while self-feedback adds edge
 * and metallic character.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | pitch | float | 100-2000 | 400 | Base frequency in Hz |
 * | ratio | float | 0.5-8 | 2.0 | Modulator/carrier ratio |
 * | amount | float | 0-1 | 0.5 | FM modulation depth |
 * | feedback | float | 0-1 | 0.2 | Modulator self-feedback |
 * | tone | float | 0-1 | 0.5 | Lowpass filter amount |
 * | decay | float | 0.05-2 | 0.3 | Amplitude decay time |
 * | modDecay | float | 0.01-0.5 | 0.1 | Modulation envelope decay |
 * | volume | float | 0-1 | 0.8 | Output volume |
 *
 * @par Example
 * @code
 * // Bell-like percussion
 * auto& bell = chain.add<FMDrum>("bell");
 * bell.pitch = 600.0f;
 * bell.ratio = 2.5f;
 * bell.amount = 0.6f;
 * bell.decay = 0.5f;
 *
 * // Metallic hit
 * auto& metal = chain.add<FMDrum>("metal");
 * metal.pitch = 200.0f;
 * metal.ratio = 3.5f;
 * metal.feedback = 0.5f;
 * metal.decay = 0.2f;
 *
 * chain.get<FMDrum>("bell")->trigger();
 * @endcode
 *
 * @see Kick, Snare, Tom, Clang, DrumStack
 */
class FMDrum : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> pitch{"pitch", 400.0f, 100.0f, 2000.0f};     ///< Base frequency
    Param<float> ratio{"ratio", 2.0f, 0.5f, 8.0f};            ///< Mod/carrier ratio
    Param<float> amount{"amount", 0.5f, 0.0f, 1.0f};          ///< FM modulation depth
    Param<float> feedback{"feedback", 0.2f, 0.0f, 1.0f};      ///< Modulator self-feedback
    Param<float> tone{"tone", 0.5f, 0.0f, 1.0f};              ///< Lowpass filter
    Param<float> decay{"decay", 0.3f, 0.05f, 2.0f};           ///< Amplitude decay
    Param<float> modDecay{"modDecay", 0.1f, 0.01f, 0.5f};     ///< Modulation decay
    Param<float> volume{"volume", 0.8f, 0.0f, 1.0f};          ///< Output volume

    /// @}
    // -------------------------------------------------------------------------

    FMDrum() {
        registerParam(pitch);
        registerParam(ratio);
        registerParam(amount);
        registerParam(feedback);
        registerParam(tone);
        registerParam(decay);
        registerParam(modDecay);
        registerParam(volume);
    }
    ~FMDrum() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void reset();
    bool isActive() const { return m_ampEnv > 0.0001f; }

    float ampEnvelope() const { return m_ampEnv; }
    float modEnvelope() const { return m_modEnv; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "FMDrum"; }

    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    bool drawVisualization(VizDrawList* drawList,
                           float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

protected:
    void onTrigger() override;

private:
    // Oscillator phases
    float m_carrierPhase = 0.0f;
    float m_modPhase = 0.0f;
    float m_feedbackSample = 0.0f;

    // Envelopes
    float m_ampEnv = 0.0f;
    float m_modEnv = 0.0f;

    // Lowpass filter
    dsp::SVFFilter m_filter;

    uint32_t m_sampleRate = 48000;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
