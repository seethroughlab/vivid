#pragma once

// Windows Media Foundation video decoder
// Only compiled on Windows

#if defined(_WIN32)

#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace vivid {
class Context;
}

namespace vivid::video {

class AudioPlayer;

/**
 * @brief Media Foundation video decoder for Windows.
 *
 * Uses Media Foundation Source Reader for hardware-accelerated video decoding.
 * Outputs RGBA pixels uploaded to a GPU texture.
 */
class MFDecoder {
public:
    MFDecoder();
    ~MFDecoder();

    // Non-copyable
    MFDecoder(const MFDecoder&) = delete;
    MFDecoder& operator=(const MFDecoder&) = delete;

    /**
     * @brief Open a video file.
     */
    bool open(Context& ctx, const std::string& path, bool loop = false);

    /**
     * @brief Close and release resources.
     */
    void close();

    /**
     * @brief Check if open.
     */
    bool isOpen() const;

    /**
     * @brief Update - decode next frame and upload to texture.
     */
    void update(Context& ctx);

    /**
     * @brief Seek to time.
     */
    void seek(float seconds);

    /**
     * @brief Pause playback.
     */
    void pause();

    /**
     * @brief Resume playback.
     */
    void play();

    bool isPlaying() const { return isPlaying_; }
    bool isFinished() const { return isFinished_; }
    float currentTime() const { return currentTime_; }
    float duration() const { return duration_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float frameRate() const { return frameRate_; }
    bool hasAudio() const { return hasAudio_; }

    void setVolume(float volume);
    float getVolume() const;

    WGPUTexture texture() const { return texture_; }
    WGPUTextureView textureView() const { return textureView_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Video info
    int width_ = 0;
    int height_ = 0;
    float duration_ = 0.0f;
    float frameRate_ = 30.0f;

    // Playback state
    bool isPlaying_ = false;
    bool isFinished_ = false;
    bool isLooping_ = false;
    bool hasAudio_ = false;
    float currentTime_ = 0.0f;
    float playbackTime_ = 0.0f;
    float nextFrameTime_ = 0.0f;
    std::string filePath_;

    // Pixel buffer for decoded frames
    std::vector<uint8_t> pixelBuffer_;

    // GPU resources
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;

    // Audio player
    std::unique_ptr<AudioPlayer> audioPlayer_;

    void createTexture();
    void resetReader();
};

} // namespace vivid::video

#endif // _WIN32
