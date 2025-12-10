// HAP Decoder - Uses Vidvox HAP library for efficient DXT texture decoding
// Uploads compressed textures directly to GPU without CPU pixel conversion

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

#include <vivid/video/hap_decoder.h>
#include <vivid/video/audio_player.h>
#include <vivid/context.h>
#include "hap.h"

#include <iostream>

// Helper to create WGPUStringView from C string
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

namespace vivid::video {

// HAP decode callback for single-threaded decoding
static void hapDecodeCallback(HapDecodeWorkFunction function, void *p,
                              unsigned int count, void *info) {
    for (unsigned int i = 0; i < count; i++) {
        function(p, i);
    }
}

struct HAPDecoder::Impl {
    AVAsset* asset = nil;
    AVAssetReader* reader = nil;
    AVAssetReaderTrackOutput* videoOutput = nil;
    AVAssetReaderAudioMixOutput* audioOutput = nil;
    CMTime frameDuration = kCMTimeZero;

    // Separate audio reader for independent looping
    AVAssetReader* audioReader = nil;

    // HAP texture format (DXT1/DXT5/etc)
    unsigned int hapTextureFormat = 0;

    void cleanup() {
        if (reader) {
            [reader cancelReading];
            reader = nil;
        }
        if (audioReader) {
            [audioReader cancelReading];
            audioReader = nil;
        }
        videoOutput = nil;
        audioOutput = nil;
        asset = nil;
    }
};

HAPDecoder::HAPDecoder() : impl_(std::make_unique<Impl>()) {}

HAPDecoder::~HAPDecoder() {
    close();
}

bool HAPDecoder::isHAPFile(const std::string& path) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        AVAsset* asset = [AVAsset assetWithURL:url];

        // Check video tracks for HAP codec
        NSArray* videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
        if (videoTracks.count == 0) return false;

        AVAssetTrack* track = videoTracks[0];
        NSArray* formatDescriptions = track.formatDescriptions;
        if (formatDescriptions.count == 0) return false;

        CMFormatDescriptionRef desc = (__bridge CMFormatDescriptionRef)formatDescriptions[0];
        FourCharCode codec = CMFormatDescriptionGetMediaSubType(desc);

