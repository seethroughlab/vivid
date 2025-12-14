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
#include <cmath>

// Platform-specific decoder includes
#if defined(__APPLE__)
#include <vivid/video/avf_decoder.h>
#include <vivid/video/avf_playback_decoder.h>
#define STANDARD_DECODER_NAME "AVFoundation"
using StandardDecoder = vivid::video::AVFDecoder;
#elif defined(_WIN32)
#include <vivid/video/mf_decoder.h>
#include <vivid/video/dshow_decoder.h>
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
#elif defined(_WIN32)
    if (m_dshowDecoder) {
        m_dshowDecoder->close();
        m_dshowDecoder.reset();
    }
#endif
    m_isHAP = false;
    m_usePlaybackDecoder = false;
    m_useDShowDecoder = false;

    // Ensure we always have a valid fallback texture before proceeding
    // This prevents crashes if the new video fails to load
    createFallbackTexture(ctx);
    // Point to fallback - will be overwritten if video loads successfully
    m_output = m_fallbackTexture;
    m_outputView = m_fallbackTextureView;
    m_width = 64;
    m_height = 64;

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
            m_resolutionLocked = true;  // Lock to video's native resolution
            m_needsReload = false;

            // Apply stored volume
            m_hapDecoder->setVolume(m_volume);

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
        m_resolutionLocked = true;  // Lock to video's native resolution
        m_needsReload = false;

        // Apply stored volume
        m_playbackDecoder->setVolume(m_volume);

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
        std::cerr << "[VideoPlayer] " << STANDARD_DECODER_NAME << " failed to open: " << m_filePath << std::endl;
        m_standardDecoder.reset();

#if defined(_WIN32)
        // Try DirectShow as a fallback for codecs MF doesn't support (e.g., ProRes)
        if (DShowDecoder::canDecode(m_filePath)) {
            std::cout << "[VideoPlayer] Trying DirectShow decoder (requires LAV Filters or similar)" << std::endl;
            m_dshowDecoder = std::make_unique<DShowDecoder>();

            if (m_dshowDecoder->open(ctx, m_filePath, m_loop)) {
                m_useDShowDecoder = true;
                m_output = m_dshowDecoder->texture();
                m_outputView = m_dshowDecoder->textureView();
                m_width = m_dshowDecoder->width();
                m_height = m_dshowDecoder->height();
                m_resolutionLocked = true;  // Lock to video's native resolution
                m_needsReload = false;

                // Apply stored volume
                m_dshowDecoder->setVolume(m_volume);

                if (m_autoPlay) {
                    m_dshowDecoder->play();
                }

                std::cout << "[VideoPlayer] Loaded via DirectShow: " << m_filePath
                          << " (" << m_width << "x" << m_height
                          << ", " << m_dshowDecoder->duration() << "s)" << std::endl;
                return;
            }

            std::cerr << "[VideoPlayer] DirectShow also failed - codec may not be installed" << std::endl;
            m_dshowDecoder.reset();
        }
#endif

        // All decoders failed - fallback texture already set
        m_needsReload = false;  // Don't keep retrying
        return;
    }

    m_output = m_standardDecoder->texture();
    m_outputView = m_standardDecoder->textureView();
    m_width = m_standardDecoder->width();
    m_height = m_standardDecoder->height();
    m_resolutionLocked = true;  // Lock to video's native resolution
    m_needsReload = false;

    // Apply stored volume
    m_standardDecoder->setVolume(m_volume);

    if (m_autoPlay) {
        m_standardDecoder->play();
    }

    std::cout << "[VideoPlayer] Loaded: " << m_filePath
              << " (" << m_width << "x" << m_height
              << ", " << m_standardDecoder->duration() << "s)" << std::endl;
}

void VideoPlayer::process(Context& ctx) {
    // Video uses loaded file dimensions - no auto-resize

    // VideoPlayer is streaming - always cooks

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
#elif defined(_WIN32)
    else if (m_useDShowDecoder && m_dshowDecoder) {
        m_dshowDecoder->update(ctx);
        m_outputView = m_dshowDecoder->textureView();
    }
#endif
    else if (m_standardDecoder) {
        m_standardDecoder->update(ctx);
        m_outputView = m_standardDecoder->textureView();
    }

    didCook();
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
#elif defined(_WIN32)
    if (m_dshowDecoder) {
        m_dshowDecoder->close();
        m_dshowDecoder.reset();
    }
#endif
    if (m_standardDecoder) {
        m_standardDecoder->close();
        m_standardDecoder.reset();
    }
    if (m_fallbackTextureView) {
        wgpuTextureViewRelease(m_fallbackTextureView);
        m_fallbackTextureView = nullptr;
    }
    if (m_fallbackTexture) {
        wgpuTextureRelease(m_fallbackTexture);
        m_fallbackTexture = nullptr;
    }
    m_isHAP = false;
    m_usePlaybackDecoder = false;
    m_useDShowDecoder = false;
    m_output = nullptr;
    m_outputView = nullptr;
}

