// HAP Decoder - Cross-platform stub for Windows/Linux
// HAP codec requires FFmpeg for container demuxing on non-macOS platforms.
// On macOS, we use AVFoundation (see hap_decoder.mm).
//
// For Windows/Linux HAP support, FFmpeg would be needed to:
// 1. Demux the MOV/MP4 container
// 2. Extract compressed HAP frames
// 3. Decode via hap.c library
// 4. Upload DXT textures to GPU
//
// Standard codecs (H.264, HEVC) work via platform-native decoders.

#if !defined(__APPLE__)

#include <vivid/video/hap_decoder.h>
#include <iostream>
#include <fstream>
#include <algorithm>

namespace vivid::video {

struct HAPDecoder::Impl {
    // Stub - no implementation on Windows/Linux without FFmpeg
};

HAPDecoder::HAPDecoder() : impl_(std::make_unique<Impl>()) {}

HAPDecoder::~HAPDecoder() {
    close();
}

bool HAPDecoder::isHAPFile(const std::string& path) {
    // Quick check: look for HAP signature in file header
    // This is a simplified check - full detection requires reading MOV atoms

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    // Read first 4KB to look for HAP codec identifiers
    char buffer[4096];
    file.read(buffer, sizeof(buffer));
    auto bytesRead = file.gcount();

    // Look for HAP FourCC codes in the file
    // 'Hap1' = 0x48617031, 'Hap5' = 0x48617035, etc.
    std::string data(buffer, bytesRead);

    // Check for HAP codec identifiers (in MOV/MP4 stsd atom)
    if (data.find("Hap1") != std::string::npos ||
        data.find("Hap5") != std::string::npos ||
        data.find("HapY") != std::string::npos ||
        data.find("HapM") != std::string::npos ||
        data.find("HapA") != std::string::npos) {

        std::cerr << "[HAPDecoder] HAP file detected but HAP playback requires FFmpeg on Windows/Linux\n";
        std::cerr << "[HAPDecoder] Consider using H.264 or HEVC encoded videos instead\n";
        return false;  // Return false so we fall back to standard decoder
    }

    return false;
}

bool HAPDecoder::open(Context& ctx, const std::string& path, bool loop) {
    std::cerr << "[HAPDecoder] HAP codec not supported on this platform without FFmpeg\n";
    std::cerr << "[HAPDecoder] Use H.264, HEVC, or other standard codecs\n";
    return false;
}

void HAPDecoder::close() {
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
    frameRate_ = 0.0f;
    isPlaying_ = false;
    isFinished_ = true;
    currentTime_ = 0.0f;
}

bool HAPDecoder::isOpen() const {
    return false;
}

void HAPDecoder::update(Context& ctx) {
    // Stub
}

void HAPDecoder::seek(float seconds) {
    // Stub
}

void HAPDecoder::pause() {
    isPlaying_ = false;
}

void HAPDecoder::play() {
    // Can't play - not supported
}

void HAPDecoder::setVolume(float volume) {
    // Stub
}

float HAPDecoder::getVolume() const {
    return 1.0f;
}

void HAPDecoder::createTexture() {
    // Stub
}

void HAPDecoder::resetReader() {
    // Stub
}

} // namespace vivid::video

#endif // !__APPLE__
