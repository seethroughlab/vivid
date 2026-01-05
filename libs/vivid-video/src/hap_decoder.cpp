// HAP Decoder - Windows/Linux implementation
// Uses custom MOV parser to demux QuickTime containers and Vidvox HAP library to decode frames.
// DXT textures are uploaded directly to GPU without CPU pixel conversion.

#if !defined(__APPLE__)

#include <vivid/video/hap_decoder.h>
#include <vivid/video/audio_player.h>
#include <vivid/context.h>
#include "hap.h"
#include "mov_parser.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <chrono>

namespace vivid::video {

// HAP decode callback for single-threaded decoding
static void hapDecodeCallback(HapDecodeWorkFunction function, void* p,
                              unsigned int count, void* /*info*/) {
    for (unsigned int i = 0; i < count; i++) {
        function(p, i);
    }
}

struct HAPDecoder::Impl {
    FILE* file = nullptr;
    MOVFile mov;
    const MOVTrack* videoTrack = nullptr;
    const MOVTrack* audioTrack = nullptr;

    unsigned int currentSample = 0;
    unsigned int totalSamples = 0;

    // HAP texture format (DXT1/DXT5/etc)
    unsigned int hapTextureFormat = 0;

    // Frame buffer for reading compressed data
    std::vector<uint8_t> frameBuffer;

    // Audio state
    unsigned int currentAudioSample = 0;
    unsigned int totalAudioSamples = 0;
    std::vector<uint8_t> audioBuffer;
    bool audioIsBigEndian = false;  // 'twos' = big-endian, 'sowt' = little-endian

    // Timing
    std::chrono::steady_clock::time_point lastUpdateTime;
    bool firstUpdate = true;

    void cleanup() {
        if (file) {
            fclose(file);
            file = nullptr;
        }
        mov = MOVFile();
        videoTrack = nullptr;
        audioTrack = nullptr;
        currentSample = 0;
        totalSamples = 0;
        hapTextureFormat = 0;
        frameBuffer.clear();
        currentAudioSample = 0;
        totalAudioSamples = 0;
        audioBuffer.clear();
        audioIsBigEndian = false;
        firstUpdate = true;
    }
};

HAPDecoder::HAPDecoder() : impl_(std::make_unique<Impl>()) {}

HAPDecoder::~HAPDecoder() {
    close();
}

bool HAPDecoder::isHAPFile(const std::string& path) {
    // Quick check: look for HAP signature in file
    // MOV/MP4 files can have moov atom at the end, so we need to scan more of the file
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Get file size
    file.seekg(0, std::ios::end);
    auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read up to 256KB or whole file to find HAP codec identifiers
    // The stsd atom with codec FourCC could be anywhere in the moov atom
    const size_t maxRead = 256 * 1024;
    size_t toRead = std::min(static_cast<size_t>(fileSize), maxRead);

    std::vector<char> buffer(toRead);
    file.read(buffer.data(), toRead);
    auto bytesRead = file.gcount();

    // Look for HAP FourCC codes in the file
    std::string data(buffer.data(), bytesRead);

    // Check for HAP codec identifiers (in MOV/MP4 stsd atom)
    // HAP uses: 'Hap1' (HAP), 'Hap5' (HAP Alpha), 'HapY' (HAP Q), 'HapM' (HAP Q Alpha), 'HapA' (HAP Alpha-Only)
    auto checkForHAP = [](const std::string& data) {
        return data.find("Hap1") != std::string::npos ||
               data.find("Hap5") != std::string::npos ||
               data.find("HapY") != std::string::npos ||
               data.find("HapM") != std::string::npos ||
               data.find("HapA") != std::string::npos;
    };

    if (checkForHAP(data)) {
        std::cout << "[HAPDecoder] Detected HAP video: " << path << std::endl;
        return true;
    }

    // If not found in first 256KB, check the last 256KB (moov at end)
    if (fileSize > static_cast<std::streamoff>(maxRead * 2)) {
        file.seekg(-static_cast<std::streamoff>(maxRead), std::ios::end);
        file.read(buffer.data(), maxRead);
        bytesRead = file.gcount();

        data.assign(buffer.data(), bytesRead);

        if (checkForHAP(data)) {
            std::cout << "[HAPDecoder] Detected HAP video: " << path << std::endl;
            return true;
        }
    }

    return false;
}

void HAPDecoder::createTexture() {
    if (texture_) {
        wgpuTextureDestroy(texture_);
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }
    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }

