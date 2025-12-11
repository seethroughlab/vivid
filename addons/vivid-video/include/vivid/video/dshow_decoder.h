#pragma once

// DirectShow Video Decoder for Windows
// Fallback decoder for codecs not supported by Media Foundation
// Relies on system-installed DirectShow filters (e.g., LAV Filters, K-Lite)

#if defined(_WIN32)

#include <vivid/video/export.h>
#include <webgpu/webgpu.h>
#include <string>
#include <memory>
#include <vector>

namespace vivid {
class Context;
}

namespace vivid::video {

class AudioPlayer;

/**
 * @brief DirectShow-based video decoder for Windows
 *
 * Uses the DirectShow filter graph to decode video formats that
 * Media Foundation doesn't support natively. Requires appropriate
 * DirectShow filters to be installed (e.g., LAV Filters for ProRes).
 */
class VIVID_VIDEO_API DShowDecoder {
public:
    DShowDecoder();
    ~DShowDecoder();

    // Non-copyable
    DShowDecoder(const DShowDecoder&) = delete;
    DShowDecoder& operator=(const DShowDecoder&) = delete;

    /**
     * @brief Check if this decoder can handle the given file
     * @param path Path to video file
     * @return true if DirectShow can likely decode this file
     */
    static bool canDecode(const std::string& path);

    bool open(Context& ctx, const std::string& path, bool loop);
    void close();
    bool isOpen() const;

    void update(Context& ctx);
    void seek(float seconds);

    void pause();
    void play();
    void setVolume(float volume);
    float getVolume() const;

    // Accessors
    WGPUTexture texture() const { return texture_; }
    WGPUTextureView textureView() const { return textureView_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float duration() const { return duration_; }
    float frameRate() const { return frameRate_; }
    float currentTime() const { return currentTime_; }
    bool isPlaying() const { return isPlaying_; }
    bool isFinished() const { return isFinished_; }
    bool hasAudio() const { return hasAudio_; }

    // Audio extraction
    uint32_t readAudioSamples(float* buffer, uint32_t maxFrames);
    void setInternalAudioEnabled(bool enable);
    bool isInternalAudioEnabled() const;
    uint32_t audioSampleRate() const;
    uint32_t audioChannels() const;

private:
    void createTexture();
    void resetPlayback();

    struct Impl;
    std::unique_ptr<Impl> impl_;

    // GPU resources
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;

    // Video properties
    int width_ = 0;
    int height_ = 0;
    float duration_ = 0.0f;
    float frameRate_ = 30.0f;
    float currentTime_ = 0.0f;
    bool isPlaying_ = false;
    bool isFinished_ = false;
    bool isLooping_ = false;

    // Audio
    bool hasAudio_ = false;
    bool internalAudioEnabled_ = true;
    uint32_t audioSampleRate_ = 48000;
    uint32_t audioChannels_ = 2;
    std::unique_ptr<AudioPlayer> audioPlayer_;

    // Frame buffer
    std::vector<uint8_t> pixelBuffer_;
    std::string filePath_;
};

} // namespace vivid::video

#endif // _WIN32
