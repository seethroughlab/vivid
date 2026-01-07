#pragma once

/**
 * @file decay.h
 * @brief One-shot decay envelope for percussion
 *
 * Simple envelope that decays from 1 to 0 - perfect for drums and percussive sounds.
 */

#include <vivid/audio_operator.h>
#include <vivid/operator_registry.h>
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
 
 * @see Envelope, AR, PitchEnv, Kick, Snare
 */
class Decay : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Self-Description
    /// @{

    static OperatorDescriptor describe() {
        return OperatorDescriptor("Decay", "Audio Envelope", "One-shot decay envelope with multiple curve shapes")
            .output(OutputKind::Audio)
            .inModule("vivid-audio")
            .withUsage(
                "auto& env = chain.add<Decay>(\"env\");\n"
                "env.input(\"noise\");\n"
                "env.time = 0.05f;                    // 50ms decay\n"
                "env.setCurve(DecayCurve::Exponential);\n"
                "\n"
                "env.trigger();  // Start decay\n"
            );
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> time{"time", 0.1f, 0.001f, 10.0f};  ///< Decay time in seconds

    /// @}
    // -------------------------------------------------------------------------

    Decay() {
        registerParam(time);
    }
    ~Decay() override = default;

    /// @brief Set decay curve type
    void setCurve(DecayCurve c) { m_curve = c; }

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void trigger();
    void reset();
    bool isActive() const { return m_value > 0.0001f; }
    float currentValue() const { return m_value; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Decay"; }

    /// @}

private:
    float computeValue(float progress) const;

    // Curve type (enum, not a Param)
    DecayCurve m_curve = DecayCurve::Exponential;

    // State
    float m_value = 0.0f;
    float m_progress = 1.0f;  // 0 = just triggered, 1 = finished
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