    WGPUTextureDescriptor desc = {};
    desc.label = { "HAPVideoFrame", WGPU_STRLEN };
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};
    desc.format = textureFormat_;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    texture_ = wgpuDeviceCreateTexture(device_, &desc);
    if (!texture_) {
        std::cerr << "[HAPDecoder] Failed to create texture" << std::endl;
        return;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = { "HAPVideoFrameView", WGPU_STRLEN };
    viewDesc.format = textureFormat_;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
}

bool HAPDecoder::open(Context& ctx, const std::string& path, bool loop) {
    close();

    device_ = ctx.device();
    queue_ = ctx.queue();
    filePath_ = path;
    isLooping_ = loop;

    // Open file
    impl_->file = fopen(path.c_str(), "rb");
    if (!impl_->file) {
        std::cerr << "[HAPDecoder] Failed to open file: " << path << std::endl;
        return false;
    }

    // Get file size
#ifdef _WIN32
    _fseeki64(impl_->file, 0, SEEK_END);
    int64_t fileSize = _ftelli64(impl_->file);
    _fseeki64(impl_->file, 0, SEEK_SET);
#else
    fseeko(impl_->file, 0, SEEK_END);
    int64_t fileSize = ftello(impl_->file);
    fseeko(impl_->file, 0, SEEK_SET);
#endif

    // Parse MOV container
    if (!parseMOV(impl_->file, fileSize, impl_->mov)) {
        std::cerr << "[HAPDecoder] Failed to parse MOV container" << std::endl;
        close();
        return false;
    }

    // Get video track
    impl_->videoTrack = impl_->mov.videoTrack();
    if (!impl_->videoTrack) {
        std::cerr << "[HAPDecoder] No video track found" << std::endl;
        close();
        return false;
    }

    impl_->totalSamples = static_cast<unsigned int>(impl_->videoTrack->samples.size());
    width_ = static_cast<int>(impl_->videoTrack->width);
    height_ = static_cast<int>(impl_->videoTrack->height);

    // Calculate duration and frame rate
    if (impl_->videoTrack->timescale > 0 && impl_->totalSamples > 0) {
        duration_ = static_cast<float>(impl_->videoTrack->duration) / static_cast<float>(impl_->videoTrack->timescale);
        frameRate_ = static_cast<float>(impl_->totalSamples) / duration_;
    }

    std::cout << "[HAPDecoder] Found video track: " << width_ << "x" << height_
              << ", " << impl_->totalSamples << " frames, "
              << frameRate_ << " fps, codec: " << impl_->videoTrack->codecFourCC << std::endl;

    // Check for audio track and initialize audio player
    impl_->audioTrack = impl_->mov.audioTrack();
    if (impl_->audioTrack && impl_->audioTrack->audioSampleRate > 0) {
        const char* codec = impl_->audioTrack->codecFourCC;
        // Check for supported PCM formats
        bool isPCM = (strcmp(codec, "sowt") == 0 ||  // little-endian 16-bit
                      strcmp(codec, "twos") == 0 ||  // big-endian 16-bit
                      strcmp(codec, "lpcm") == 0 ||  // linear PCM
                      strcmp(codec, "in24") == 0 ||  // 24-bit integer
                      strcmp(codec, "in32") == 0 ||  // 32-bit integer
                      strcmp(codec, "fl32") == 0 ||  // 32-bit float
                      strcmp(codec, "fl64") == 0);   // 64-bit float

        if (isPCM) {
            impl_->audioIsBigEndian = (strcmp(codec, "twos") == 0);
            impl_->totalAudioSamples = static_cast<unsigned int>(impl_->audioTrack->samples.size());

            // Initialize audio player
            audioPlayer_ = std::make_unique<AudioPlayer>();
            if (audioPlayer_->init(impl_->audioTrack->audioSampleRate, impl_->audioTrack->audioChannels)) {
                hasAudio_ = true;
                audioSampleRate_ = impl_->audioTrack->audioSampleRate;
                audioChannels_ = impl_->audioTrack->audioChannels;
                std::cout << "[HAPDecoder] Audio: " << audioSampleRate_ << "Hz, "
                          << audioChannels_ << " ch, " << impl_->audioTrack->audioBitsPerSample
                          << "-bit " << codec << std::endl;
            } else {
                std::cerr << "[HAPDecoder] Failed to initialize audio player" << std::endl;
                audioPlayer_.reset();
            }
        } else {
            std::cout << "[HAPDecoder] Unsupported audio codec: " << codec << std::endl;
        }
    }

    // Read first frame to determine HAP format
    if (impl_->totalSamples == 0 || impl_->videoTrack->samples.empty()) {
        std::cerr << "[HAPDecoder] No samples in video track" << std::endl;
        close();
        return false;
    }

    const MOVSample& firstSample = impl_->videoTrack->samples[0];
    if (firstSample.size == 0) {
        std::cerr << "[HAPDecoder] First frame has zero size" << std::endl;
        close();
        return false;
    }

    // Read the frame data
    impl_->frameBuffer.resize(firstSample.size);
#ifdef _WIN32
    _fseeki64(impl_->file, firstSample.offset, SEEK_SET);
#else
    fseeko(impl_->file, firstSample.offset, SEEK_SET);
#endif
    if (fread(impl_->frameBuffer.data(), 1, firstSample.size, impl_->file) != firstSample.size) {
        std::cerr << "[HAPDecoder] Failed to read first frame" << std::endl;
        close();
        return false;
    }

    // Determine HAP texture format
    unsigned int textureCount = 0;
    if (HapGetFrameTextureCount(impl_->frameBuffer.data(), firstSample.size, &textureCount) != HapResult_No_Error ||
        textureCount == 0) {
        std::cerr << "[HAPDecoder] Invalid HAP frame or not a HAP file" << std::endl;
        close();
        return false;
    }

    if (HapGetFrameTextureFormat(impl_->frameBuffer.data(), firstSample.size, 0, &impl_->hapTextureFormat)
        != HapResult_No_Error) {
        std::cerr << "[HAPDecoder] Cannot determine HAP texture format" << std::endl;
        close();
        return false;
    }

    // Map HAP format to WebGPU texture format
    size_t bytesPerBlock = 0;
    switch (impl_->hapTextureFormat) {
        case HapTextureFormat_RGB_DXT1:
            textureFormat_ = WGPUTextureFormat_BC1RGBAUnorm;
            bytesPerBlock = 8;
            std::cout << "[HAPDecoder] Format: HAP (DXT1/BC1)" << std::endl;
            break;
        case HapTextureFormat_RGBA_DXT5:
        case HapTextureFormat_YCoCg_DXT5:
            textureFormat_ = WGPUTextureFormat_BC3RGBAUnorm;
            bytesPerBlock = 16;
            std::cout << "[HAPDecoder] Format: HAP Alpha/Q (DXT5/BC3)" << std::endl;
            break;
        case HapTextureFormat_A_RGTC1:
            textureFormat_ = WGPUTextureFormat_BC4RUnorm;
            bytesPerBlock = 8;
            std::cout << "[HAPDecoder] Format: HAP Alpha-Only (RGTC1/BC4)" << std::endl;
            break;
        default:
            std::cerr << "[HAPDecoder] Unsupported HAP format: " << impl_->hapTextureFormat << std::endl;
            close();
            return false;
    }

    // Calculate DXT buffer size (width and height rounded up to 4)
    int blocksX = (width_ + 3) / 4;
    int blocksY = (height_ + 3) / 4;
    size_t dxtSize = blocksX * blocksY * bytesPerBlock;
    dxtBuffer_.resize(dxtSize);

    // Decode first frame
    unsigned long outputSize = 0;
    unsigned int outputFormat = 0;
    unsigned int result = HapDecode(impl_->frameBuffer.data(), firstSample.size, 0,
                                    hapDecodeCallback, nullptr,
                                    dxtBuffer_.data(), dxtBuffer_.size(),
                                    &outputSize, &outputFormat);

    if (result != HapResult_No_Error) {
        std::cerr << "[HAPDecoder] Failed to decode first frame: " << result << std::endl;
        close();
        return false;
    }

    // Create GPU texture
    createTexture();

    if (!texture_) {
        close();
        return false;
    }

    // Upload first frame
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = texture_;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = blocksX * bytesPerBlock;
    dataLayout.rowsPerImage = blocksY;

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};

    wgpuQueueWriteTexture(queue_, &destination, dxtBuffer_.data(), dxtBuffer_.size(),
                          &dataLayout, &writeSize);

    // Initialize playback state
    impl_->currentSample = 1; // Already decoded frame 0
    isPlaying_ = false; // Wait for play() call
    isFinished_ = false;
    currentTime_ = 0.0f;
    playbackTime_ = 0.0f;
    nextFrameTime_ = 0.0f;

    // Pre-buffer audio
    if (hasAudio_ && audioPlayer_) {
        prebufferAudio();
    }

    std::cout << "[HAPDecoder] Opened " << path
              << " (" << width_ << "x" << height_
              << ", " << duration_ << "s, " << frameRate_ << " fps"
              << (hasAudio_ ? ", with audio" : "") << ")" << std::endl;

    return true;
}

