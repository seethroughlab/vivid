#pragma once

/**
 * @file video_audio.h
 * @brief VideoAudio operator - extract audio from VideoPlayer
 *
 * Routes audio from VideoPlayer through the chain's audio system,
 * enabling audio processing and export integration.
 */

#include <vivid/audio_operator.h>
#include <vivid/operator_registry.h>
#include <vivid/video/export.h>
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
class VIVID_VIDEO_API VideoAudio : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Self-Description
    /// @{

    static OperatorDescriptor describe() {
        return OperatorDescriptor("VideoAudio", "Video", "Extract audio from VideoPlayer for chain routing")
            .output(OutputKind::Audio)
            .withAliases({"VideoSound", "MovieAudio"})
            .withUsage(
                "chain.add<VideoPlayer>(\"video\").file(\"movie.mov\");\n"
                "chain.add<VideoAudio>(\"audio\").setSource(\"video\");\n"
                "chain.add<AudioOutput>(\"out\").input(\"audio\");\n"
                "\n"
                "chain.output(\"video\");       // Visual\n"
                "chain.audioOutput(\"out\");    // Audio\n"
            )
            .withExamples({{"examples/video-audio"}});
    }

    /// @}

    VideoAudio();
    ~VideoAudio() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set source VideoPlayer by name
     * @param videoOpName Name of the VideoPlayer operator
     */
    void setSource(const std::string& videoOpName);

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
