#pragma once

// Vivid Video - VideoPlayer Operator
// Video playback as a texture source in chains
// Platform support:
//   - macOS: HAP (direct DXT) + AVFoundation (H.264, HEVC, ProRes, etc.)
//   - Windows: HAP (direct DXT) + Media Foundation (H.264, HEVC, etc.)
//   - Linux: HAP (direct DXT) + FFmpeg (stub - not yet implemented)

#include <vivid/effects/texture_operator.h>
#include <string>
#include <memory>

namespace vivid::video {
class HAPDecoder;

// Platform-specific standard codec decoders
#if defined(__APPLE__)
class AVFDecoder;
#elif defined(_WIN32)
class MFDecoder;
#else
class FFmpegDecoder;
#endif
}

namespace vivid::video {

/**
 * @brief Video playback operator for use in chains.
 *
 * Supports HAP-encoded videos for efficient GPU-compressed playback.
 * Standard codecs (H.264, HEVC) may be added in future.
 *
 * Usage:
 *   auto& video = chain->add<VideoPlayer>("video");
 *   video.file("assets/videos/my_video.mov")
 *        .loop(true);
 *
 *   // In update:
 *   video.play();  // or video.pause(), video.seek(seconds)
 */
class VideoPlayer : public effects::TextureOperator {
public:
    VideoPlayer();
    ~VideoPlayer() override;

    // Non-copyable
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    // -------------------------------------------------------------------------
    // Fluent Configuration API
    // -------------------------------------------------------------------------

    /**
     * @brief Set video file path (HAP-encoded MOV recommended)
     */
    VideoPlayer& file(const std::string& path) {
        m_filePath = path;
        m_needsReload = true;
        return *this;
    }

    /**
     * @brief Enable/disable looping
     */
    VideoPlayer& loop(bool enable) {
        m_loop = enable;
        return *this;
    }

    /**
     * @brief Set audio volume (0.0 - 1.0)
     */
    VideoPlayer& volume(float v);

    /**
     * @brief Set playback speed (1.0 = normal, 0.5 = half speed, etc.)
     * Note: Audio is muted when speed != 1.0
     */
    VideoPlayer& speed(float s) {
        m_speed = s;
        return *this;
    }

    // -------------------------------------------------------------------------
    // Playback Control
    // -------------------------------------------------------------------------

    /**
     * @brief Start/resume playback
     */
    void play();

    /**
     * @brief Pause playback
     */
    void pause();

    /**
     * @brief Seek to specific time in seconds
     */
    void seek(float seconds);

    /**
     * @brief Restart from beginning
     */
    void restart() { seek(0.0f); }

    // -------------------------------------------------------------------------
    // State Queries
    // -------------------------------------------------------------------------

    bool isPlaying() const;
    bool isFinished() const;
    bool isOpen() const;

    float currentTime() const;
    float duration() const;
    float frameRate() const;

    int videoWidth() const;
    int videoHeight() const;

    bool hasAudio() const;

    // -------------------------------------------------------------------------
    // Operator Interface
    // -------------------------------------------------------------------------

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "VideoPlayer"; }

private:
    void loadVideo(Context& ctx);

    std::string m_filePath;
    bool m_loop = false;
    float m_speed = 1.0f;
    bool m_needsReload = false;
    bool m_autoPlay = true;

    // Decoders - one will be active based on codec
    std::unique_ptr<HAPDecoder> m_hapDecoder;

    // Platform-specific standard codec decoder
#if defined(__APPLE__)
    std::unique_ptr<AVFDecoder> m_standardDecoder;
#elif defined(_WIN32)
    std::unique_ptr<MFDecoder> m_standardDecoder;
#else
    std::unique_ptr<FFmpegDecoder> m_standardDecoder;
#endif

    bool m_isHAP = false;
};

} // namespace vivid::video