void HAPDecoder::close() {
    if (audioPlayer_) {
        audioPlayer_->pause();
        audioPlayer_->shutdown();
        audioPlayer_.reset();
    }

    impl_->cleanup();

    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }
    if (texture_) {
        wgpuTextureDestroy(texture_);
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }

    width_ = 0;
    height_ = 0;
    duration_ = 0.0f;
    frameRate_ = 30.0f;
    isPlaying_ = false;
    isFinished_ = true;
    hasAudio_ = false;
    currentTime_ = 0.0f;
    playbackTime_ = 0.0f;
    dxtBuffer_.clear();
}

bool HAPDecoder::isOpen() const {
    return impl_->file != nullptr && impl_->videoTrack != nullptr;
}

void HAPDecoder::update(Context& ctx) {
    if (!isPlaying_ || isFinished_ || !impl_->videoTrack) {
        return;
    }

    // Keep audio buffer topped up first
    if (hasAudio_ && audioPlayer_ && internalAudioEnabled_) {
        uint32_t targetFrames = audioSampleRate_ / 4;  // ~0.25 seconds ahead
        while (audioPlayer_->getBufferedFrames() < targetFrames &&
               impl_->currentAudioSample < impl_->totalAudioSamples) {
            feedAudioBuffer();
        }
    }

    // Use audio playback position as master clock if audio is available
    // Otherwise fall back to wall-clock timing
    double targetTime;
    if (audioPlayer_ && hasAudio_ && internalAudioEnabled_) {
        targetTime = audioPlayer_->getPlaybackPosition();
    } else {
        auto now = std::chrono::steady_clock::now();
        if (impl_->firstUpdate) {
            impl_->firstUpdate = false;
            impl_->lastUpdateTime = now;
        }
        float elapsed = std::chrono::duration<float>(now - impl_->lastUpdateTime).count();
        impl_->lastUpdateTime = now;
        playbackTime_ += elapsed;
        targetTime = playbackTime_;
    }

    // Check if we need a new frame based on target time
    if (targetTime < nextFrameTime_) {
        return;
    }

    // Check for end of video
    if (impl_->currentSample >= impl_->totalSamples) {
        if (isLooping_) {
            // Loop audio
            if (hasAudio_ && audioPlayer_) {
                loopAudioReader();
                prebufferAudio();
            }
            impl_->currentSample = 0;
            playbackTime_ = 0.0f;
            nextFrameTime_ = 0.0f;
            currentTime_ = 0.0f;
            impl_->firstUpdate = true;
            return;
        } else {
            isFinished_ = true;
            isPlaying_ = false;
            if (audioPlayer_) {
                audioPlayer_->pause();
            }
            return;
        }
    }

    // Find the frame that matches our target time
    // Skip frames if video is behind audio
    int framesSkipped = 0;
    while (impl_->currentSample < impl_->totalSamples) {
        // Calculate this frame's timestamp
        uint64_t frameTimestamp = 0;
        for (unsigned int i = 0; i < impl_->currentSample; i++) {
            frameTimestamp += impl_->videoTrack->samples[i].duration;
        }
        float frameTime = static_cast<float>(frameTimestamp) / static_cast<float>(impl_->videoTrack->timescale);
        float nextFrame = frameTime + (1.0f / frameRate_);

        // If this frame's end time is past our target, or we've skipped too many, use it
        if (nextFrame >= targetTime || framesSkipped >= 5) {
            currentTime_ = frameTime;
            nextFrameTime_ = nextFrame;
            break;
        }

        // Skip this frame
        impl_->currentSample++;
        framesSkipped++;
    }

    if (impl_->currentSample >= impl_->totalSamples) {
        return;
    }

    // Get frame info from our MOV structure
    const MOVSample& sample = impl_->videoTrack->samples[impl_->currentSample];

    if (sample.size == 0) {
        impl_->currentSample++;
        return;
    }

    // Read frame data
    if (impl_->frameBuffer.size() < sample.size) {
        impl_->frameBuffer.resize(sample.size);
    }

#ifdef _WIN32
    _fseeki64(impl_->file, sample.offset, SEEK_SET);
#else
    fseeko(impl_->file, sample.offset, SEEK_SET);
#endif
    if (fread(impl_->frameBuffer.data(), 1, sample.size, impl_->file) != sample.size) {
        std::cerr << "[HAPDecoder] Failed to read frame " << impl_->currentSample << std::endl;
        impl_->currentSample++;
        return;
    }

    // Decode HAP frame to DXT
    unsigned long outputSize = 0;
    unsigned int outputFormat = 0;
    unsigned int result = HapDecode(impl_->frameBuffer.data(), sample.size, 0,
                                    hapDecodeCallback, nullptr,
                                    dxtBuffer_.data(), dxtBuffer_.size(),
                                    &outputSize, &outputFormat);

    if (result == HapResult_No_Error) {
        // Upload DXT data to GPU texture
        int blocksX = (width_ + 3) / 4;
        size_t bytesPerBlock = (impl_->hapTextureFormat == HapTextureFormat_RGB_DXT1 ||
                                impl_->hapTextureFormat == HapTextureFormat_A_RGTC1) ? 8 : 16;

        WGPUTexelCopyTextureInfo destination = {};
        destination.texture = texture_;
        destination.mipLevel = 0;
        destination.origin = {0, 0, 0};
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout dataLayout = {};
        dataLayout.offset = 0;
        dataLayout.bytesPerRow = blocksX * bytesPerBlock;
        dataLayout.rowsPerImage = (height_ + 3) / 4;

        WGPUExtent3D writeSize = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};

        wgpuQueueWriteTexture(queue_, &destination, dxtBuffer_.data(), dxtBuffer_.size(),
                              &dataLayout, &writeSize);
    }

    impl_->currentSample++;
}

