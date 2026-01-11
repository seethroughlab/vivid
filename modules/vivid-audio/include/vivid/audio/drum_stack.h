#pragma once

/**
 * @file drum_stack.h
 * @brief Drum layering operator
 *
 * Layers multiple drum sounds with individual mix and tuning.
 */

#include <vivid/audio_operator.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Drum layering operator
 *
 * Combines up to 3 drum sounds into a single composite drum hit.
 * Each layer has its own mix level. Triggering the DrumStack
 * triggers all connected drums simultaneously.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | mix1 | float | 0-1 | 1.0 | Layer 1 mix level |
 * | mix2 | float | 0-1 | 0.7 | Layer 2 mix level |
 * | mix3 | float | 0-1 | 0.5 | Layer 3 mix level |
 * | volume | float | 0-1 | 0.8 | Master output volume |
 *
 * @par Example
 * @code
 * // Create drums
 * auto& kick = chain.add<Kick>("kick");
 * auto& tom = chain.add<Tom>("tom");
 * auto& noise = chain.add<NoiseGen>("noise");
 *
 * // Create layered drum
 * auto& stack = chain.add<DrumStack>("layered");
 * stack.setLayer1("kick");
 * stack.setLayer2("tom");
 * stack.setLayer3("noise");
 * stack.mix1 = 1.0f;    // Full kick
 * stack.mix2 = 0.5f;    // Half tom
 * stack.mix3 = 0.2f;    // Subtle noise
 *
 * // Trigger fires all layers
 * stack.trigger();
 * @endcode
 *
 * @see Kick, Snare, Tom, FMDrum, Clang
 */
class DrumStack : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> mix1{"mix1", 1.0f, 0.0f, 1.0f};    ///< Layer 1 mix
    Param<float> mix2{"mix2", 0.7f, 0.0f, 1.0f};    ///< Layer 2 mix
    Param<float> mix3{"mix3", 0.5f, 0.0f, 1.0f};    ///< Layer 3 mix
    Param<float> volume{"volume", 0.8f, 0.0f, 1.0f}; ///< Master volume

    /// @}
    // -------------------------------------------------------------------------
    /// @name Layer Configuration
    /// @{

    /**
     * @brief Set layer 1 input operator
     */
    void setLayer1(const std::string& name) { m_layer1Name = name; }

    /**
     * @brief Set layer 2 input operator
     */
    void setLayer2(const std::string& name) { m_layer2Name = name; }

    /**
     * @brief Set layer 3 input operator
     */
    void setLayer3(const std::string& name) { m_layer3Name = name; }

    /// @}
    // -------------------------------------------------------------------------

    DrumStack() {
        registerParam(mix1);
        registerParam(mix2);
        registerParam(mix3);
        registerParam(volume);
    }
    ~DrumStack() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void reset();
    bool isActive() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "DrumStack"; }

    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    bool drawVisualization(VizDrawList* drawList,
                           float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

protected:
    void onTrigger() override;

private:
    void resolveInputs();

    std::string m_layer1Name;
    std::string m_layer2Name;
    std::string m_layer3Name;

    AudioOperator* m_layer1 = nullptr;
    AudioOperator* m_layer2 = nullptr;
    AudioOperator* m_layer3 = nullptr;

    bool m_inputsResolved = false;

    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