        // HAP codec FourCC codes
        return (codec == 'Hap1' || codec == 'Hap5' || codec == 'HapY' ||
                codec == 'HapM' || codec == 'HapA');
    }
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
    desc.label = toStringView("HAPVideoFrame");
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
    viewDesc.label = toStringView("HAPVideoFrameView");
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

    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        impl_->asset = [AVAsset assetWithURL:url];

        if (!impl_->asset) {
            std::cerr << "[HAPDecoder] Failed to create asset: " << path << std::endl;
            return false;
        }

        // Get video track
        NSArray* videoTracks = [impl_->asset tracksWithMediaType:AVMediaTypeVideo];
        if (videoTracks.count == 0) {
            std::cerr << "[HAPDecoder] No video track found" << std::endl;
            close();
            return false;
        }

        AVAssetTrack* videoTrack = videoTracks[0];

        // Get video dimensions and frame rate
        CGSize size = videoTrack.naturalSize;
        width_ = static_cast<int>(size.width);
        height_ = static_cast<int>(size.height);
        frameRate_ = videoTrack.nominalFrameRate;
        if (frameRate_ <= 0) frameRate_ = 30.0f;

        duration_ = CMTimeGetSeconds(impl_->asset.duration);

        // Calculate frame duration
        impl_->frameDuration = CMTimeMake(1, static_cast<int32_t>(frameRate_));

        // Create asset reader
        NSError* error = nil;
        impl_->reader = [[AVAssetReader alloc] initWithAsset:impl_->asset error:&error];
        if (error) {
            std::cerr << "[HAPDecoder] Failed to create reader: "
                      << error.localizedDescription.UTF8String << std::endl;
            close();
            return false;
        }

        // Configure video output to pass through raw sample data (no decoding)
        impl_->videoOutput = [[AVAssetReaderTrackOutput alloc]
                              initWithTrack:videoTrack
                              outputSettings:nil];  // nil = pass through
        impl_->videoOutput.alwaysCopiesSampleData = YES;

        if ([impl_->reader canAddOutput:impl_->videoOutput]) {
            [impl_->reader addOutput:impl_->videoOutput];
        } else {
            std::cerr << "[HAPDecoder] Cannot add video output" << std::endl;
            close();
            return false;
        }

        // Setup audio if available
        NSArray* audioTracks = [impl_->asset tracksWithMediaType:AVMediaTypeAudio];
        if (audioTracks.count > 0) {
            NSDictionary* audioSettings = @{
                AVFormatIDKey: @(kAudioFormatLinearPCM),
                AVLinearPCMBitDepthKey: @32,
                AVLinearPCMIsFloatKey: @YES,
                AVLinearPCMIsNonInterleaved: @NO,
                AVSampleRateKey: @48000,
                AVNumberOfChannelsKey: @2
            };

            impl_->audioOutput = [[AVAssetReaderAudioMixOutput alloc]
                                  initWithAudioTracks:audioTracks
                                  audioSettings:audioSettings];

            if ([impl_->reader canAddOutput:impl_->audioOutput]) {
                [impl_->reader addOutput:impl_->audioOutput];

                // Initialize audio ring buffer for external reading
                audioRingBuffer_.resize(AUDIO_RING_SIZE);
                std::fill(audioRingBuffer_.begin(), audioRingBuffer_.end(), 0.0f);
                audioWritePos_ = 0;
                audioReadPos_ = 0;
                audioStartPTS_ = 0.0;
                audioEndPTS_ = 0.0;

                audioPlayer_ = std::make_unique<AudioPlayer>();
                if (audioPlayer_->init(48000, 2)) {
                    hasAudio_ = true;
                    std::cout << "[HAPDecoder] Audio: 48000Hz, 2 ch" << std::endl;
                } else {
                    audioPlayer_.reset();
                }
            }
        }

        // Start reading
        if (![impl_->reader startReading]) {
            std::cerr << "[HAPDecoder] Failed to start reading: "
                      << impl_->reader.error.localizedDescription.UTF8String << std::endl;
            close();
            return false;
        }

        // Read first frame to determine HAP format and create texture
        CMSampleBufferRef sampleBuffer = nil;
        CMBlockBufferRef blockBuffer = nil;
        size_t dataLength = 0;
        std::vector<char> frameBuffer;
        char* dataPtr = nullptr;

        const int maxAttempts = 10;
        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            sampleBuffer = [impl_->videoOutput copyNextSampleBuffer];
            if (!sampleBuffer) {
                std::cerr << "[HAPDecoder] Failed to read sample" << std::endl;
                close();
                return false;
            }

            CMItemCount numSamples = CMSampleBufferGetNumSamples(sampleBuffer);
            blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);

            if (numSamples > 0 && blockBuffer) {
                dataLength = CMBlockBufferGetDataLength(blockBuffer);
                if (dataLength > 0) {
                    break;
                }
            }

            CFRelease(sampleBuffer);
            sampleBuffer = nil;
            blockBuffer = nil;
        }

        if (!sampleBuffer || !blockBuffer) {
            std::cerr << "[HAPDecoder] No valid frames found" << std::endl;
            close();
            return false;
        }

        // Get pointer to the compressed data
        if (CMBlockBufferIsRangeContiguous(blockBuffer, 0, dataLength)) {
            CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, nullptr, &dataPtr);
        } else {
            frameBuffer.resize(dataLength);
            CMBlockBufferCopyDataBytes(blockBuffer, 0, dataLength, frameBuffer.data());
            dataPtr = frameBuffer.data();
        }

        if (dataLength == 0 || dataPtr == nullptr) {
            std::cerr << "[HAPDecoder] No data in sample buffer" << std::endl;
            CFRelease(sampleBuffer);
            close();
            return false;
        }

        // Determine HAP texture format
        unsigned int textureCount = 0;
        if (HapGetFrameTextureCount(dataPtr, dataLength, &textureCount) != HapResult_No_Error ||
            textureCount == 0) {
            std::cerr << "[HAPDecoder] Invalid HAP frame" << std::endl;
            CFRelease(sampleBuffer);
            close();
            return false;
        }

        if (HapGetFrameTextureFormat(dataPtr, dataLength, 0, &impl_->hapTextureFormat)
            != HapResult_No_Error) {
            std::cerr << "[HAPDecoder] Cannot determine HAP texture format" << std::endl;
            CFRelease(sampleBuffer);
            close();
            return false;
        }

        // Determine WebGPU texture format from HAP format
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
                std::cerr << "[HAPDecoder] Unsupported HAP format: " << impl_->hapTextureFormat
                          << std::endl;
                CFRelease(sampleBuffer);
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
        unsigned int result = HapDecode(dataPtr, dataLength, 0,
                                        hapDecodeCallback, nullptr,
                                        dxtBuffer_.data(), dxtBuffer_.size(),
                                        &outputSize, &outputFormat);

        CFRelease(sampleBuffer);

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

        isPlaying_ = true;
        isFinished_ = false;
        currentTime_ = 0.0f;
        playbackTime_ = 0.0f;
        nextFrameTime_ = 0.0f;

        // Pre-buffer audio (but don't start playback until play() is called)
        if (audioPlayer_) {
            prebufferAudio();
            // Only auto-start audio if internal audio is enabled
            if (internalAudioEnabled_) {
                audioPlayer_->play();
            }
        }

        std::cout << "[HAPDecoder] Opened " << path
                  << " (" << width_ << "x" << height_
                  << ", " << frameRate_ << "fps)" << std::endl;

        return true;
    }
}

