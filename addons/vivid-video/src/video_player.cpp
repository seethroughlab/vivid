// Vivid Video - VideoPlayer Operator Implementation
// Auto-detects codec and uses appropriate decoder:
// - HAP: Direct DXT/BC texture upload (most efficient, cross-platform)
// - Standard codecs: Platform-specific decoder
//   - macOS: AVFoundation (H.264, HEVC, ProRes, MPEG2, etc.)
//   - Windows: Media Foundation (H.264, HEVC, etc.)
//   - Linux: FFmpeg (stub - not yet implemented)

#include <vivid/video/video_player.h>
#include <vivid/video/hap_decoder.h>
#include <vivid/context.h>
#include <iostream>

// Platform-specific decoder includes
#if defined(__APPLE__)
#include <vivid/video/avf_decoder.h>
#define STANDARD_DECODER_NAME "AVFoundation"
using StandardDecoder = vivid::video::AVFDecoder;
#elif defined(_WIN32)
#include <vivid/video/mf_decoder.h>
#define STANDARD_DECODER_NAME "Media Foundation"
using StandardDecoder = vivid::video::MFDecoder;
#else
#include <vivid/video/ffmpeg_decoder.h>
#define STANDARD_DECODER_NAME "FFmpeg"
using StandardDecoder = vivid::video::FFmpegDecoder;
#endif

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
    if (m_standardDecoder) {
        m_standardDecoder->close();
        m_standardDecoder.reset();
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

        std::cerr << "[VideoPlayer] HAP decoder failed, falling back to " << STANDARD_DECODER_NAME << std::endl;
        m_hapDecoder.reset();
    }

    // Use platform-specific decoder for standard codecs
    std::cout << "[VideoPlayer] Using " << STANDARD_DECODER_NAME << " decoder" << std::endl;
    m_standardDecoder = std::make_unique<StandardDecoder>();

    if (!m_standardDecoder->open(ctx, m_filePath, m_loop)) {
        std::cerr << "[VideoPlayer] Failed to open: " << m_filePath << std::endl;
        m_standardDecoder.reset();
        return;
    }

    m_output = m_standardDecoder->texture();
    m_outputView = m_standardDecoder->textureView();
    m_width = m_standardDecoder->width();
    m_height = m_standardDecoder->height();
    m_needsReload = false;

    if (m_autoPlay) {
        m_standardDecoder->play();
    }

    std::cout << "[VideoPlayer] Loaded: " << m_filePath
              << " (" << m_width << "x" << m_height
              << ", " << m_standardDecoder->duration() << "s)" << std::endl;
}

void VideoPlayer::process(Context& ctx) {
    // Check if we need to reload
    if (m_needsReload) {
        loadVideo(ctx);
    }

    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->update(ctx);
        m_outputView = m_hapDecoder->textureView();
    } else if (m_standardDecoder) {
        m_standardDecoder->update(ctx);
        m_outputView = m_standardDecoder->textureView();
    }
}

void VideoPlayer::cleanup() {
    if (m_hapDecoder) {
        m_hapDecoder->close();
        m_hapDecoder.reset();
    }
    if (m_standardDecoder) {
        m_standardDecoder->close();
        m_standardDecoder.reset();
    }
    m_isHAP = false;
    m_output = nullptr;
    m_outputView = nullptr;
}

VideoPlayer& VideoPlayer::volume(float v) {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->setVolume(v);
    } else if (m_standardDecoder) {
        m_standardDecoder->setVolume(v);
    }
    return *this;
}

void VideoPlayer::play() {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->play();
    } else if (m_standardDecoder) {
        m_standardDecoder->play();
    }
}

void VideoPlayer::pause() {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->pause();
    } else if (m_standardDecoder) {
        m_standardDecoder->pause();
    }
}

void VideoPlayer::seek(float seconds) {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->seek(seconds);
    } else if (m_standardDecoder) {
        m_standardDecoder->seek(seconds);
    }
}

bool VideoPlayer::isPlaying() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isPlaying();
    if (m_standardDecoder) return m_standardDecoder->isPlaying();
    return false;
}

bool VideoPlayer::isFinished() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isFinished();
    if (m_standardDecoder) return m_standardDecoder->isFinished();
    return true;
}

bool VideoPlayer::isOpen() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isOpen();
    if (m_standardDecoder) return m_standardDecoder->isOpen();
    return false;
}

float VideoPlayer::currentTime() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->currentTime();
    if (m_standardDecoder) return m_standardDecoder->currentTime();
    return 0.0f;
}

float VideoPlayer::duration() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->duration();
    if (m_standardDecoder) return m_standardDecoder->duration();
    return 0.0f;
}

float VideoPlayer::frameRate() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->frameRate();
    if (m_standardDecoder) return m_standardDecoder->frameRate();
    return 0.0f;
}

int VideoPlayer::videoWidth() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->width();
    if (m_standardDecoder) return m_standardDecoder->width();
    return 0;
}

int VideoPlayer::videoHeight() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->height();
    if (m_standardDecoder) return m_standardDecoder->height();
    return 0;
}

bool VideoPlayer::hasAudio() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->hasAudio();
    if (m_standardDecoder) return m_standardDecoder->hasAudio();
    return false;
}

} // namespace vivid::video