void HAPDecoder::seek(float seconds) {
    if (!impl_->videoTrack) {
        return;
    }

    if (seconds < 0) seconds = 0;
    if (seconds > duration_) seconds = duration_;

    // Find the sample closest to the requested time using accumulated durations
    if (impl_->videoTrack->timescale > 0) {
        uint64_t targetTimestamp = static_cast<uint64_t>(seconds * impl_->videoTrack->timescale);
        uint64_t accumulatedTime = 0;

        impl_->currentSample = 0;
        for (unsigned int i = 0; i < impl_->totalSamples; i++) {
            if (accumulatedTime >= targetTimestamp) {
                impl_->currentSample = i > 0 ? i - 1 : 0;
                break;
            }
            accumulatedTime += impl_->videoTrack->samples[i].duration;
            impl_->currentSample = i;
        }
    } else {
        // Fallback: estimate sample index from time
        impl_->currentSample = static_cast<unsigned int>(seconds * frameRate_);
    }

    if (impl_->currentSample >= impl_->totalSamples) {
        impl_->currentSample = impl_->totalSamples > 0 ? impl_->totalSamples - 1 : 0;
    }

    currentTime_ = seconds;
    playbackTime_ = seconds;
    nextFrameTime_ = seconds;
    isFinished_ = false;
    impl_->firstUpdate = true;

    // Decode the frame at the seek position immediately
    if (impl_->currentSample < impl_->totalSamples) {
        const MOVSample& sample = impl_->videoTrack->samples[impl_->currentSample];

        if (sample.size > 0) {
            if (impl_->frameBuffer.size() < sample.size) {
                impl_->frameBuffer.resize(sample.size);
            }

#ifdef _WIN32
            _fseeki64(impl_->file, sample.offset, SEEK_SET);
#else
            fseeko(impl_->file, sample.offset, SEEK_SET);
#endif
            if (fread(impl_->frameBuffer.data(), 1, sample.size, impl_->file) == sample.size) {
                unsigned long outputSize = 0;
                unsigned int outputFormat = 0;
                unsigned int result = HapDecode(impl_->frameBuffer.data(), sample.size, 0,
                                                hapDecodeCallback, nullptr,
                                                dxtBuffer_.data(), dxtBuffer_.size(),
                                                &outputSize, &outputFormat);

                if (result == HapResult_No_Error && texture_) {
                    int blocksX = (width_ + 3) / 4;
                    size_t bytesPerBlock = (impl_->hapTextureFormat == HapTextureFormat_RGB_DXT1 ||
                                            impl_->hapTextureFormat == HapTextureFormat_A_RGTC1) ? 8 : 16;

                    WGPUTexelCopyTextureInfo destination = {};
                    destination.texture = texture_;
                    destination.mipLevel = 0;
                    destination.origin = {0, 0, 0};
                    destination.aspect = WGPUTextureAspect_All;

                    WGPUTexelCopyBufferLayout dataLayout = {};
                    dataLayout.offset = 0;
                    dataLayout.bytesPerRow = blocksX * bytesPerBlock;
                    dataLayout.rowsPerImage = (height_ + 3) / 4;

                    WGPUExtent3D writeSize = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};

                    wgpuQueueWriteTexture(queue_, &destination, dxtBuffer_.data(), dxtBuffer_.size(),
                                          &dataLayout, &writeSize);
                }
            }
        }

        impl_->currentSample++; // Move to next frame for subsequent update()
    }

    // Reset audio position and rebuffer
    if (hasAudio_ && audioPlayer_) {
        // Find audio sample closest to seek time
        if (impl_->audioTrack && impl_->audioTrack->timescale > 0) {
            uint64_t targetTimestamp = static_cast<uint64_t>(seconds * impl_->audioTrack->timescale);
            uint64_t accumulatedTime = 0;
            impl_->currentAudioSample = 0;
            for (unsigned int i = 0; i < impl_->totalAudioSamples; i++) {
                if (accumulatedTime >= targetTimestamp) {
                    impl_->currentAudioSample = i > 0 ? i - 1 : 0;
                    break;
                }
                accumulatedTime += impl_->audioTrack->samples[i].duration;
                impl_->currentAudioSample = i;
            }
        } else {
            impl_->currentAudioSample = 0;
        }
        audioPlayer_->flush();
        prebufferAudio();
    }
}