void HAPDecoder::feedAudioBuffer() {
    if (!impl_->audioOutput) {
        return;
    }

    @autoreleasepool {
        const uint32_t channels = 2;

        // Two modes:
        // 1. Internal audio enabled: push directly to AudioPlayer (no ring buffer)
        // 2. Internal audio disabled: write to ring buffer for external reading

        if (internalAudioEnabled_ && audioPlayer_) {
            // Internal audio mode - feed AudioPlayer directly
            const uint32_t minBufferFrames = 48000 / 4;
            while (audioPlayer_->getBufferedFrames() < minBufferFrames) {
                CMSampleBufferRef sampleBuffer = [impl_->audioOutput copyNextSampleBuffer];
                if (!sampleBuffer) break;

                CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
                if (blockBuffer) {
                    size_t dataLength = 0;
                    char* dataPtr = nullptr;
                    CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &dataLength, &dataPtr);

                    size_t frameCount = dataLength / (channels * sizeof(float));
                    audioPlayer_->pushSamples(reinterpret_cast<float*>(dataPtr), frameCount);
                }

                CFRelease(sampleBuffer);
            }
        } else {
            // External audio mode - write to ring buffer for VideoAudio to read

            // Check if we need to loop audio first (outside the lock)
            if (audioNeedsLoop_) {
                if (isLooping_) {
                    loopAudioReader();
                }
                audioNeedsLoop_ = false;
            }

            std::lock_guard<std::mutex> lock(audioMutex_);

            // Calculate how many samples are in the buffer
            uint32_t used = (audioWritePos_ >= audioReadPos_)
                ? (audioWritePos_ - audioReadPos_)
                : (AUDIO_RING_SIZE - audioReadPos_ + audioWritePos_);

            // Keep the buffer at least half full
            // This is the main throttle - don't decode more than needed
            if (used > AUDIO_RING_SIZE / 2) return;

            uint32_t available = AUDIO_RING_SIZE - used - 1;

            // Read audio samples from AVAssetReader into ring buffer
            while (available > 1024 * channels) {
                CMSampleBufferRef sampleBuffer = [impl_->audioOutput copyNextSampleBuffer];
                if (!sampleBuffer) {
                    // Audio EOF - mark for looping on next call
                    if (isLooping_) {
                        audioNeedsLoop_ = true;
                    }
                    break;
                }

                // Extract PTS from this audio sample buffer
                CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
                double samplePTS = CMTimeGetSeconds(pts);

                CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
                if (blockBuffer) {
                    size_t dataLength = 0;
                    char* dataPtr = nullptr;
                    CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &dataLength, &dataPtr);

                    size_t sampleCount = dataLength / sizeof(float);
                    size_t frameCount = sampleCount / channels;
                    size_t toCopy = std::min(sampleCount, (size_t)available);
                    size_t framesToCopy = toCopy / channels;
                    float* audioData = reinterpret_cast<float*>(dataPtr);

                    // Copy to ring buffer
                    for (size_t i = 0; i < toCopy; ++i) {
                        audioRingBuffer_[audioWritePos_] = audioData[i];
                        audioWritePos_ = (audioWritePos_ + 1) % AUDIO_RING_SIZE;
                    }

                    // Update audio end PTS based on samples written
                    audioEndPTS_ = samplePTS + (double)framesToCopy / AUDIO_SAMPLE_RATE_D;

                    available -= toCopy;
                }

                CFRelease(sampleBuffer);
            }
        }
    }
}

