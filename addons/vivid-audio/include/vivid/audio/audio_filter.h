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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> cutoff{"cutoff", 1000.0f, 20.0f, 20000.0f};     ///< Cutoff frequency in Hz
    Param<float> resonance{"resonance", 0.707f, 0.1f, 20.0f};    ///< Filter Q/resonance
    Param<float> gain{"gain", 0.0f, -24.0f, 24.0f};              ///< Gain in dB (shelf/peak only)

    /// @}
    // -------------------------------------------------------------------------

    AudioFilter() {
        registerParam(cutoff);
        registerParam(resonance);
        registerParam(gain);
    }
    ~AudioFilter() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set filter type
     * @param t FilterType
     */
    void setType(FilterType t) { m_type = t; m_needsUpdate = true; }

    // Convenience methods
    void setLowpass(float hz) { cutoff = hz; setType(FilterType::Lowpass); }
    void setHighpass(float hz) { cutoff = hz; setType(FilterType::Highpass); }
    void setBandpass(float hz) { cutoff = hz; setType(FilterType::Bandpass); }

    // Visualization access
    FilterType filterType() const { return m_type; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "AudioFilter"; }

    // Custom visualization
    bool drawVisualization(ImDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

private:
    void updateCoefficients();
    float processSample(float in, int channel);

    // Filter type (enum, not a Param)
    FilterType m_type = FilterType::Lowpass;

    // Biquad coefficients
    float m_a0 = 1, m_a1 = 0, m_a2 = 0;
    float m_b0 = 1, m_b1 = 0, m_b2 = 0;

    // Filter state (per channel)
    float m_x1[2] = {0, 0}, m_x2[2] = {0, 0};  // Input history
    float m_y1[2] = {0, 0}, m_y2[2] = {0, 0};  // Output history

    // Cached values for detecting changes
    float m_cachedCutoff = 1000.0f;
    float m_cachedResonance = 0.707f;
    float m_cachedGain = 0.0f;

    uint32_t m_sampleRate = 48000;
    bool m_needsUpdate = true;
};

} // namespace vivid::audio