void HAPDecoder::pause() {
    isPlaying_ = false;
    impl_->firstUpdate = true; // Reset timing on next play
    if (audioPlayer_) {
        audioPlayer_->pause();
    }
}

void HAPDecoder::play() {
    if (!isFinished_ && impl_->videoTrack) {
        isPlaying_ = true;
        impl_->firstUpdate = true;
        if (audioPlayer_) {
            audioPlayer_->play();
        }
    }
}

void HAPDecoder::setVolume(float volume) {
    if (audioPlayer_) {
        audioPlayer_->setVolume(volume);
    }
}

float HAPDecoder::getVolume() const {
    return audioPlayer_ ? audioPlayer_->getVolume() : 1.0f;
}

void HAPDecoder::resetReader() {
    if (impl_->videoTrack) {
        impl_->currentSample = 0;
        impl_->firstUpdate = true;
    }
}

void HAPDecoder::prebufferAudio() {
    if (!audioPlayer_ || !impl_->audioTrack || !hasAudio_) return;

    // Target ~0.5 seconds of audio buffered
    uint32_t targetFrames = audioSampleRate_ / 2;

    while (audioPlayer_->getBufferedFrames() < targetFrames &&
           impl_->currentAudioSample < impl_->totalAudioSamples) {
        feedAudioBuffer();
    }

    // Start audio playback
    audioPlayer_->play();
}