void HAPDecoder::loopAudioReader() {
    if (!impl_->asset) return;

    @autoreleasepool {
        NSArray* audioTracks = [impl_->asset tracksWithMediaType:AVMediaTypeAudio];
        if (audioTracks.count == 0) {
            std::cerr << "[HAPDecoder] No audio tracks found for loop" << std::endl;
            return;
        }

        // Clean up old audio reader if exists
        if (impl_->audioReader) {
            [impl_->audioReader cancelReading];
            impl_->audioReader = nil;
        }

        // Create a separate audio reader for looping
        NSError* error = nil;
        impl_->audioReader = [[AVAssetReader alloc] initWithAsset:impl_->asset error:&error];
        if (error || !impl_->audioReader) {
            std::cerr << "[HAPDecoder] Failed to create audio reader for loop: "
                      << (error ? [[error localizedDescription] UTF8String] : "unknown") << std::endl;
            return;
        }

        NSDictionary* audioSettings = @{
            AVFormatIDKey: @(kAudioFormatLinearPCM),
            AVLinearPCMBitDepthKey: @32,
            AVLinearPCMIsFloatKey: @YES,
            AVLinearPCMIsNonInterleaved: @NO,
            AVSampleRateKey: @48000,
            AVNumberOfChannelsKey: @2
        };

        AVAssetReaderAudioMixOutput* newAudioOutput = [[AVAssetReaderAudioMixOutput alloc]
            initWithAudioTracks:audioTracks
            audioSettings:audioSettings];

        if (![impl_->audioReader canAddOutput:newAudioOutput]) {
            std::cerr << "[HAPDecoder] Failed to add audio output to reader for loop" << std::endl;
            [impl_->audioReader cancelReading];
            impl_->audioReader = nil;
            return;
        }

        [impl_->audioReader addOutput:newAudioOutput];
        [impl_->audioReader startReading];

        // Replace the audio output
        impl_->audioOutput = newAudioOutput;
    }
}

