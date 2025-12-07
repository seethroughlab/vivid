// AVFoundation Decoder - Uses macOS native video decoding
// Decodes to RGBA pixels and uploads to GPU texture

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <vivid/video/avf_decoder.h>
#include <vivid/video/audio_player.h>
#include <vivid/context.h>

#include <iostream>

// Helper to create WGPUStringView from C string
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

namespace vivid::video {

struct AVFDecoder::Impl {
    AVAsset* asset = nil;
    AVAssetReader* reader = nil;
    AVAssetReaderTrackOutput* videoOutput = nil;
    AVAssetReaderAudioMixOutput* audioOutput = nil;

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

AVFDecoder::AVFDecoder() : impl_(std::make_unique<Impl>()) {}

AVFDecoder::~AVFDecoder() {
    close();
}

void AVFDecoder::createTexture() {
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
    desc.label = toStringView("AVFVideoFrame");
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    texture_ = wgpuDeviceCreateTexture(device_, &desc);
    if (!texture_) {
        std::cerr << "[AVFDecoder] Failed to create texture" << std::endl;
        return;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = toStringView("AVFVideoFrameView");
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
}

bool AVFDecoder::open(Context& ctx, const std::string& path, bool loop) {
    close();

    device_ = ctx.device();
    queue_ = ctx.queue();
    filePath_ = path;
    isLooping_ = loop;

    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];

        // Use AVURLAsset with options for better format support
        NSDictionary* assetOptions = @{
            AVURLAssetPreferPreciseDurationAndTimingKey: @YES
        };
        impl_->asset = [AVURLAsset URLAssetWithURL:url options:assetOptions];

        if (!impl_->asset) {
            std::cerr << "[AVFDecoder] Failed to load asset: " << path << std::endl;
            return false;
        }

        // Synchronously load required keys
        dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
        [impl_->asset loadValuesAsynchronouslyForKeys:@[@"tracks", @"duration", @"playable"]
            completionHandler:^{
                dispatch_semaphore_signal(semaphore);
            }];

        dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC);
        if (dispatch_semaphore_wait(semaphore, timeout) != 0) {
            std::cerr << "[AVFDecoder] Timeout loading asset" << std::endl;
            return false;
        }

        // Get video track
        NSArray* videoTracks = [impl_->asset tracksWithMediaType:AVMediaTypeVideo];
        if (videoTracks.count == 0) {
            std::cerr << "[AVFDecoder] No video track found" << std::endl;
            return false;
        }

        AVAssetTrack* videoTrack = videoTracks[0];

        // Get video properties
        CGSize size = videoTrack.naturalSize;
        width_ = static_cast<int>(size.width);
        height_ = static_cast<int>(size.height);
        duration_ = CMTimeGetSeconds(impl_->asset.duration);
        frameRate_ = videoTrack.nominalFrameRate;
        if (frameRate_ <= 0) frameRate_ = 30.0f;

        // Log codec info
        CMFormatDescriptionRef formatDesc = (__bridge CMFormatDescriptionRef)[videoTrack.formatDescriptions firstObject];
        if (formatDesc) {
            FourCharCode codec = CMFormatDescriptionGetMediaSubType(formatDesc);
            char codecStr[5] = {0};
            codecStr[0] = (codec >> 24) & 0xFF;
            codecStr[1] = (codec >> 16) & 0xFF;
            codecStr[2] = (codec >> 8) & 0xFF;
            codecStr[3] = codec & 0xFF;
            std::cout << "[AVFDecoder] Codec: " << codecStr << std::endl;
        }

        // Create asset reader
        NSError* error = nil;
        impl_->reader = [[AVAssetReader alloc] initWithAsset:impl_->asset error:&error];
        if (error) {
            std::cerr << "[AVFDecoder] Failed to create reader: "
                      << error.localizedDescription.UTF8String << std::endl;
            return false;
        }

        // Configure video output to decode to BGRA
        NSDictionary* outputSettings = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
        };
        impl_->videoOutput = [[AVAssetReaderTrackOutput alloc]
                              initWithTrack:videoTrack
                              outputSettings:outputSettings];
        impl_->videoOutput.alwaysCopiesSampleData = NO;

