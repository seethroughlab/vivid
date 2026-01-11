#pragma once

/**
 * @file frequency_shift.h
 * @brief Bode frequency shifter effect
 *
 * Shifts all frequencies by a fixed Hz amount using Hilbert transform.
 * Creates inharmonic, metallic, robotic sounds.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/clock.h>
#include <vivid/audio/glitch/rate_utils.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <array>
#include <cmath>

namespace vivid::audio {

/**
 * @brief Bode frequency shifter
 *
 * Unlike pitch shifting, frequency shifting adds a fixed Hz offset to all
 * frequencies. This breaks harmonic relationships, creating inharmonic,
 * metallic, or robotic timbres. Classic effect for electronic music.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | shift | float | -500 to 500 | 0 | Frequency shift in Hz |
 * | modDepth | float | 0-200 | 0 | LFO modulation depth in Hz |
 *
 * @par Example
 * @code
 * auto& freqShift = chain.add<FrequencyShift>("shift");
 * freqShift.input("synth");
 * freqShift.shift = 50.0f;  // Shift up 50 Hz
 * freqShift.bpm = 128.0f;
 * freqShift.modDiv(ClockDiv::Quarter);
 * freqShift.modDepth = 30.0f;  // LFO modulation
 * @endcode
 *
 * @see modules/vivid-audio/examples/glitch-effects for complete example
 */
class FrequencyShift : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> shift{"shift", 0.0f, -500.0f, 500.0f};      ///< Shift amount in Hz
    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    Param<float> modDepth{"modDepth", 0.0f, 0.0f, 200.0f};   ///< LFO depth in Hz
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set LFO modulation rate
     */
    void modDiv(ClockDiv div) { m_modDiv = div; }

    /// @}
    // -------------------------------------------------------------------------

    FrequencyShift() {
        registerParam(shift);
        registerParam(bpm);
        registerParam(modDepth);
        registerParam(this->mix);
        initHilbert();
    }

    std::string name() const override { return "FrequencyShift"; }

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;

private:
    // Hilbert transform FIR filter (31-tap)
    static constexpr int HILBERT_TAPS = 31;
    static constexpr int HILBERT_DELAY = HILBERT_TAPS / 2;

    std::array<float, HILBERT_TAPS> m_hilbertCoeffs;
    std::array<float, HILBERT_TAPS> m_delayLineL;  // Left channel delay line
    std::array<float, HILBERT_TAPS> m_delayLineR;  // Right channel delay line
    int m_delayIndex = 0;

    // Oscillator for frequency shifting
    double m_oscPhase = 0.0;

    // LFO for modulation
    ClockDiv m_modDiv = ClockDiv::Quarter;
    double m_lfoPhase = 0.0;

    void initHilbert();
    void processHilbert(float inL, float inR, float& iL, float& qL, float& iR, float& qR);
};

} // namespace vivid::audio