void HAPDecoder::prebufferAudio() {
    // Pre-fill audio buffer during init/seek
    // This bypasses the normal timing throttle to ensure we have audio ready
    if (!impl_->audioOutput || !hasAudio_ || internalAudioEnabled_) {
        feedAudioBuffer();
        return;
    }

    @autoreleasepool {
        std::lock_guard<std::mutex> lock(audioMutex_);
        const uint32_t channels = 2;

        // Target: fill at least 200ms of audio from current position
        double targetEndPTS = audioStartPTS_ + 0.2;

        uint32_t used = (audioWritePos_ >= audioReadPos_)
            ? (audioWritePos_ - audioReadPos_)
            : (AUDIO_RING_SIZE - audioReadPos_ + audioWritePos_);
        uint32_t available = AUDIO_RING_SIZE - used - 1;

        // Keep filling until we have enough
        while (available > 1024 * channels && audioEndPTS_ < targetEndPTS) {
            CMSampleBufferRef sampleBuffer = [impl_->audioOutput copyNextSampleBuffer];
            if (!sampleBuffer) break;

            CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
            double samplePTS = CMTimeGetSeconds(pts);

            CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
            if (blockBuffer) {
                size_t dataLength = 0;
                char* dataPtr = nullptr;
                CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &dataLength, &dataPtr);

                size_t sampleCount = dataLength / sizeof(float);
                size_t toCopy = std::min(sampleCount, (size_t)available);
                size_t framesToCopy = toCopy / channels;
                float* audioData = reinterpret_cast<float*>(dataPtr);

                // Keep audioStartPTS_ as set by seek() - don't change it based on sample PTS
                // This ensures audio timing aligns with video even if AVFoundation
                // returns samples with slightly different PTS than requested

                for (size_t i = 0; i < toCopy; ++i) {
                    audioRingBuffer_[audioWritePos_] = audioData[i];
                    audioWritePos_ = (audioWritePos_ + 1) % AUDIO_RING_SIZE;
                }

                audioEndPTS_ = samplePTS + (double)framesToCopy / AUDIO_SAMPLE_RATE_D;
                available -= toCopy;
            }

            CFRelease(sampleBuffer);
        }
    }
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

    isPlaying_ = false;
    isFinished_ = true;
    hasAudio_ = false;
    currentTime_ = 0.0f;
}

bool HAPDecoder::isOpen() const {
    return impl_->reader != nil;
}

void HAPDecoder::pause() {
    isPlaying_ = false;
    if (audioPlayer_) {
        audioPlayer_->pause();
    }
}

void HAPDecoder::play() {
    if (!isFinished_) {
        isPlaying_ = true;
        // Only start audio if internal audio is enabled
        if (audioPlayer_ && internalAudioEnabled_) {
            audioPlayer_->play();
        }
    }
}

