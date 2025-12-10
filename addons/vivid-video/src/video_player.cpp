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
#include <vivid/video/avf_playback_decoder.h>
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
#if defined(__APPLE__)
    if (m_playbackDecoder) {
        m_playbackDecoder->close();
        m_playbackDecoder.reset();
    }
#endif
    m_isHAP = false;
    m_usePlaybackDecoder = false;

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

#if defined(__APPLE__)
    // Use AVPlayer-based decoder for real-time playback with proper A/V sync
    // This handles looping correctly via AVPlayerLooper
    std::cout << "[VideoPlayer] Using AVPlayer decoder (OS-level A/V sync)" << std::endl;
    m_playbackDecoder = std::make_unique<AVFPlaybackDecoder>();

    if (m_playbackDecoder->open(ctx, m_filePath, m_loop)) {
        m_usePlaybackDecoder = true;
        m_output = m_playbackDecoder->texture();
        m_outputView = m_playbackDecoder->textureView();
        m_width = m_playbackDecoder->width();
        m_height = m_playbackDecoder->height();
        m_needsReload = false;

        // Note: AVPlayer auto-plays, and handles audio internally
        std::cout << "[VideoPlayer] Loaded: " << m_filePath
                  << " (" << m_width << "x" << m_height
                  << ", " << m_playbackDecoder->duration() << "s)" << std::endl;
        return;
    }

    std::cerr << "[VideoPlayer] AVPlayer decoder failed, falling back to AVAssetReader" << std::endl;
    m_playbackDecoder.reset();
#endif

    // Fallback: Use AVAssetReader-based decoder (for offline processing or if AVPlayer fails)
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
    }
#if defined(__APPLE__)
    else if (m_usePlaybackDecoder && m_playbackDecoder) {
        m_playbackDecoder->update(ctx);
        m_outputView = m_playbackDecoder->textureView();
    }
#endif
    else if (m_standardDecoder) {
        m_standardDecoder->update(ctx);
        m_outputView = m_standardDecoder->textureView();
    }
}

void VideoPlayer::cleanup() {
    if (m_hapDecoder) {
        m_hapDecoder->close();
        m_hapDecoder.reset();
    }
#if defined(__APPLE__)
    if (m_playbackDecoder) {
        m_playbackDecoder->close();
        m_playbackDecoder.reset();
    }
#endif
    if (m_standardDecoder) {
        m_standardDecoder->close();
        m_standardDecoder.reset();
    }
    m_isHAP = false;
    m_usePlaybackDecoder = false;
    m_output = nullptr;
    m_outputView = nullptr;
}

VideoPlayer& VideoPlayer::volume(float v) {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->setVolume(v);
    }
#if defined(__APPLE__)
    else if (m_usePlaybackDecoder && m_playbackDecoder) {
        m_playbackDecoder->setVolume(v);
    }
#endif
    else if (m_standardDecoder) {
        m_standardDecoder->setVolume(v);
    }
    return *this;
}

void VideoPlayer::play() {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->play();
    }
#if defined(__APPLE__)
    else if (m_usePlaybackDecoder && m_playbackDecoder) {
        m_playbackDecoder->play();
    }
#endif
    else if (m_standardDecoder) {
        m_standardDecoder->play();
    }
}

void VideoPlayer::pause() {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->pause();
    }
#if defined(__APPLE__)
    else if (m_usePlaybackDecoder && m_playbackDecoder) {
        m_playbackDecoder->pause();
    }
#endif
    else if (m_standardDecoder) {
        m_standardDecoder->pause();
    }
}

void VideoPlayer::seek(float seconds) {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->seek(seconds);
    }
#if defined(__APPLE__)
    else if (m_usePlaybackDecoder && m_playbackDecoder) {
        m_playbackDecoder->seek(seconds);
    }
#endif
    else if (m_standardDecoder) {
        m_standardDecoder->seek(seconds);
    }
}

bool VideoPlayer::isPlaying() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isPlaying();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->isPlaying();
#endif
    if (m_standardDecoder) return m_standardDecoder->isPlaying();
    return false;
}

bool VideoPlayer::isFinished() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isFinished();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->isFinished();
#endif
    if (m_standardDecoder) return m_standardDecoder->isFinished();
    return true;
}

bool VideoPlayer::isOpen() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isOpen();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->isOpen();
#endif
    if (m_standardDecoder) return m_standardDecoder->isOpen();
    return false;
}

float VideoPlayer::currentTime() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->currentTime();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->currentTime();
#endif
    if (m_standardDecoder) return m_standardDecoder->currentTime();
    return 0.0f;
}

float VideoPlayer::duration() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->duration();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->duration();
#endif
    if (m_standardDecoder) return m_standardDecoder->duration();
    return 0.0f;
}

float VideoPlayer::frameRate() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->frameRate();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->frameRate();
#endif
    if (m_standardDecoder) return m_standardDecoder->frameRate();
    return 0.0f;
}

int VideoPlayer::videoWidth() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->width();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->width();
#endif
    if (m_standardDecoder) return m_standardDecoder->width();
    return 0;
}

int VideoPlayer::videoHeight() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->height();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->height();
#endif
    if (m_standardDecoder) return m_standardDecoder->height();
    return 0;
}

bool VideoPlayer::hasAudio() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->hasAudio();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->hasAudio();
#endif
    if (m_standardDecoder) return m_standardDecoder->hasAudio();
    return false;
}

uint32_t VideoPlayer::readAudioSamples(float* buffer, uint32_t maxFrames) {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->readAudioSamples(buffer, maxFrames);
    if (m_standardDecoder) return m_standardDecoder->readAudioSamples(buffer, maxFrames);
    return 0;
}

uint32_t VideoPlayer::readAudioSamplesForPTS(float* buffer, double videoPTS, uint32_t maxFrames) {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->readAudioSamplesForPTS(buffer, videoPTS, maxFrames);
    // Standard decoder falls back to non-PTS read for now
    if (m_standardDecoder) return m_standardDecoder->readAudioSamples(buffer, maxFrames);
    return 0;
}

double VideoPlayer::audioAvailableStartPTS() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->audioAvailableStartPTS();
    return 0.0;
}

double VideoPlayer::audioAvailableEndPTS() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->audioAvailableEndPTS();
    return 0.0;
}

void VideoPlayer::setInternalAudioEnabled(bool enable) {
    if (m_isHAP && m_hapDecoder) m_hapDecoder->setInternalAudioEnabled(enable);
    if (m_standardDecoder) m_standardDecoder->setInternalAudioEnabled(enable);
}

bool VideoPlayer::isInternalAudioEnabled() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isInternalAudioEnabled();
    if (m_standardDecoder) return m_standardDecoder->isInternalAudioEnabled();
    return true;
}

uint32_t VideoPlayer::audioSampleRate() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->audioSampleRate();
    if (m_standardDecoder) return m_standardDecoder->audioSampleRate();
    return 48000;
}

uint32_t VideoPlayer::audioChannels() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->audioChannels();
    if (m_standardDecoder) return m_standardDecoder->audioChannels();
    return 2;
}

} // namespace vivid::video