void VideoPlayer::createFallbackTexture(Context& ctx) {
    // Release existing fallback texture if it exists (so we can recreate with new content)
    if (m_fallbackTextureView) {
        wgpuTextureViewRelease(m_fallbackTextureView);
        m_fallbackTextureView = nullptr;
    }
    if (m_fallbackTexture) {
        wgpuTextureRelease(m_fallbackTexture);
        m_fallbackTexture = nullptr;
    }

    // Create a 320x180 texture (16:9 aspect ratio) for the test pattern
    const uint32_t texWidth = 320;
    const uint32_t texHeight = 180;

    WGPUTextureDescriptor texDesc = {};
    texDesc.label = { "VideoPlayer Fallback", WGPU_STRLEN };
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = { texWidth, texHeight, 1 };
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;

    m_fallbackTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = { "VideoPlayer Fallback View", WGPU_STRLEN };
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;

    m_fallbackTextureView = wgpuTextureCreateView(m_fallbackTexture, &viewDesc);

    // Create pixel data - classic TV test pattern style
    std::vector<uint8_t> pixels(texWidth * texHeight * 4);

    // Helper to set pixel color
    auto setPixel = [&](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
        if (x >= texWidth || y >= texHeight) return;
        size_t idx = (y * texWidth + x) * 4;
        pixels[idx + 0] = r;
        pixels[idx + 1] = g;
        pixels[idx + 2] = b;
        pixels[idx + 3] = 255;
    };

    // SMPTE-style color bars (top 60% of image)
    // Colors: White, Yellow, Cyan, Green, Magenta, Red, Blue
    const uint8_t bars[7][3] = {
        {192, 192, 192},  // White (75%)
        {192, 192, 0},    // Yellow
        {0, 192, 192},    // Cyan
        {0, 192, 0},      // Green
        {192, 0, 192},    // Magenta
        {192, 0, 0},      // Red
        {0, 0, 192},      // Blue
    };

    uint32_t barHeight = texHeight * 60 / 100;  // Top 60%
    uint32_t barWidth = texWidth / 7;

    for (uint32_t y = 0; y < barHeight; y++) {
        for (uint32_t x = 0; x < texWidth; x++) {
            int barIdx = (x * 7) / texWidth;
            if (barIdx > 6) barIdx = 6;
            setPixel(x, y, bars[barIdx][0], bars[barIdx][1], bars[barIdx][2]);
        }
    }

    // Reverse/complement bars (next 10%)
    const uint8_t revBars[7][3] = {
        {0, 0, 192},      // Blue
        {19, 19, 19},     // Black
        {192, 0, 192},    // Magenta
        {19, 19, 19},     // Black
        {0, 192, 192},    // Cyan
        {19, 19, 19},     // Black
        {192, 192, 192},  // White
    };

    uint32_t revStart = barHeight;
    uint32_t revHeight = texHeight * 10 / 100;

    for (uint32_t y = revStart; y < revStart + revHeight; y++) {
        for (uint32_t x = 0; x < texWidth; x++) {
            int barIdx = (x * 7) / texWidth;
            if (barIdx > 6) barIdx = 6;
            setPixel(x, y, revBars[barIdx][0], revBars[barIdx][1], revBars[barIdx][2]);
        }
    }

    // Grayscale ramp and PLUGE (bottom 30%)
    uint32_t bottomStart = revStart + revHeight;
    uint32_t bottomHeight = texHeight - bottomStart;

    for (uint32_t y = bottomStart; y < texHeight; y++) {
        for (uint32_t x = 0; x < texWidth; x++) {
            // Left third: -I, White, +Q pattern
            if (x < texWidth / 3) {
                int section = (x * 3) / (texWidth / 3);
                if (section == 0) {
                    setPixel(x, y, 0, 33, 76);  // -I (dark blue-ish)
                } else if (section == 1) {
                    setPixel(x, y, 192, 192, 192);  // White
                } else {
                    setPixel(x, y, 50, 0, 106);  // +Q (purple-ish)
                }
            }
            // Middle third: grayscale gradient
            else if (x < texWidth * 2 / 3) {
                uint32_t localX = x - texWidth / 3;
                uint32_t gradWidth = texWidth / 3;
                uint8_t gray = (localX * 255) / gradWidth;
                setPixel(x, y, gray, gray, gray);
            }
            // Right third: black, PLUGE, black
            else {
                uint32_t localX = x - texWidth * 2 / 3;
                uint32_t sectionWidth = texWidth / 9;
                if (localX < sectionWidth) {
                    setPixel(x, y, 3, 3, 3);  // Super black
                } else if (localX < sectionWidth * 2) {
                    setPixel(x, y, 19, 19, 19);  // Black
                } else {
                    setPixel(x, y, 38, 38, 38);  // Dark gray
                }
            }
        }
    }

    // Draw center circle (convergence pattern)
    int centerX = texWidth / 2;
    int centerY = texHeight / 2 - 10;
    int radius = 30;

    for (int dy = -radius - 2; dy <= radius + 2; dy++) {
        for (int dx = -radius - 2; dx <= radius + 2; dx++) {
            int dist = (int)std::sqrt(dx * dx + dy * dy);
            int px = centerX + dx;
            int py = centerY + dy;
            if (px >= 0 && px < (int)texWidth && py >= 0 && py < (int)texHeight) {
                // Circle outline
                if (dist >= radius - 1 && dist <= radius + 1) {
                    setPixel(px, py, 255, 255, 255);
                }
                // Crosshair
                if ((dx == 0 && std::abs(dy) <= radius) || (dy == 0 && std::abs(dx) <= radius)) {
                    setPixel(px, py, 255, 255, 255);
                }
            }
        }
    }

    // Draw "NO SIGNAL" text using 5x7 pixel font
    const uint8_t font5x7[][7] = {
        // N
        {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001},
        // O
        {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
        // (space)
        {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000},
        // S
        {0b01110, 0b10001, 0b10000, 0b01110, 0b00001, 0b10001, 0b01110},
        // I
        {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
        // G
        {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01110},
        // N
        {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001},
        // A
        {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},
        // L
        {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111},
    };

    // Position text at bottom center
    const int charCount = 9;  // "NO SIGNAL"
    uint32_t textX = texWidth / 2 - (charCount * 6) / 2;
    uint32_t textY = texHeight - 18;

    // Draw text background box
    for (uint32_t y = textY - 3; y < textY + 10; y++) {
        for (uint32_t x = textX - 4; x < textX + charCount * 6 + 2; x++) {
            if (x < texWidth && y < texHeight) {
                setPixel(x, y, 0, 0, 0);
            }
        }
    }

    // Draw the text
    for (int c = 0; c < charCount; c++) {
        for (int row = 0; row < 7; row++) {
            uint8_t rowBits = font5x7[c][row];
            for (int col = 0; col < 5; col++) {
                if (rowBits & (1 << (4 - col))) {
                    setPixel(textX + c * 6 + col, textY + row, 255, 80, 80);
                }
            }
        }
    }

    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = m_fallbackTexture;
    destination.mipLevel = 0;
    destination.origin = { 0, 0, 0 };
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = texWidth * 4;
    dataLayout.rowsPerImage = texHeight;

    WGPUExtent3D writeSize = { texWidth, texHeight, 1 };

    wgpuQueueWriteTexture(ctx.queue(), &destination, pixels.data(),
                          pixels.size(), &dataLayout, &writeSize);

    m_output = m_fallbackTexture;
    m_outputView = m_fallbackTextureView;
    m_width = texWidth;
    m_height = texHeight;
}

void VideoPlayer::setVolume(float v) {
    m_volume = v;  // Store for later application if decoder not yet ready
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->setVolume(v);
    }
#if defined(__APPLE__)
    else if (m_usePlaybackDecoder && m_playbackDecoder) {
        m_playbackDecoder->setVolume(v);
    }
#elif defined(_WIN32)
    else if (m_useDShowDecoder && m_dshowDecoder) {
        m_dshowDecoder->setVolume(v);
    }
#endif
    else if (m_standardDecoder) {
        m_standardDecoder->setVolume(v);
    }
}

void VideoPlayer::play() {
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->play();
    }
#if defined(__APPLE__)
    else if (m_usePlaybackDecoder && m_playbackDecoder) {
        m_playbackDecoder->play();
    }
#elif defined(_WIN32)
    else if (m_useDShowDecoder && m_dshowDecoder) {
        m_dshowDecoder->play();
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
#elif defined(_WIN32)
    else if (m_useDShowDecoder && m_dshowDecoder) {
        m_dshowDecoder->pause();
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
#elif defined(_WIN32)
    else if (m_useDShowDecoder && m_dshowDecoder) {
        m_dshowDecoder->seek(seconds);
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
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->isPlaying();
#endif
    if (m_standardDecoder) return m_standardDecoder->isPlaying();
    return false;
}

bool VideoPlayer::isFinished() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isFinished();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->isFinished();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->isFinished();
#endif
    if (m_standardDecoder) return m_standardDecoder->isFinished();
    return true;
}

bool VideoPlayer::isOpen() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isOpen();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->isOpen();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->isOpen();
#endif
    if (m_standardDecoder) return m_standardDecoder->isOpen();
    return false;
}

float VideoPlayer::currentTime() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->currentTime();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->currentTime();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->currentTime();
#endif
    if (m_standardDecoder) return m_standardDecoder->currentTime();
    return 0.0f;
}

