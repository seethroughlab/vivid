#pragma once

/**
 * @file audio_filter.h
 * @brief Biquad filter for audio processing
 *
 * Standard biquad filter with lowpass, highpass, bandpass, and notch modes.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Filter types
 */
enum class FilterType {
    Lowpass,    ///< Passes frequencies below cutoff
    Highpass,   ///< Passes frequencies above cutoff
    Bandpass,   ///< Passes frequencies around cutoff
    Notch,      ///< Rejects frequencies around cutoff
    Lowshelf,   ///< Boost/cut below cutoff
    Highshelf,  ///< Boost/cut above cutoff
    Peak        ///< Boost/cut at cutoff (parametric EQ)
};

/**
 * @brief Biquad audio filter
 *
 * Standard biquad filter implementation with multiple filter types.
 * Essential for shaping noise into useful percussion sounds.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | cutoff | float | 20-20000 | 1000 | Cutoff frequency in Hz |
 * | resonance | float | 0.1-20 | 0.707 | Filter Q/resonance |
 * | gain | float | -24-24 | 0 | Gain in dB (shelf/peak only) |
 *
 * @par Example
 * @code
 * chain.add<NoiseGen>("noise").color(NoiseColor::White);
 * chain.add<AudioFilter>("filter")
 *     .input("noise")
 *     .type(FilterType::Highpass)
 *     .cutoff(8000.0f)
 *     .resonance(2.0f);
 * @endcode
 */
class AudioFilter : public AudioOperator {
public:
    AudioFilter() = default;
    ~AudioFilter() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set filter type
     * @param t FilterType
     */
    AudioFilter& type(FilterType t) { m_type = t; m_needsUpdate = true; return *this; }

    /**
     * @brief Set cutoff frequency
     * @param hz Cutoff in Hz (20-20000)
     */
    AudioFilter& cutoff(float hz) { m_cutoff = hz; m_needsUpdate = true; return *this; }

    /**
     * @brief Set resonance/Q
     * @param q Q value (0.1-20)
     */
    AudioFilter& resonance(float q) { m_resonance = q; m_needsUpdate = true; return *this; }

    /**
     * @brief Set gain (for shelf/peak filters)
     * @param dB Gain in decibels (-24 to +24)
     */
    AudioFilter& gain(float dB) { m_gain = dB; m_needsUpdate = true; return *this; }

    // Convenience methods
    AudioFilter& lowpass(float hz) { return type(FilterType::Lowpass).cutoff(hz); }
    AudioFilter& highpass(float hz) { return type(FilterType::Highpass).cutoff(hz); }
    AudioFilter& bandpass(float hz) { return type(FilterType::Bandpass).cutoff(hz); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "AudioFilter"; }

    std::vector<ParamDecl> params() override {
        return { m_cutoff.decl(), m_resonance.decl(), m_gain.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "cutoff") { out[0] = m_cutoff; return true; }
        if (name == "resonance") { out[0] = m_resonance; return true; }
        if (name == "gain") { out[0] = m_gain; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "cutoff") { m_cutoff = value[0]; m_needsUpdate = true; return true; }
        if (name == "resonance") { m_resonance = value[0]; m_needsUpdate = true; return true; }
        if (name == "gain") { m_gain = value[0]; m_needsUpdate = true; return true; }
        return false;
    }

    /// @}

private:
    void updateCoefficients();
    float processSample(float in, int channel);

    // Parameters
    FilterType m_type = FilterType::Lowpass;
    Param<float> m_cutoff{"cutoff", 1000.0f, 20.0f, 20000.0f};
    Param<float> m_resonance{"resonance", 0.707f, 0.1f, 20.0f};
    Param<float> m_gain{"gain", 0.0f, -24.0f, 24.0f};

    // Biquad coefficients
    float m_a0 = 1, m_a1 = 0, m_a2 = 0;
    float m_b0 = 1, m_b1 = 0, m_b2 = 0;

    // Filter state (per channel)
    float m_x1[2] = {0, 0}, m_x2[2] = {0, 0};  // Input history
    float m_y1[2] = {0, 0}, m_y2[2] = {0, 0};  // Output history

    uint32_t m_sampleRate = 48000;
    bool m_needsUpdate = true;
    bool m_initialized = false;
};

} // namespace vivid::audio
