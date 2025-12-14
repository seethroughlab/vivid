#pragma once

/**
 * @file audio_in.h
 * @brief Microphone/line-in audio capture operator
 *
 * AudioIn captures audio from the system's default input device (microphone)
 * and outputs it as an AudioBuffer for use in audio processing chains.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <memory>
#include <string>

namespace vivid::audio {

/**
 * @brief Captures audio from microphone or line-in
 *
 * AudioIn opens the system's default audio input device and captures
 * audio samples, making them available as an AudioBuffer output.
 *
 * @par Example
 * @code
 * // Capture mic input and apply effects
 * chain.add<AudioIn>("mic").volume(1.0f);
 * chain.add<Reverb>("reverb").input("mic").roomSize(0.7f);
 * chain.add<AudioOutput>("out").input("reverb");
 * chain.audioOutput("out");
 * @endcode
 *
 * @par Controls
 * - `volume(float)` - Input gain (0.0 to 2.0, default 1.0)
 * - `mute(bool)` - Mute input (default false)
 */
class AudioIn : public AudioOperator {
public:
    Param<float> volume{"volume", 1.0f, 0.0f, 2.0f};  ///< Input volume/gain

    AudioIn();
    ~AudioIn();

    // Non-copyable
    AudioIn(const AudioIn&) = delete;
    AudioIn& operator=(const AudioIn&) = delete;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Mute/unmute input
     * @param m True to mute, false to unmute
     * @return Reference for chaining
     */
    AudioIn& mute(bool m);

    /// @}
    // -------------------------------------------------------------------------
    /// @name State
    /// @{

    /// @brief Check if capture device is active
    bool isCapturing() const;

    /// @brief Get current volume
    float getVolume() const { return static_cast<float>(volume); }

    /// @brief Check if muted
    bool isMuted() const { return m_muted; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    void generateBlock(uint32_t frameCount) override;

    std::string name() const override { return "AudioIn"; }

    /// @}

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool m_muted = false;
    bool m_initialized = false;
};

} // namespace vivid::audio
