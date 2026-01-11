#pragma once

/**
 * @file stretch.h
 * @brief Granular time-stretch effect
 *
 * Time-stretches audio without changing pitch using overlapping grains.
 * Triggered probabilistically at tempo-synced intervals.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/clock.h>
#include <vivid/audio/glitch/circular_buffer.h>
#include <vivid/audio/glitch/rate_utils.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <random>
#include <cmath>

namespace vivid::audio {

/**
 * @brief Granular time-stretch effect
 *
 * Uses overlapping grains to stretch or compress time while maintaining
 * the original pitch. When triggered, captures audio and plays it back
 * at a different rate using granular synthesis.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | bpm | float | 20-300 | 120 | Tempo for sync |
 * | chance | float | 0-1 | 0.3 | Trigger probability |
 * | stretchFactor | float | 0.25-4 | 2.0 | Time stretch ratio (2 = half speed) |
 * | grainSize | float | 10-200 | 50 | Grain size in ms |
 * | grainRandom | float | 0-0.5 | 0.1 | Grain position randomization |
 * | overlap | float | 0.25-0.75 | 0.5 | Grain overlap amount |
 * | mix | float | 0-1 | 1.0 | Wet/dry mix |
 *
 * @par Example
 * @code
 * auto& stretch = chain.add<Stretch>("stretch");
 * stretch.input("synth");
 * stretch.bpm = 120.0f;
 * stretch.triggerDiv(ClockDiv::Half);
 * stretch.stretchFactor = 2.0f;  // Half speed
 * stretch.grainSize = 60.0f;     // 60ms grains
 * stretch.chance = 0.25f;
 * @endcode
 *
 * @see modules/vivid-audio/examples/glitch-effects for complete example
 */
class Stretch : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    Param<float> chance{"chance", 0.3f, 0.0f, 1.0f};
    Param<float> stretchFactor{"stretchFactor", 2.0f, 0.25f, 4.0f};
    Param<float> grainSize{"grainSize", 50.0f, 10.0f, 200.0f};
    Param<float> grainRandom{"grainRandom", 0.1f, 0.0f, 0.5f};
    Param<float> overlap{"overlap", 0.5f, 0.25f, 0.75f};
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set trigger rate
     * @param div Clock division for trigger checks
     */
    void triggerDiv(ClockDiv div) { m_triggerDiv = div; }

    /**
     * @brief Set stretch duration
     * @param div Clock division for how long to stretch
     */
    void stretchDiv(ClockDiv div) { m_stretchDiv = div; }

    /// @}
    // -------------------------------------------------------------------------

    Stretch() : m_buffer(24.0f), m_rng(std::random_device{}()) {
        registerParam(bpm);
        registerParam(chance);
        registerParam(stretchFactor);
        registerParam(grainSize);
        registerParam(grainRandom);
        registerParam(overlap);
        registerParam(this->mix);
    }

    std::string name() const override { return "Stretch"; }

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;

private:
    static constexpr int MAX_GRAINS = 8;

    struct Grain {
        bool active = false;
        double sourcePos = 0.0;      // Position in source buffer
        double grainPhase = 0.0;     // 0-1 phase through grain
        uint32_t grainSamples = 0;   // Length of this grain in samples
    };

    CircularAudioBuffer m_buffer;
    Grain m_grains[MAX_GRAINS];
    int m_nextGrain = 0;

    // Trigger state
    ClockDiv m_triggerDiv = ClockDiv::Half;
    ClockDiv m_stretchDiv = ClockDiv::Quarter;
    double m_triggerPhase = 0.0;

    // Stretch state
    bool m_stretching = false;
    size_t m_stretchStart = 0;       // Start position in buffer
    uint32_t m_stretchLength = 0;    // Source length in samples
    double m_sourcePhase = 0.0;      // Current read position (0-1 through source)
    uint32_t m_stretchSamples = 0;   // Total output samples
    uint32_t m_stretchPos = 0;       // Current output position

    // Grain scheduling
    uint32_t m_grainInterval = 0;    // Samples between grain starts
    uint32_t m_grainCounter = 0;     // Counter until next grain

    // Random
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};
    float random() { return m_dist(m_rng); }

    // Hann window for smooth grain envelopes
    static float hannWindow(float phase) {
        return 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * phase));
    }

    void startGrain(uint32_t sampleRate);
};

} // namespace vivid::audio
