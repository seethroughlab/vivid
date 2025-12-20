#pragma once

/**
 * @file audio_output.h
 * @brief AudioOutput operator for speaker playback
 *
 * AudioOutput is the terminal audio operator that sends audio to speakers.
 * It also provides the audio buffer for video export integration.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <memory>
#include <string>

namespace vivid {

// Forward declarations
class AudioGraph;

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
    /// @name Configuration
    /// @{

    /**
     * @brief Set input by operator name
     * @param name Name of audio operator to connect
     */
    void setInput(const std::string& name);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "AudioOutput"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    std::vector<ParamDecl> params() override {
        return { m_volumeParam.decl() };
    }

    bool getParam(const std::string& pname, float out[4]) override {
        if (pname == "volume") { out[0] = m_volume; return true; }
        return false;
    }

    bool setParam(const std::string& pname, const float value[4]) override {
        if (pname == "volume") { setVolume(value[0]); return true; }
        return false;
    }

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
    // -------------------------------------------------------------------------
    /// @name Audio Graph Integration
    /// @{

    /**
     * @brief Set the audio graph for pull-based generation
     *
     * In live mode, the miniaudio callback will pull samples directly
     * from this AudioGraph, bypassing the ring buffer.
     *
     * @param graph Pointer to AudioGraph (does not take ownership)
     */
    void setAudioGraph(AudioGraph* graph);

    /**
     * @brief Enable/disable recording mode
     *
     * In recording mode, audio is read from ring buffer instead of
     * being generated in the callback. The video exporter pushes
     * samples to the ring buffer.
     */
    void setRecordingMode(bool recording);

    /**
     * @brief Generate audio for video export (called from main thread)
     *
     * This generates audio synchronously, independent of the callback.
     * Used by video exporter to generate frame-aligned audio.
     *
     * @param output Output buffer (interleaved stereo)
     * @param frameCount Number of frames to generate
     */
    void generateForExport(float* output, uint32_t frameCount);

    /**
     * @brief Push samples to ring buffer for recording mode playback
     */
    void pushToRingBuffer(const float* samples, uint32_t sampleCount);

    /// @}

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    std::string m_inputName;
    AudioOperator* m_input = nullptr;  // Resolved input operator
    float m_volume = 1.0f;
    bool m_autoPlay = true;  // Auto-start playback on first audio

    // Parameter declarations for UI
    Param<float> m_volumeParam{"volume", 1.0f, 0.0f, 2.0f};
};

} // namespace vivid
