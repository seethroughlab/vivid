#pragma once

/**
 * @file audio_effect.h
 * @brief Base class for all audio effects
 *
 * AudioEffect provides common functionality for audio processing:
 * - Named input connection to other audio operators
 * - Dry/wet mix control
 * - Bypass toggle
 */

#include <vivid/audio_operator.h>
#include <string>

namespace vivid::audio {

/**
 * @brief Base class for audio effects
 *
 * All effects inherit from this class, which provides:
 * - `input(name)` - Connect to an audio source by name
 * - `mix(amount)` - Control dry/wet blend (0=dry, 1=wet)
 * - `bypass(bool)` - Toggle effect bypass
 *
 * Subclasses implement:
 * - `processEffect(input, output, frames)` - Apply the effect algorithm
 * - `initEffect(ctx)` - Initialize effect-specific state
 *
 * @par Example
 * @code
 * chain.add<Delay>("delay")
 *     .input("videoAudio")
 *     .delayTime(250)
 *     .feedback(0.3f)
 *     .mix(0.4f);
 * @endcode
 */
class AudioEffect : public AudioOperator {
public:
    virtual ~AudioEffect() = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Connect to audio source by name
     * @param name Name of the source audio operator
     * @return Reference for chaining
     */
    AudioEffect& input(const std::string& name) {
        m_inputName = name;
        return *this;
    }

    /**
     * @brief Set dry/wet mix
     * @param amount Mix amount (0=fully dry, 1=fully wet)
     * @return Reference for chaining
     */
    AudioEffect& mix(float amount) {
        m_mix = std::max(0.0f, std::min(1.0f, amount));
        return *this;
    }

    /**
     * @brief Enable/disable effect bypass
     * @param b True to bypass (pass-through), false for normal operation
     * @return Reference for chaining
     *
     * When bypassed, the effect copies input directly to output (pass-through).
     * The effect still runs so the chain connections work correctly.
     */
    AudioEffect& bypass(bool b) {
        m_bypass = b;
        setBypassed(b);  // Also set base class for UI sync
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getMix() const { return m_mix; }
    bool isBypassed() const { return m_bypass; }
    const std::string& inputName() const { return m_inputName; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;

    /// @}

protected:
    /**
     * @brief Initialize effect-specific state
     * Override in subclass to set up DSP components
     */
    virtual void initEffect(Context& ctx) {}

    /**
     * @brief Apply the effect algorithm
     * @param input Input audio samples (interleaved stereo)
     * @param output Output buffer to write to
     * @param frames Number of frames to process
     *
     * Override in subclass to implement the effect.
     * The base class handles input connection, dry/wet mixing, and bypass.
     */
    virtual void processEffect(const float* input, float* output, uint32_t frames) = 0;

    /**
     * @brief Clean up effect-specific resources
     */
    virtual void cleanupEffect() {}

    // Input connection
    std::string m_inputName;
    AudioOperator* m_connectedInput = nullptr;

    // Mix and bypass control
    float m_mix = 1.0f;     // Fully wet by default
    bool m_bypass = false;  // Bypass state (pass-through when true)
};

} // namespace vivid::audio
