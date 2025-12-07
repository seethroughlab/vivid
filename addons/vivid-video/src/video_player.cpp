// Vivid Video - VideoPlayer Operator Implementation

#include <vivid/video/video_player.h>
#include <vivid/video/hap_decoder.h>
#include <vivid/context.h>
#include <iostream>

namespace vivid::video {

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer() {
    cleanup();
}

void VideoPlayer::init(Context& ctx) {
    if (!m_filePath.empty()) {
        loadVideo(ctx);
    }
}

void VideoPlayer::loadVideo(Context& ctx) {
    // Close existing video
    if (m_decoder) {
        m_decoder.reset();
    }

    if (m_filePath.empty()) {
        return;
    }

    // Check if it's a HAP file
    if (!HAPDecoder::isHAPFile(m_filePath)) {
        std::cerr << "[VideoPlayer] Not a HAP file: " << m_filePath << std::endl;
        std::cerr << "[VideoPlayer] Only HAP-encoded MOV files are currently supported." << std::endl;
        std::cerr << "[VideoPlayer] Use FFmpeg to convert: ffmpeg -i input.mp4 -c:v hap output.mov" << std::endl;
        setError("Only HAP-encoded videos are supported");
        return;
    }

    m_decoder = std::make_unique<HAPDecoder>();

    if (!m_decoder->open(ctx, m_filePath, m_loop)) {
        std::cerr << "[VideoPlayer] Failed to open: " << m_filePath << std::endl;
        setError("Failed to open video file");
        m_decoder.reset();
        return;
    }

    // Update output texture to use the decoder's texture
    m_outputTexture = m_decoder->texture();
    m_outputView = m_decoder->textureView();
    m_width = m_decoder->width();
    m_height = m_decoder->height();

    m_needsReload = false;

    // Auto-play if configured
    if (m_autoPlay) {
        m_decoder->play();
    }

    std::cout << "[VideoPlayer] Loaded: " << m_filePath
              << " (" << m_width << "x" << m_height
              << ", " << m_decoder->duration() << "s)" << std::endl;
}

void VideoPlayer::process(Context& ctx) {
    // Check if we need to reload
    if (m_needsReload) {
        loadVideo(ctx);
    }

    if (!m_decoder) {
        return;
    }

    // Update video playback
    m_decoder->update(ctx);

    // Update our output texture reference (in case it changed)
    m_outputView = m_decoder->textureView();
}

void VideoPlayer::cleanup() {
    if (m_decoder) {
        m_decoder->close();
        m_decoder.reset();
    }
    m_outputTexture = nullptr;
    m_outputView = nullptr;
}

VideoPlayer& VideoPlayer::volume(float v) {
    if (m_decoder) {
        m_decoder->setVolume(v);
    }
    return *this;
}

void VideoPlayer::play() {
    if (m_decoder) {
        m_decoder->play();
    }
}

void VideoPlayer::pause() {
    if (m_decoder) {
        m_decoder->pause();
    }
}

void VideoPlayer::seek(float seconds) {
    if (m_decoder) {
        m_decoder->seek(seconds);
    }
}

bool VideoPlayer::isPlaying() const {
    return m_decoder ? m_decoder->isPlaying() : false;
}

bool VideoPlayer::isFinished() const {
    return m_decoder ? m_decoder->isFinished() : true;
}

bool VideoPlayer::isOpen() const {
    return m_decoder ? m_decoder->isOpen() : false;
}

float VideoPlayer::currentTime() const {
    return m_decoder ? m_decoder->currentTime() : 0.0f;
}

float VideoPlayer::duration() const {
    return m_decoder ? m_decoder->duration() : 0.0f;
}

float VideoPlayer::frameRate() const {
    return m_decoder ? m_decoder->frameRate() : 0.0f;
}

int VideoPlayer::videoWidth() const {
    return m_decoder ? m_decoder->width() : 0;
}

int VideoPlayer::videoHeight() const {
    return m_decoder ? m_decoder->height() : 0;
}

bool VideoPlayer::hasAudio() const {
    return m_decoder ? m_decoder->hasAudio() : false;
}

} // namespace vivid::video
