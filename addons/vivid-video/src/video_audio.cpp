/**
 * @file video_audio.cpp
 * @brief VideoAudio operator implementation
 */

#include <vivid/video/video_audio.h>
#include <vivid/video/video_player.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <iostream>

namespace vivid::video {

VideoAudio::VideoAudio() = default;

VideoAudio& VideoAudio::source(const std::string& videoOpName) {
    m_sourceName = videoOpName;
    return *this;
}

void VideoAudio::init(Context& ctx) {
    // Resolve source VideoPlayer by name
    if (m_sourceName.empty()) {
        std::cerr << "[VideoAudio] No source specified\n";
        return;
    }

    Operator* op = ctx.chain().getByName(m_sourceName);
    if (!op) {
        std::cerr << "[VideoAudio] Source '" << m_sourceName << "' not found\n";
        return;
    }

    // Check if it's a VideoPlayer
    m_videoPlayer = dynamic_cast<VideoPlayer*>(op);
    if (!m_videoPlayer) {
        std::cerr << "[VideoAudio] Source '" << m_sourceName << "' is not a VideoPlayer\n";
        return;
    }

    m_connectedToSource = true;

    // Allocate output buffer with default settings
    // (will be adjusted when we know the actual audio format)
    allocateOutput(AUDIO_BLOCK_SIZE, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE);

    std::cout << "[VideoAudio] Connected to '" << m_sourceName << "'" << std::endl;
}

void VideoAudio::process(Context& ctx) {
    if (!m_connectedToSource || !m_videoPlayer) {
        clearOutput();
        return;
    }

    // Check if video has audio
    if (!m_videoPlayer->hasAudio()) {
        clearOutput();
        return;
    }

    // On first frame with audio, disable internal playback to avoid double audio
    if (m_videoPlayer->isInternalAudioEnabled()) {
        m_videoPlayer->setInternalAudioEnabled(false);
        std::cout << "[VideoAudio] Disabled internal audio on '" << m_sourceName << "'" << std::endl;
    }

    // Calculate how many frames to read based on frame duration
    uint32_t framesToRead;
    double frameDuration;

    if (ctx.isRecording()) {
        // When recording, use fixed frame duration from recording fps
        frameDuration = 1.0 / ctx.recordingFps();
        framesToRead = static_cast<uint32_t>(frameDuration * AUDIO_SAMPLE_RATE);
        static bool loggedOnce = false;
        if (!loggedOnce) {
            std::cout << "[VideoAudio] Recording mode: " << framesToRead << " frames/video-frame at "
                      << ctx.recordingFps() << " fps (PTS-based sync)" << std::endl;
            loggedOnce = true;
        }
    } else {
        // Normal playback: use wall-clock delta time
        // Audio must be read continuously regardless of video frame rate
        frameDuration = std::min(ctx.dt(), 0.033);
        framesToRead = static_cast<uint32_t>(frameDuration * AUDIO_SAMPLE_RATE);
        // Clamp to reasonable bounds (min 256, max 2048 frames per graphics frame)
        framesToRead = std::max(framesToRead, 256u);
        framesToRead = std::min(framesToRead, 2048u);
    }

    // Ensure output buffer is large enough
    if (framesToRead > m_output.frameCount) {
        allocateOutput(framesToRead, AUDIO_CHANNELS, AUDIO_SAMPLE_RATE);
    }

    // Read audio samples - use simple sequential read, not PTS-based
    // The audio buffer in HAPDecoder is already synced to video via seek()
    // We just need to continuously consume audio at the playback rate
    uint32_t framesRead = m_videoPlayer->readAudioSamples(
        m_output.samples, framesToRead);

    // Update actual frame count for downstream operators
    m_output.frameCount = framesRead;
}

void VideoAudio::cleanup() {
    // Re-enable internal audio in VideoPlayer if we disabled it
    if (m_connectedToSource && m_videoPlayer) {
        m_videoPlayer->setInternalAudioEnabled(true);
    }

    m_videoPlayer = nullptr;
    m_connectedToSource = false;
    releaseOutput();
}

} // namespace vivid::video