void HAPDecoder::setInternalAudioEnabled(bool enable) {
    bool wasEnabled = internalAudioEnabled_;
    internalAudioEnabled_ = enable;

    if (audioPlayer_) {
        if (enable) {
            audioPlayer_->play();
        } else {
            audioPlayer_->pause();
        }
    }

    // When switching from internal to external audio mode,
    // we need to seek the audio reader back to the current video position
    // and fill the ring buffer so VideoAudio can read from the right place
    if (wasEnabled && !enable && hasAudio_) {
        // Seek to current video position to resync audio
        float currentPos = currentTime_;
        seek(currentPos);
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

uint32_t HAPDecoder::readAudioSamples(float* buffer, uint32_t maxFrames) {
    if (!hasAudio_ || !buffer || maxFrames == 0) {
        return 0;
    }

    const uint32_t channels = AUDIO_CHANNELS;
    const uint32_t maxSamples = maxFrames * channels;

    std::lock_guard<std::mutex> lock(audioMutex_);

    // Calculate available samples in ring buffer
    uint32_t available = (audioWritePos_ >= audioReadPos_)
        ? (audioWritePos_ - audioReadPos_)
        : (AUDIO_RING_SIZE - audioReadPos_ + audioWritePos_);

    uint32_t samplesToRead = std::min(maxSamples, available);
    uint32_t framesRead = samplesToRead / channels;

    // Read from ring buffer
    for (uint32_t i = 0; i < samplesToRead; ++i) {
        buffer[i] = audioRingBuffer_[audioReadPos_];
        audioReadPos_ = (audioReadPos_ + 1) % AUDIO_RING_SIZE;
    }

    // Update start PTS to reflect consumed samples
    audioStartPTS_ += (double)framesRead / AUDIO_SAMPLE_RATE_D;

    return framesRead;
}

uint32_t HAPDecoder::readAudioSamplesForPTS(float* buffer, double videoPTS, uint32_t maxFrames) {
    if (!hasAudio_ || !buffer || maxFrames == 0) {
        return 0;
    }

    const uint32_t channels = AUDIO_CHANNELS;
    const uint32_t maxSamples = maxFrames * channels;

    std::lock_guard<std::mutex> lock(audioMutex_);

    // Calculate available samples in ring buffer
    uint32_t available = (audioWritePos_ >= audioReadPos_)
        ? (audioWritePos_ - audioReadPos_)
        : (AUDIO_RING_SIZE - audioReadPos_ + audioWritePos_);

    if (available == 0) {
        return 0;
    }

    // Sync strategy: position read cursor to match video PTS
    // audioStartPTS_ represents the PTS of the sample at audioReadPos_
    //
    // Goal: read audio samples corresponding to [videoPTS, videoPTS + frameDuration]
    // where frameDuration = maxFrames / AUDIO_SAMPLE_RATE

    // First, if video is past our buffer, we have nothing to give
    if (videoPTS > audioEndPTS_) {
        return 0;
    }

    // If video is significantly behind our buffer start, we can't provide audio
    // Return 0 and wait for video to catch up
    // Tolerance accounts for audio chunk size (~21ms) and timing jitter
    const double syncTolerance = 0.025;  // 25ms

    if (videoPTS < audioStartPTS_ - syncTolerance) {
        return 0;
    }

    // Position read cursor to match video PTS
    // Skip samples if video is ahead of current read position
    double skipTime = videoPTS - audioStartPTS_;
    if (skipTime > syncTolerance) {
        uint32_t skipFrames = static_cast<uint32_t>(skipTime * AUDIO_SAMPLE_RATE_D);
        uint32_t samplesToSkip = std::min(skipFrames * channels, available);

        audioReadPos_ = (audioReadPos_ + samplesToSkip) % AUDIO_RING_SIZE;
        available -= samplesToSkip;
        audioStartPTS_ = videoPTS;  // Set exactly to video PTS
    }

    // Read the requested amount
    uint32_t samplesToRead = std::min(maxSamples, available);
    uint32_t framesRead = samplesToRead / channels;

    // Read from ring buffer
    for (uint32_t i = 0; i < samplesToRead; ++i) {
        buffer[i] = audioRingBuffer_[audioReadPos_];
        audioReadPos_ = (audioReadPos_ + 1) % AUDIO_RING_SIZE;
    }

    // Update start PTS to reflect consumed samples
    audioStartPTS_ += (double)framesRead / AUDIO_SAMPLE_RATE_D;

    return framesRead;
}

double HAPDecoder::audioAvailableStartPTS() const {
    std::lock_guard<std::mutex> lock(audioMutex_);
    return audioStartPTS_;
}

double HAPDecoder::audioAvailableEndPTS() const {
    std::lock_guard<std::mutex> lock(audioMutex_);
    return audioEndPTS_;
}

void HAPDecoder::update(Context& ctx) {
    if (!isPlaying_ || isFinished_ || !impl_->reader) {
        return;
    }

    // Advance playback time
    playbackTime_ += static_cast<float>(ctx.dt());

    // Keep audio buffer fed (always read to stay in sync)
    if (hasAudio_ && impl_->audioOutput) {
        feedAudioBuffer();
    }

    // Check if it's time for the next frame
    if (playbackTime_ < nextFrameTime_) {
        return;
    }

    @autoreleasepool {
        CMSampleBufferRef sampleBuffer = nil;
        CMBlockBufferRef blockBuffer = nil;
        size_t dataLength = 0;

        const int maxAttempts = 5;
        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            sampleBuffer = [impl_->videoOutput copyNextSampleBuffer];
            if (!sampleBuffer) {
                if (isLooping_) {
                    seek(0.0f);
                } else {
                    isFinished_ = true;
                    isPlaying_ = false;
                    if (audioPlayer_) {
                        audioPlayer_->pause();
                    }
                }
                return;
            }

            CMItemCount numSamples = CMSampleBufferGetNumSamples(sampleBuffer);
            blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);

            if (numSamples > 0 && blockBuffer) {
                dataLength = CMBlockBufferGetDataLength(blockBuffer);
                if (dataLength > 0) {
                    break;
                }
            }

            CFRelease(sampleBuffer);
            sampleBuffer = nil;
            blockBuffer = nil;
        }

        if (!sampleBuffer || !blockBuffer || dataLength == 0) {
            return;
        }

        // Get presentation time
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        currentTime_ = CMTimeGetSeconds(pts);

        // Get data pointer
        std::vector<char> frameBuffer;
        char* dataPtr = nullptr;

        if (CMBlockBufferIsRangeContiguous(blockBuffer, 0, dataLength)) {
            CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, nullptr, &dataPtr);
        } else {
            frameBuffer.resize(dataLength);
            CMBlockBufferCopyDataBytes(blockBuffer, 0, dataLength, frameBuffer.data());
            dataPtr = frameBuffer.data();
        }

        // Decode HAP frame to DXT
        unsigned long outputSize = 0;
        unsigned int outputFormat = 0;
        unsigned int result = HapDecode(dataPtr, dataLength, 0,
                                        hapDecodeCallback, nullptr,
                                        dxtBuffer_.data(), dxtBuffer_.size(),
                                        &outputSize, &outputFormat);

        CFRelease(sampleBuffer);

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

        // Schedule next frame
        float frameDuration = 1.0f / frameRate_;
        nextFrameTime_ = playbackTime_ + frameDuration;
    }
}

