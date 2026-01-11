#pragma once

/**
 * @file glitch.h
 * @brief Multi-effect glitch processor
 *
 * Combines all glitch effects (BeatRepeat, Reverse, Stutter, Scratch,
 * TapeStop, FrequencyShift) with per-effect probability controls.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/clock.h>
#include <vivid/audio/glitch/circular_buffer.h>
#include <vivid/audio/glitch/rate_utils.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <random>

namespace vivid::audio {

/**
 * @brief Multi-effect glitch processor
 *
 * A single operator that combines all glitch effects with individual
 * probability controls. Randomly selects and triggers effects based
 * on their chance settings. Only one effect plays at a time.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | bpm | float | 20-300 | 120 | Tempo for sync |
 * | repeatChance | float | 0-1 | 0.2 | BeatRepeat probability |
 * | reverseChance | float | 0-1 | 0.15 | Reverse probability |
 * | stutterChance | float | 0-1 | 0.15 | Stutter probability |
 * | scratchChance | float | 0-1 | 0.1 | Scratch probability |
 * | tapeChance | float | 0-1 | 0.08 | TapeStop probability |
 * | shiftChance | float | 0-1 | 0.1 | FrequencyShift probability |
 *
 * @par Example
 * @code
 * auto& glitch = chain.add<Glitch>("glitch");
 * glitch.input("drums");
 * glitch.bpm = 128.0f;
 * glitch.triggerDiv(ClockDiv::Quarter);
 * glitch.repeatChance = 0.25f;
 * glitch.stutterChance = 0.2f;
 * glitch.tapeChance = 0.1f;
 * @endcode
 *
 * @see modules/vivid-audio/examples/glitch-effects for complete example
 */
class Glitch : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};

    // Per-effect probabilities
    Param<float> repeatChance{"repeatChance", 0.2f, 0.0f, 1.0f};
    Param<float> reverseChance{"reverseChance", 0.15f, 0.0f, 1.0f};
    Param<float> stutterChance{"stutterChance", 0.15f, 0.0f, 1.0f};
    Param<float> scratchChance{"scratchChance", 0.1f, 0.0f, 1.0f};
    Param<float> tapeChance{"tapeChance", 0.08f, 0.0f, 1.0f};
    Param<float> shiftChance{"shiftChance", 0.1f, 0.0f, 1.0f};

    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set trigger rate (how often to check for glitch)
     */
    void triggerDiv(ClockDiv div) { m_triggerDiv = div; }

    /// @}
    // -------------------------------------------------------------------------

    Glitch() : m_buffer(24.0f), m_rng(std::random_device{}()) {
        registerParam(bpm);
        registerParam(repeatChance);
        registerParam(reverseChance);
        registerParam(stutterChance);
        registerParam(scratchChance);
        registerParam(tapeChance);
        registerParam(shiftChance);
        registerParam(this->mix);
    }

    std::string name() const override { return "Glitch"; }

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;

private:
    enum class ActiveEffect {
        None,
        Repeat,
        Reverse,
        Stutter,
        Scratch,
        TapeStop,
        FreqShift
    };

    CircularAudioBuffer m_buffer;
    ActiveEffect m_active = ActiveEffect::None;

    // Trigger clock
    ClockDiv m_triggerDiv = ClockDiv::Quarter;
    double m_triggerPhase = 0.0;

    // === BeatRepeat state ===
    size_t m_repeatStart = 0;
    uint32_t m_repeatLength = 0;
    double m_repeatPhase = 0.0;
    int m_repeatCount = 0;
    int m_repeatTotal = 4;
    float m_repeatGain = 1.0f;

    // === Reverse state ===
    size_t m_reverseStart = 0;
    uint32_t m_reverseLength = 0;
    double m_reversePhase = 0.0;

    // === Stutter state ===
    size_t m_stutterStart = 0;
    uint32_t m_stutterLength = 0;
    double m_stutterPhase = 0.0;
    int m_stutterCount = 0;
    int m_stutterTotal = 8;

    // === Scratch state ===
    size_t m_scratchStart = 0;
    uint32_t m_scratchLength = 0;
    double m_scratchPhase = 0.0;
    double m_scratchReadPos = 0.0;
    float m_scratchSpeed = 1.0f;
    int m_scratchDir = 1;

    // === TapeStop state ===
    double m_tapeRate = 1.0;
    double m_tapeReadPos = 0.0;
    uint32_t m_tapePhase = 0;
    uint32_t m_tapeStopLen = 0;
    uint32_t m_tapeStartLen = 0;
    bool m_tapeStopping = true;

    // === FreqShift state ===
    uint32_t m_shiftDuration = 0;
    uint32_t m_shiftPhase = 0;
    double m_shiftOscPhase = 0.0;
    float m_shiftAmount = 0.0f;
    // Simple allpass-based approximation for Hilbert
    float m_allpassL[4] = {0};
    float m_allpassR[4] = {0};

    // Random
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};
    float random() { return m_dist(m_rng); }

    // Effect selection
    ActiveEffect selectEffect();
    void startEffect(ActiveEffect effect, uint32_t sampleRate);
};

} // namespace vivid::audio
