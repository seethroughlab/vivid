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
 * chain.add<Oscillator>("osc").frequency(440.0f);
 * chain.add<AudioGain>("vol").input("osc").gain(0.5f).pan(-0.3f);
 * chain.add<AudioOutput>("out").input("vol");
 * @endcode
 */
class AudioGain : public AudioEffect {
public:
    AudioGain() = default;
    ~AudioGain() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set gain level
     * @param g Gain multiplier (0-4, 1.0 = unity gain)
     */
    AudioGain& gain(float g) { m_gain = g; return *this; }

    /**
     * @brief Set stereo pan position
     * @param p Pan (-1 = full left, 0 = center, 1 = full right)
     */
    AudioGain& pan(float p) { m_pan = p; return *this; }

    /**
     * @brief Mute/unmute output
     * @param m true to mute
     */
    AudioGain& mute(bool m) { m_mute = m; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "AudioGain"; }

    std::vector<ParamDecl> params() override {
        return { m_gain.decl(), m_pan.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "gain") { out[0] = m_gain; return true; }
        if (name == "pan") { out[0] = m_pan; return true; }
        return AudioEffect::getParam(name, out);
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "gain") { m_gain = value[0]; return true; }
        if (name == "pan") { m_pan = value[0]; return true; }
        return AudioEffect::setParam(name, value);
    }

    /// @}

protected:
    void processEffect(const float* input, float* output, uint32_t frames) override;

private:
    Param<float> m_gain{"gain", 1.0f, 0.0f, 4.0f};
    Param<float> m_pan{"pan", 0.0f, -1.0f, 1.0f};
    bool m_mute = false;
};

} // namespace vivid::audio
