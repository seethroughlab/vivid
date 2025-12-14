#pragma once

/**
 * @file audio_mixer.h
 * @brief Simple audio mixer for combining multiple sources
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <array>

namespace vivid::audio {

/**
 * @brief Simple audio mixer
 *
 * Mixes up to 8 audio inputs into a single output. Each input has
 * its own gain control.
 *
 * @par Example
 * @code
 * chain.add<Kick>("kick");
 * chain.add<Snare>("snare");
 * chain.add<HiHat>("hihat");
 *
 * chain.add<AudioMixer>("mixer")
 *     .input(0, "kick")
 *     .input(1, "snare")
 *     .input(2, "hihat")
 *     .gain(0, 1.0f)
 *     .gain(1, 0.8f)
 *     .gain(2, 0.5f);
 *
 * chain.add<AudioOutput>("out").input("mixer");
 * @endcode
 */
class AudioMixer : public AudioOperator {
public:
    static constexpr int MAX_INPUTS = 8;

    Param<float> volume{"volume", 1.0f, 0.0f, 2.0f};  ///< Master output volume

    AudioMixer() {
        registerParam(volume);
    }
    ~AudioMixer() = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set input by index and name
     * @param index Input slot (0-7)
     * @param name Operator name
     */
    AudioMixer& input(int index, const std::string& name);

    /**
     * @brief Set gain for input
     * @param index Input slot (0-7)
     * @param g Gain (0-2, default 1.0)
     */
    AudioMixer& gain(int index, float g);


    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "AudioMixer"; }

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    // Input names and gains
    std::array<std::string, MAX_INPUTS> m_inputNames{};
    std::array<float, MAX_INPUTS> m_gains{};
    std::array<AudioOperator*, MAX_INPUTS> m_inputs{};

    bool m_initialized = false;
};

} // namespace vivid::audio
