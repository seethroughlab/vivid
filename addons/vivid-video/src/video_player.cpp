// Vivid Video - VideoPlayer Operator Implementation
// Auto-detects codec and uses appropriate decoder:
// - HAP: Direct DXT/BC texture upload (most efficient)
// - Other: AVFoundation decode to BGRA

#include <vivid/video/video_player.h>
#include <vivid/video/hap_decoder.h>
#include <vivid/video/avf_decoder.h>
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
    // Close existing decoders
    if (m_hapDecoder) {
        m_hapDecoder->close();
        m_hapDecoder.reset();
    }
    if (m_avfDecoder) {
        m_avfDecoder->close();
        m_avfDecoder.reset();
    }
    m_isHAP = false;

    if (m_filePath.empty()) {
        return;
    }

    // Check if it's a HAP file - use efficient DXT decoder
    if (HAPDecoder::isHAPFile(m_filePath)) {
        std::cout << "[VideoPlayer] Using HAP decoder (direct DXT upload)" << std::endl;
        m_hapDecoder = std::make_unique<HAPDecoder>();

        if (m_hapDecoder->open(ctx, m_filePath, m_loop)) {
            m_isHAP = true;
            m_output = m_hapDecoder->texture();
            m_outputView = m_hapDecoder->textureView();
            m_width = m_hapDecoder->width();
            m_height = m_hapDecoder->height();
            m_needsReload = false;

            if (m_autoPlay) {
                m_hapDecoder->play();
            }

            std::cout << "[VideoPlayer] Loaded: " << m_filePath
                      << " (" << m_width << "x" << m_height
                      << ", " << m_hapDecoder->duration() << "s)" << std::endl;
            return;
        }

        std::cerr << "[VideoPlayer] HAP decoder failed, falling back to AVFoundation" << std::endl;
        m_hapDecoder.reset();
    }

    // Use AVFoundation for standard codecs (H.264, MPEG2, ProRes, etc.)
    std::cout << "[VideoPlayer] Using AVFoundation decoder" << std::endl;
    m_avfDecoder = std::make_unique<AVFDecoder>();

    if (!m_avfDecoder->open(ctx, m_filePath, m_loop)) {
        std::cerr << "[VideoPlayer] Failed to open: " << m_filePath << std::endl;
        m_avfDecoder.reset();
        return;
    }

    m_output = m_avfDecoder->texture();
    m_outputView = m_avfDecoder->textureView();
    m_width = m_avfDecoder->width();
    m_height = m_avfDecoder->height();
    m_needsReload = false;

    if (m_autoPlay) {
        m_avfDecoder->play();
    }

    std::cout << "[VideoPlayer] Loaded: " << m_filePath
              << " (" << m_width << "x" << m_height
              << ", " << m_avfDecoder->duration() << "s)" << std::endl;
}

void VideoPlayer::process(Context& ctx) {
    // Check if we need to reload
    if (m_needsReload) {
        loadVideo(ctx);
    }

    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->update(ctx);
        m_outputView = m_hapDecoder->textureView();
    } else if (m_avfDecoder) {
        m_avfDecoder->update(ctx);
        m_outputView = m_avfDecoder->textureView();
    }
}

void VideoPlayer::cleanup() {
    if (m_hapDecoder) {
        m_hapDecoder->close();
        m_hapDecoder.reset();
    }
    if (m_avfDecoder) {
        m_avfDecoder->close();
        m_avfDecoder.reset();
    }
    m_isHAP = false;
    m_output = nullptr;
    m_outputView = nullptr;
}

VideoPlayer& VideoPlayer::volume(float v) {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->setVolume(v);
    } else if (m_avfDecoder) {
        m_avfDecoder->setVolume(v);
    }
    return *this;
}

void VideoPlayer::play() {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->play();
    } else if (m_avfDecoder) {
        m_avfDecoder->play();
    }
}

void VideoPlayer::pause() {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->pause();
    } else if (m_avfDecoder) {
        m_avfDecoder->pause();
    }
}

void VideoPlayer::seek(float seconds) {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->seek(seconds);
    } else if (m_avfDecoder) {
        m_avfDecoder->seek(seconds);
    }
}

bool VideoPlayer::isPlaying() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isPlaying();
    if (m_avfDecoder) return m_avfDecoder->isPlaying();
    return false;
}

bool VideoPlayer::isFinished() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isFinished();
    if (m_avfDecoder) return m_avfDecoder->isFinished();
    return true;
}

bool VideoPlayer::isOpen() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isOpen();
    if (m_avfDecoder) return m_avfDecoder->isOpen();
    return false;
}

float VideoPlayer::currentTime() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->currentTime();
    if (m_avfDecoder) return m_avfDecoder->currentTime();
    return 0.0f;
}

float VideoPlayer::duration() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->duration();
    if (m_avfDecoder) return m_avfDecoder->duration();
    return 0.0f;
}

float VideoPlayer::frameRate() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->frameRate();
    if (m_avfDecoder) return m_avfDecoder->frameRate();
    return 0.0f;
}

int VideoPlayer::videoWidth() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->width();
    if (m_avfDecoder) return m_avfDecoder->width();
    return 0;
}

int VideoPlayer::videoHeight() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->height();
    if (m_avfDecoder) return m_avfDecoder->height();
    return 0;
}

bool VideoPlayer::hasAudio() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->hasAudio();
    if (m_avfDecoder) return m_avfDecoder->hasAudio();
    return false;
}

} // namespace vivid::video
