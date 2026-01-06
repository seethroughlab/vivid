#pragma once

// FFmpeg video decoder for Linux
// Only compiled on Linux

#if !defined(_WIN32) && !defined(__APPLE__)

#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace vivid {
class Context;
}

namespace vivid::video {

/**
 * @brief FFmpeg video decoder for Linux.
 *
 * Uses FFmpeg/libav for video decoding on Linux.
 * Currently a stub - will be implemented when Linux support is needed.
 */
class FFmpegDecoder {
public:
    FFmpegDecoder();
    ~FFmpegDecoder();

    // Non-copyable
    FFmpegDecoder(const FFmpegDecoder&) = delete;
    FFmpegDecoder& operator=(const FFmpegDecoder&) = delete;

    bool open(Context& ctx, const std::string& path, bool loop = false);
    void close();
    bool isOpen() const;
    void update(Context& ctx);
    void seek(float seconds);
    void pause();
    void play();

    bool isPlaying() const { return isPlaying_; }
    bool isFinished() const { return isFinished_; }
    float currentTime() const { return currentTime_; }
    float duration() const { return duration_; }
    int width() const { return width_; }
    int height() const { return height_; }
    float frameRate() const { return frameRate_; }
    bool hasAudio() const { return hasAudio_; }
    uint32_t audioSampleRate() const { return audioSampleRate_; }
    uint32_t audioChannels() const { return audioChannels_; }

    void setVolume(float volume);
    float getVolume() const;

    // Audio sample reading (for external audio processing)
    uint32_t readAudioSamples(float* buffer, uint32_t maxFrames) { return 0; }
    void setInternalAudioEnabled(bool enable) { internalAudioEnabled_ = enable; }
    bool isInternalAudioEnabled() const { return internalAudioEnabled_; }

    WGPUTexture texture() const { return texture_; }
    WGPUTextureView textureView() const { return textureView_; }

private:
    int width_ = 0;
    int height_ = 0;
    float duration_ = 0.0f;
    float frameRate_ = 30.0f;
    bool isPlaying_ = false;
    bool isFinished_ = false;
    bool hasAudio_ = false;
    bool internalAudioEnabled_ = true;
    float currentTime_ = 0.0f;
    uint32_t audioSampleRate_ = 48000;
    uint32_t audioChannels_ = 2;

    WGPUTexture texture_ = nullptr;
    WGPUTextureView textureView_ = nullptr;
};

} // namespace vivid::video

#endif // Linux