void HAPDecoder::seek(float seconds) {
    if (!impl_->asset) return;

    if (seconds < 0) seconds = 0;
    if (seconds > duration_) seconds = duration_;

    @autoreleasepool {
        // Cancel current reader but keep the asset
        if (impl_->reader) {
            [impl_->reader cancelReading];
            impl_->reader = nil;
        }
        impl_->videoOutput = nil;
        impl_->audioOutput = nil;

        NSError* error = nil;
        impl_->reader = [[AVAssetReader alloc] initWithAsset:impl_->asset error:&error];
        if (error) {
            std::cerr << "[HAPDecoder] Failed to recreate reader for seek" << std::endl;
            return;
        }

        NSArray* videoTracks = [impl_->asset tracksWithMediaType:AVMediaTypeVideo];
        AVAssetTrack* videoTrack = videoTracks[0];

        impl_->videoOutput = [[AVAssetReaderTrackOutput alloc]
                              initWithTrack:videoTrack
                              outputSettings:nil];
        impl_->videoOutput.alwaysCopiesSampleData = NO;
        [impl_->reader addOutput:impl_->videoOutput];

        if (hasAudio_) {
            NSArray* audioTracks = [impl_->asset tracksWithMediaType:AVMediaTypeAudio];
            if (audioTracks.count > 0) {
                NSDictionary* audioSettings = @{
                    AVFormatIDKey: @(kAudioFormatLinearPCM),
                    AVLinearPCMBitDepthKey: @32,
                    AVLinearPCMIsFloatKey: @YES,
                    AVLinearPCMIsNonInterleaved: @NO,
                    AVSampleRateKey: @48000,
                    AVNumberOfChannelsKey: @2
                };

                impl_->audioOutput = [[AVAssetReaderAudioMixOutput alloc]
                                      initWithAudioTracks:audioTracks
                                      audioSettings:audioSettings];
                [impl_->reader addOutput:impl_->audioOutput];
            }
        }

        CMTime startTime = CMTimeMakeWithSeconds(seconds, 600);
        CMTime endTime = impl_->asset.duration;
        impl_->reader.timeRange = CMTimeRangeMake(startTime, CMTimeSubtract(endTime, startTime));

        [impl_->reader startReading];

        // Reset audio buffers and PTS tracking
        {
            std::lock_guard<std::mutex> lock(audioMutex_);
            audioWritePos_ = 0;
            audioReadPos_ = 0;
            audioStartPTS_ = seconds;
            audioEndPTS_ = seconds;
        }

        if (audioPlayer_) {
            audioPlayer_->flush();
        }

        // Pre-fill audio buffer with ~200ms of audio to avoid underruns
        if (hasAudio_) {
            prebufferAudio();
        }

        currentTime_ = seconds;
        playbackTime_ = seconds;
        nextFrameTime_ = seconds;
        isFinished_ = false;
    }
}

} // namespace vivid::video
