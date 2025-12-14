#pragma once

/**
 * @file audio_gain.h
 * @brief Simple gain/volume control for audio signals
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Simple gain/volume control
 *
 * Applies gain (amplification or attenuation) to an audio signal.
 * Simpler than AudioMixer when you just need to control the level
 * of a single audio source.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | gain | float | 0-4 | 1.0 | Gain multiplier (1.0 = unity) |
 * | pan | float | -1-1 | 0.0 | Stereo pan (-1=left, 0=center, 1=right) |
 * | mute | bool | - | false | Mute output |
 *
 * @par Example
 * @code
 * chain.add<Oscillator>("osc");
 * chain.get<Oscillator>("osc")->frequency = 440.0f;
 * chain.add<AudioGain>("vol").input("osc");
 * chain.get<AudioGain>("vol")->gain = 0.5f;
 * chain.get<AudioGain>("vol")->pan = -0.3f;
 * chain.add<AudioOutput>("out").input("vol");
 * @endcode
 */
class AudioGain : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> gain{"gain", 1.0f, 0.0f, 4.0f};    ///< Gain multiplier (1.0 = unity)
    Param<float> pan{"pan", 0.0f, -1.0f, 1.0f};     ///< Stereo pan position

    /// @}
    // -------------------------------------------------------------------------

    AudioGain() {
        registerParam(gain);
        registerParam(pan);
    }
    ~AudioGain() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Mute/unmute output
     * @param m true to mute
     */
    AudioGain& mute(bool m) { m_mute = m; return *this; }

    /**
     * @brief Connect gain modulation input by name
     * @param name Name of the modulation source (e.g., envelope)
     *
     * The modulation source's output value will multiply the gain.
     * Typically used with Envelope operators for amplitude modulation.
     */
    AudioGain& gainInput(const std::string& name) {
        m_gainInputName = name;
        return *this;
    }

    // Override input to return AudioGain& for chaining
    AudioGain& input(const std::string& name) {
        AudioEffect::input(name);
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "AudioGain"; }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;

private:
    bool m_mute = false;

    // Gain modulation input (e.g., envelope)
    std::string m_gainInputName;
    Operator* m_gainInputOp = nullptr;
};

} // namespace vivid::audio