float VideoPlayer::duration() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->duration();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->duration();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->duration();
#endif
    if (m_standardDecoder) return m_standardDecoder->duration();
    return 0.0f;
}

float VideoPlayer::frameRate() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->frameRate();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->frameRate();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->frameRate();
#endif
    if (m_standardDecoder) return m_standardDecoder->frameRate();
    return 0.0f;
}

int VideoPlayer::videoWidth() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->width();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->width();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->width();
#endif
    if (m_standardDecoder) return m_standardDecoder->width();
    return 0;
}

int VideoPlayer::videoHeight() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->height();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->height();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->height();
#endif
    if (m_standardDecoder) return m_standardDecoder->height();
    return 0;
}

bool VideoPlayer::hasAudio() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->hasAudio();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->hasAudio();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->hasAudio();
#endif
    if (m_standardDecoder) return m_standardDecoder->hasAudio();
    return false;
}

uint32_t VideoPlayer::readAudioSamples(float* buffer, uint32_t maxFrames) {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->readAudioSamples(buffer, maxFrames);
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->readAudioSamples(buffer, maxFrames);
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->readAudioSamples(buffer, maxFrames);
#endif
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
    m_internalAudioEnabled = enable;
    if (m_isHAP && m_hapDecoder) {
        m_hapDecoder->setInternalAudioEnabled(enable);
    }
#if defined(__APPLE__)
    else if (m_usePlaybackDecoder && m_playbackDecoder) {
        m_playbackDecoder->setInternalAudioEnabled(enable);
    }
#elif defined(_WIN32)
    else if (m_useDShowDecoder && m_dshowDecoder) {
        m_dshowDecoder->setInternalAudioEnabled(enable);
    }
#endif
    else if (m_standardDecoder) {
        m_standardDecoder->setInternalAudioEnabled(enable);
    }
}

bool VideoPlayer::isInternalAudioEnabled() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->isInternalAudioEnabled();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->isInternalAudioEnabled();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->isInternalAudioEnabled();
#endif
    if (m_standardDecoder) return m_standardDecoder->isInternalAudioEnabled();
    return true;
}

uint32_t VideoPlayer::audioSampleRate() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->audioSampleRate();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->audioSampleRate();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->audioSampleRate();
#endif
    if (m_standardDecoder) return m_standardDecoder->audioSampleRate();
    return 48000;
}

uint32_t VideoPlayer::audioChannels() const {
    if (m_isHAP && m_hapDecoder) return m_hapDecoder->audioChannels();
#if defined(__APPLE__)
    if (m_usePlaybackDecoder && m_playbackDecoder) return m_playbackDecoder->audioChannels();
#elif defined(_WIN32)
    if (m_useDShowDecoder && m_dshowDecoder) return m_dshowDecoder->audioChannels();
#endif
    if (m_standardDecoder) return m_standardDecoder->audioChannels();
    return 2;
}

} // namespace vivid::video