void HAPDecoder::feedAudioBuffer() {
    if (!audioPlayer_ || !impl_->audioTrack || !hasAudio_ || !internalAudioEnabled_) return;
    if (impl_->currentAudioSample >= impl_->totalAudioSamples) return;

    const MOVSample& sample = impl_->audioTrack->samples[impl_->currentAudioSample];
    if (sample.size == 0) {
        impl_->currentAudioSample++;
        return;
    }

    // Resize buffer if needed
    if (impl_->audioBuffer.size() < sample.size) {
        impl_->audioBuffer.resize(sample.size);
    }

    // Read audio sample data
#ifdef _WIN32
    _fseeki64(impl_->file, sample.offset, SEEK_SET);
#else
    fseeko(impl_->file, sample.offset, SEEK_SET);
#endif
    if (fread(impl_->audioBuffer.data(), 1, sample.size, impl_->file) != sample.size) {
        impl_->currentAudioSample++;
        return;
    }

    // Convert to float samples based on format
    uint16_t bitsPerSample = impl_->audioTrack->audioBitsPerSample;
    uint16_t channels = impl_->audioTrack->audioChannels;
    uint32_t bytesPerSample = bitsPerSample / 8;
    uint32_t frameCount = sample.size / (bytesPerSample * channels);

    std::vector<float> floatSamples(frameCount * channels);

    if (bitsPerSample == 16) {
        // 16-bit PCM
        int16_t* src = reinterpret_cast<int16_t*>(impl_->audioBuffer.data());
        for (uint32_t i = 0; i < frameCount * channels; i++) {
            int16_t val = src[i];
            // Swap bytes if big-endian
            if (impl_->audioIsBigEndian) {
                val = static_cast<int16_t>(((val & 0xFF) << 8) | ((val >> 8) & 0xFF));
            }
            floatSamples[i] = static_cast<float>(val) / 32768.0f;
        }
    } else if (bitsPerSample == 24) {
        // 24-bit PCM
        uint8_t* src = impl_->audioBuffer.data();
        for (uint32_t i = 0; i < frameCount * channels; i++) {
            int32_t val;
            if (impl_->audioIsBigEndian) {
                val = (src[0] << 24) | (src[1] << 16) | (src[2] << 8);
            } else {
                val = (src[2] << 24) | (src[1] << 16) | (src[0] << 8);
            }
            val >>= 8;  // Sign-extend
            floatSamples[i] = static_cast<float>(val) / 8388608.0f;
            src += 3;
        }
    } else if (bitsPerSample == 32) {
        // Could be 32-bit int or float
        const char* codec = impl_->audioTrack->codecFourCC;
        if (strcmp(codec, "fl32") == 0) {
            // 32-bit float
            memcpy(floatSamples.data(), impl_->audioBuffer.data(), sample.size);
        } else {
            // 32-bit integer
            int32_t* src = reinterpret_cast<int32_t*>(impl_->audioBuffer.data());
            for (uint32_t i = 0; i < frameCount * channels; i++) {
                floatSamples[i] = static_cast<float>(src[i]) / 2147483648.0f;
            }
        }
    }

    // Push to audio player
    audioPlayer_->pushSamples(floatSamples.data(), frameCount);
    impl_->currentAudioSample++;
}

void HAPDecoder::loopAudioReader() {
    if (impl_->audioTrack) {
        impl_->currentAudioSample = 0;
        if (audioPlayer_) {
            audioPlayer_->flush();
        }
    }
}

uint32_t HAPDecoder::readAudioSamples(float* buffer, uint32_t maxFrames) {
    // External audio extraction not implemented for HAP
    return 0;
}

uint32_t HAPDecoder::readAudioSamplesForPTS(float* buffer, double videoPTS, uint32_t maxFrames) {
    // External audio extraction not implemented for HAP
    return 0;
}

double HAPDecoder::audioAvailableStartPTS() const {
    return 0.0;
}

double HAPDecoder::audioAvailableEndPTS() const {
    return 0.0;
}

void HAPDecoder::setInternalAudioEnabled(bool enable) {
    internalAudioEnabled_ = enable;
}

} // namespace vivid::video

#endif // !__APPLE__