        if ([impl_->reader canAddOutput:impl_->videoOutput]) {
            [impl_->reader addOutput:impl_->videoOutput];
        } else {
            std::cerr << "[AVFDecoder] Cannot add video output" << std::endl;
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
                    std::cout << "[AVFDecoder] Audio: 48000Hz, 2 ch" << std::endl;
                } else {
                    audioPlayer_.reset();
                }
            }
        }

        // Start reading
        if (![impl_->reader startReading]) {
            std::cerr << "[AVFDecoder] Failed to start reading: "
                      << impl_->reader.error.localizedDescription.UTF8String << std::endl;
            return false;
        }

        // Create GPU texture
        createTexture();
        if (!texture_) {
            return false;
        }

        // Allocate pixel buffer
        pixelBuffer_.resize(width_ * height_ * 4);

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

        std::cout << "[AVFDecoder] Opened " << path
                  << " (" << width_ << "x" << height_
                  << ", " << frameRate_ << "fps)" << std::endl;

        return true;
    }
}

void AVFDecoder::prebufferAudio() {
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

void AVFDecoder::close() {
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

bool AVFDecoder::isOpen() const {
    return impl_->reader != nil;
}

void AVFDecoder::pause() {
    isPlaying_ = false;
    if (audioPlayer_) {
        audioPlayer_->pause();
    }
}

void AVFDecoder::play() {
    if (!isFinished_) {
        isPlaying_ = true;
        if (audioPlayer_) {
            audioPlayer_->play();
        }
    }
}

void AVFDecoder::setVolume(float volume) {
    if (audioPlayer_) {
        audioPlayer_->setVolume(volume);
    }
}

float AVFDecoder::getVolume() const {
    return audioPlayer_ ? audioPlayer_->getVolume() : 1.0f;
}

void AVFDecoder::update(Context& ctx) {
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
        CMSampleBufferRef sampleBuffer = [impl_->videoOutput copyNextSampleBuffer];

        if (!sampleBuffer) {
            std::cout << "[AVFDecoder] EOF at " << currentTime_ << "s" << std::endl;
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

        // Get presentation time
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        currentTime_ = CMTimeGetSeconds(pts);

        // Get pixel buffer
        CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
        if (imageBuffer) {
            CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

            size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
            void* baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);

            if (baseAddress) {
                // Copy pixels and convert BGRA -> RGBA
                uint8_t* src = static_cast<uint8_t*>(baseAddress);
                uint8_t* dst = pixelBuffer_.data();
                size_t expectedStride = width_ * 4;

                for (int y = 0; y < height_; y++) {
                    for (int x = 0; x < width_; x++) {
                        int srcIdx = x * 4;
                        int dstIdx = x * 4;
                        // BGRA -> RGBA: swap B and R
                        dst[dstIdx + 0] = src[srcIdx + 2];  // R <- B
                        dst[dstIdx + 1] = src[srcIdx + 1];  // G <- G
                        dst[dstIdx + 2] = src[srcIdx + 0];  // B <- R
                        dst[dstIdx + 3] = src[srcIdx + 3];  // A <- A
                    }
                    src += bytesPerRow;
                    dst += expectedStride;
                }

                // Upload to GPU
                WGPUTexelCopyTextureInfo destination = {};
                destination.texture = texture_;
                destination.mipLevel = 0;
                destination.origin = {0, 0, 0};
                destination.aspect = WGPUTextureAspect_All;

                WGPUTexelCopyBufferLayout dataLayout = {};
                dataLayout.offset = 0;
                dataLayout.bytesPerRow = width_ * 4;
                dataLayout.rowsPerImage = height_;

                WGPUExtent3D writeSize = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};

                wgpuQueueWriteTexture(queue_, &destination, pixelBuffer_.data(), pixelBuffer_.size(),
                                      &dataLayout, &writeSize);
            }

            CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        }

        CFRelease(sampleBuffer);

        // Schedule next frame
        float frameDuration = 1.0f / frameRate_;
        nextFrameTime_ = playbackTime_ + frameDuration;
    }
}

void AVFDecoder::seek(float seconds) {
    if (!impl_->asset) return;

    if (seconds < 0) seconds = 0;
    if (seconds > duration_) seconds = duration_;

    @autoreleasepool {
        impl_->cleanup();

        NSError* error = nil;
        impl_->reader = [[AVAssetReader alloc] initWithAsset:impl_->asset error:&error];
        if (error) {
            std::cerr << "[AVFDecoder] Failed to recreate reader for seek" << std::endl;
            return;
        }

        NSArray* videoTracks = [impl_->asset tracksWithMediaType:AVMediaTypeVideo];
        AVAssetTrack* videoTrack = videoTracks[0];

        NSDictionary* outputSettings = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
        };
        impl_->videoOutput = [[AVAssetReaderTrackOutput alloc]
                              initWithTrack:videoTrack
                              outputSettings:outputSettings];
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
