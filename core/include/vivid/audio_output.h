#pragma once

/**
 * @file audio_output.h
 * @brief AudioOutput operator for speaker playback
 *
 * AudioOutput is the terminal audio operator that sends audio to speakers.
 * It also provides the audio buffer for video export integration.
 */

#include <vivid/audio_operator.h>
#include <memory>
#include <string>

namespace vivid {

/**
 * @brief Audio output operator for speaker playback
 *
 * AudioOutput receives audio from connected AudioOperators and plays
 * it through the default audio device using miniaudio.
 *
 * @par Example
 * @code
 * chain.add<VideoAudio>("videoAudio").source("video");
 * chain.add<AudioOutput>("audioOut").input("videoAudio").volume(0.8f);
 * chain.audioOutput("audioOut");
 * @endcode
 *
 * When used with video export, the Chain will automatically capture
 * audio from this operator and mux it into the video file.
 */
class AudioOutput : public AudioOperator {
public:
    AudioOutput();
    ~AudioOutput() override;

    // Non-copyable
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // -------------------------------------------------------------------------
    /// @name Configuration (fluent interface)
    /// @{

    /**
     * @brief Set input by operator name
     * @param name Name of audio operator to connect
     * @return Reference for chaining
     */
    AudioOutput& input(const std::string& name);

    /**
     * @brief Set volume level
     * @param v Volume (0.0 = silent, 1.0 = full, 2.0 = +6dB boost)
     * @return Reference for chaining
     */
    AudioOutput& volume(float v);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "AudioOutput"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /// @brief Start audio playback
    void play();

    /// @brief Pause audio playback
    void pause();

    /// @brief Check if currently playing
    bool isPlaying() const;

    /// @brief Get current volume
    float getVolume() const { return m_volume; }

    /// @brief Set volume directly
    void setVolume(float v);

    /// @}

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    std::string m_inputName;
    float m_volume = 1.0f;
    bool m_initialized = false;
    bool m_autoPlay = true;  // Auto-start playback on first audio
};

} // namespace vivid
