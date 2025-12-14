#pragma once

/**
 * @file audio_file.h
 * @brief AudioFile operator - load and play audio files
 *
 * Loads WAV audio files and outputs samples to the audio chain.
 * Supports looping and basic transport controls.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Audio file playback operator
 *
 * Loads WAV files and outputs audio samples for processing
 * through the audio effects chain.
 *
 * @par Supported formats
 * - WAV (16-bit, 24-bit, 32-bit float)
 * - Mono or stereo
 * - Any sample rate (resampled to 48kHz)
 *
 * @par Example
 * @code
 * chain.add<AudioFile>("music")
 *     .file("assets/audio/loop.wav")
 *     .loop(true)
 *     .volume(0.8f);
 *
 * chain.add<Reverb>("reverb").input("music").roomSize(0.5f);
 * chain.add<AudioOutput>("out").input("reverb");
 * chain.audioOutput("out");
 * @endcode
 */
class AudioFile : public AudioOperator {
public:
    Param<float> volume{"volume", 1.0f, 0.0f, 1.0f};  ///< Playback volume

    AudioFile();
    ~AudioFile() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set audio file path
     * @param path Path to WAV file
     * @return Reference for chaining
     */
    AudioFile& file(const std::string& path);

    /**
     * @brief Enable/disable looping
     * @param enable True for looping playback
     * @return Reference for chaining
     */
    AudioFile& loop(bool enable) {
        m_loop = enable;
        return *this;
    }


    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void play();
    void pause();
    void stop();
    void seek(float seconds);

    bool isPlaying() const { return m_playing; }
    bool isLooping() const { return m_loop; }
    float currentTime() const;
    float duration() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "AudioFile"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    bool loadWAV(const std::string& path);

    std::string m_filePath;
    bool m_loop = false;
    bool m_playing = false;
    bool m_needsLoad = false;

    // Audio data (resampled to 48kHz stereo)
    std::vector<float> m_samples;
    uint32_t m_sampleRate = 48000;
    uint32_t m_channels = 2;
    uint64_t m_playPosition = 0;
    uint64_t m_totalFrames = 0;
};

} // namespace vivid::audio
