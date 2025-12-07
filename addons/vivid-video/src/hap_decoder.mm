// HAP Decoder - Uses Vidvox HAP library for efficient DXT texture decoding
// Uploads compressed textures directly to GPU without CPU pixel conversion

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

#include <vivid/video/hap_decoder.h>
#include <vivid/video/audio_player.h>
#include <vivid/context.h>
#include "hap.h"

#include <iostream>

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

    // HAP texture format (DXT1/DXT5/etc)
    unsigned int hapTextureFormat = 0;

    void cleanup() {
        if (reader) {
            [reader cancelReading];
            reader = nil;
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
    desc.label = "HAPVideoFrame";
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
    viewDesc.label = "HAPVideoFrameView";
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
        WGPUImageCopyTexture destination = {};
        destination.texture = texture_;
        destination.mipLevel = 0;
        destination.origin = {0, 0, 0};
        destination.aspect = WGPUTextureAspect_All;

        WGPUTextureDataLayout dataLayout = {};
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

        // Pre-buffer and start audio
        if (audioPlayer_) {
            prebufferAudio();
            audioPlayer_->play();
        }

        std::cout << "[HAPDecoder] Opened " << path
                  << " (" << width_ << "x" << height_
                  << ", " << frameRate_ << "fps)" << std::endl;

        return true;
    }
}

void HAPDecoder::prebufferAudio() {
    if (!audioPlayer_ || !impl_->audioOutput) return;

    @autoreleasepool {
        const uint32_t targetFrames = 48000 / 2;

        while (audioPlayer_->getBufferedFrames() < targetFrames) {
            CMSampleBufferRef sampleBuffer = [impl_->audioOutput copyNextSampleBuffer];
            if (!sampleBuffer) break;

            CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
            if (blockBuffer) {
                size_t dataLength = 0;
                char* dataPtr = nullptr;
                CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &dataLength, &dataPtr);

                size_t frameCount = dataLength / (2 * sizeof(float));
                audioPlayer_->pushSamples(reinterpret_cast<float*>(dataPtr), frameCount);
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

void HAPDecoder::update(Context& ctx) {
    if (!isPlaying_ || isFinished_ || !impl_->reader) {
        return;
    }

    // Advance playback time
    playbackTime_ += static_cast<float>(ctx.dt());

    // Keep audio buffer fed
    @autoreleasepool {
        if (audioPlayer_ && impl_->audioOutput) {
            const uint32_t minBufferFrames = 48000 / 4;
            while (audioPlayer_->getBufferedFrames() < minBufferFrames) {
                CMSampleBufferRef sampleBuffer = [impl_->audioOutput copyNextSampleBuffer];
                if (!sampleBuffer) break;

                CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
                if (blockBuffer) {
                    size_t dataLength = 0;
                    char* dataPtr = nullptr;
                    CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &dataLength, &dataPtr);

                    size_t frameCount = dataLength / (2 * sizeof(float));
                    audioPlayer_->pushSamples(reinterpret_cast<float*>(dataPtr), frameCount);
                }

                CFRelease(sampleBuffer);
            }
        }
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
                std::cout << "[HAPDecoder] EOF at " << currentTime_ << "s" << std::endl;
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

            WGPUImageCopyTexture destination = {};
            destination.texture = texture_;
            destination.mipLevel = 0;
            destination.origin = {0, 0, 0};
            destination.aspect = WGPUTextureAspect_All;

            WGPUTextureDataLayout dataLayout = {};
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
        impl_->cleanup();

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

        if (audioPlayer_) {
            audioPlayer_->flush();
            prebufferAudio();
        }

        currentTime_ = seconds;
        playbackTime_ = seconds;
        nextFrameTime_ = seconds;
        isFinished_ = false;
    }
}

} // namespace vivid::video
