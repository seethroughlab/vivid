#pragma once

/**
 * @file video_audio.h
 * @brief VideoAudio operator - extract audio from VideoPlayer
 *
 * Routes audio from VideoPlayer through the chain's audio system,
 * enabling audio processing and export integration.
 */

#include <vivid/audio_operator.h>
#include <string>

namespace vivid::video {

class VideoPlayer;

/**
 * @brief Extract audio from VideoPlayer for chain routing
 *
 * VideoAudio reads audio samples from a VideoPlayer and outputs them
 * as an AudioBuffer, allowing the audio to be:
 * - Processed by audio effect operators
 * - Routed through AudioOutput for speaker playback
 * - Captured by VideoExporter for audio muxing
 *
 * When VideoAudio is connected to a VideoPlayer, it automatically
 * disables the VideoPlayer's internal audio playback to avoid
 * double playback.
 *
 * @par Example
 * @code
 * chain.add<VideoPlayer>("video").file("movie.mov");
 * chain.add<VideoAudio>("videoAudio").source("video");
 * chain.add<AudioOutput>("audioOut").input("videoAudio");
 *
 * chain.output("video");           // Visual output
 * chain.audioOutput("audioOut");   // Audio output
 * @endcode
 */
class VideoAudio : public AudioOperator {
public:
    VideoAudio();
    ~VideoAudio() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set source VideoPlayer by name
     * @param videoOpName Name of the VideoPlayer operator
     * @return Reference for chaining
     */
    VideoAudio& source(const std::string& videoOpName);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "VideoAudio"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    std::string m_sourceName;
    VideoPlayer* m_videoPlayer = nullptr;
    bool m_connectedToSource = false;
};

} // namespace vivid::video
