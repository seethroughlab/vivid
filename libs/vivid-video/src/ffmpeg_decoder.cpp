// FFmpeg Video Decoder for Linux
// Stub implementation - will use libavcodec/libavformat when Linux support is added

#if !defined(_WIN32) && !defined(__APPLE__)

#include <vivid/video/ffmpeg_decoder.h>
#include <iostream>

// FFmpeg headers would go here:
// extern "C" {
// #include <libavformat/avformat.h>
// #include <libavcodec/avcodec.h>
// #include <libswscale/swscale.h>
// }

namespace vivid::video {

FFmpegDecoder::FFmpegDecoder() = default;

FFmpegDecoder::~FFmpegDecoder() {
    close();
}

bool FFmpegDecoder::open(Context& ctx, const std::string& path, bool loop) {
    std::cerr << "[FFmpegDecoder] Linux video playback not yet implemented\n";
    std::cerr << "[FFmpegDecoder] Will use FFmpeg/libav in a future update\n";
    return false;
}

void FFmpegDecoder::close() {
    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }
    if (texture_) {
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }

    width_ = 0;
    height_ = 0;
    duration_ = 0.0f;
    isPlaying_ = false;
    isFinished_ = false;
    currentTime_ = 0.0f;
}

bool FFmpegDecoder::isOpen() const {
    return false;
}

void FFmpegDecoder::update(Context& ctx) {
    // Stub
}

void FFmpegDecoder::seek(float seconds) {
    // Stub
}

void FFmpegDecoder::pause() {
    isPlaying_ = false;
}

void FFmpegDecoder::play() {
    isPlaying_ = true;
}

void FFmpegDecoder::setVolume(float volume) {
    // Stub
}

float FFmpegDecoder::getVolume() const {
    return 1.0f;
}

} // namespace vivid::video

#endif // Linux
