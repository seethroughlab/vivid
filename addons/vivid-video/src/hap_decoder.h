#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

// Forward declarations for Diligent types
namespace Diligent {
    struct IRenderDevice;
    struct IDeviceContext;
    struct ITexture;
    struct ITextureView;
}

namespace vivid {
class Context;
}

namespace vivid::video {

class AudioPlayer;

/**
 * @brief HAP video decoder using Vidvox HAP library.
 *
 * Uses AVFoundation to demux the MOV container and extract raw HAP frame data,
 * then uses the Vidvox HAP library to decompress to DXT-compressed textures.
 * DXT data is uploaded directly to GPU as BC1/BC3 compressed textures,
 * avoiding CPU pixel conversion entirely.
 *
 * Audio is decoded via AVFoundation's AVAssetReader.
 */
class HAPDecoder {
public:
    HAPDecoder();
    ~HAPDecoder();

    // Non-copyable
    HAPDecoder(const HAPDecoder&) = delete;
    HAPDecoder& operator=(const HAPDecoder&) = delete;

    /**
     * @brief Check if a file is a HAP-encoded video.
     */
    static bool isHAPFile(const std::string& path);

    /**
     * @brief Open a HAP video file.
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

    /**
     * @brief Check if playing.
     */
    bool isPlaying() const { return isPlaying_; }

    /**
     * @brief Check if finished.
     */
    bool isFinished() const { return isFinished_; }

    /**
     * @brief Get current time in seconds.
     */
    float currentTime() const { return currentTime_; }

    /**
     * @brief Get duration in seconds.
     */
    float duration() const { return duration_; }

    /**
     * @brief Get video width.
     */
    int width() const { return width_; }

    /**
     * @brief Get video height.
     */
    int height() const { return height_; }

    /**
     * @brief Get frame rate.
     */
    float frameRate() const { return frameRate_; }

    /**
     * @brief Check if file has audio.
     */
    bool hasAudio() const { return hasAudio_; }

    /**
     * @brief Set audio volume (0.0 - 1.0).
     */
    void setVolume(float volume);

    /**
     * @brief Get audio volume.
     */
    float getVolume() const;

    /**
     * @brief Get texture.
     */
    Diligent::ITexture* texture() const;

    /**
     * @brief Get texture view.
     */
    Diligent::ITextureView* textureView() const;

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

    // DXT buffer for decoded frames
    std::vector<uint8_t> dxtBuffer_;

    // GPU resources
    Diligent::IRenderDevice* device_ = nullptr;
    Diligent::IDeviceContext* context_ = nullptr;
    Diligent::ITexture* texture_ = nullptr;
    Diligent::ITextureView* srv_ = nullptr;

    // Audio player
    std::unique_ptr<AudioPlayer> audioPlayer_;

    void prebufferAudio();
};

} // namespace vivid::video
