#pragma once

/**
 * @file decay.h
 * @brief One-shot decay envelope for percussion
 *
 * Simple envelope that decays from 1 to 0 - perfect for drums and percussive sounds.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Decay curve types
 */
enum class DecayCurve {
    Linear,         ///< Linear decay
    Exponential,    ///< Natural exponential decay (default)
    Logarithmic     ///< Slow start, fast end
};

/**
 * @brief One-shot decay envelope
 *
 * Simplified envelope for percussive sounds. When triggered, immediately
 * jumps to 1.0 and decays to 0 over the specified time. Simpler and more
 * efficient than full ADSR for drums.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | time | float | 0.001-10 | 0.1 | Decay time in seconds |
 *
 * @par Example
 * @code
 * chain.add<NoiseGen>("noise").color(NoiseColor::White);
 * chain.add<Decay>("env")
 *     .input("noise")
 *     .time(0.05f)
 *     .curve(DecayCurve::Exponential);
 *
 * // Trigger on beat
 * chain.get<Decay>("env")->trigger();
 * @endcode
 */
class Decay : public AudioOperator {
public:
    Decay() = default;
    ~Decay() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set decay time
     * @param seconds Decay time (0.001-10 seconds)
     */
    Decay& time(float seconds) { m_time = seconds; return *this; }

    /**
     * @brief Set decay curve type
     * @param c DecayCurve (Linear, Exponential, Logarithmic)
     */
    Decay& curve(DecayCurve c) { m_curve = c; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Trigger the envelope (jump to 1.0, start decay)
     */
    void trigger();

    /**
     * @brief Reset to idle state
     */
    void reset();

    /**
     * @brief Check if envelope is active
     */
    bool isActive() const { return m_value > 0.0001f; }

    /**
     * @brief Get current envelope value
     */
    float currentValue() const { return m_value; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Decay"; }

    std::vector<ParamDecl> params() override {
        return { m_time.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "time") { out[0] = m_time; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "time") { m_time = value[0]; return true; }
        return false;
    }

    /// @}

private:
    float computeValue(float progress) const;

    // Parameters
    Param<float> m_time{"time", 0.1f, 0.001f, 10.0f};
    DecayCurve m_curve = DecayCurve::Exponential;

    // State
    float m_value = 0.0f;
    float m_progress = 1.0f;  // 0 = just triggered, 1 = finished
    uint32_t m_sampleRate = 48000;

    bool m_initialized = false;
};

} // namespace vivid::audio
