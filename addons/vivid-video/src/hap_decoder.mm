// HAP Decoder - Uses Vidvox HAP library for efficient DXT texture decoding
// Uploads compressed textures directly to GPU without CPU pixel conversion

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

#include "hap_decoder.h"
#include "hap.h"
#include "audio_player.h"
#include "vivid/context.h"

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Texture.h"
#include "RefCntAutoPtr.hpp"

#include <iostream>

namespace vivid::video {

using namespace Diligent;

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
        // 'Hap1' = HAP, 'Hap5' = HAP Alpha, 'HapY' = HAP Q
        // 'HapM' = HAP Q Alpha, 'HapA' = HAP Alpha-Only
        return (codec == 'Hap1' || codec == 'Hap5' || codec == 'HapY' ||
                codec == 'HapM' || codec == 'HapA');
    }
}

bool HAPDecoder::open(Context& ctx, const std::string& path, bool loop) {
    close();

    device_ = ctx.device();
    context_ = ctx.immediateContext();
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
        NSDictionary* videoSettings = @{
            // No pixel buffer attributes = pass through compressed data
        };
        impl_->videoOutput = [[AVAssetReaderTrackOutput alloc]
                              initWithTrack:videoTrack
                              outputSettings:nil];  // nil = pass through
        // alwaysCopiesSampleData = YES ensures data is copied to accessible buffer
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
            // Configure audio output for PCM float
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

                // Initialize audio player
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
        // Note: HAP files may have empty sync samples at the beginning that we need to skip
        CMSampleBufferRef sampleBuffer = nil;
        CMBlockBufferRef blockBuffer = nil;
        size_t dataLength = 0;
        std::vector<char> frameBuffer;
        char* dataPtr = nullptr;

        const int maxAttempts = 10;  // Don't loop forever
        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            sampleBuffer = [impl_->videoOutput copyNextSampleBuffer];
            if (!sampleBuffer) {
                std::cerr << "[HAPDecoder] Failed to read sample (attempt " << attempt << ")" << std::endl;
                close();
                return false;
            }

            // Check if this sample has actual data
            CMItemCount numSamples = CMSampleBufferGetNumSamples(sampleBuffer);
            blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);

            if (numSamples > 0 && blockBuffer) {
                dataLength = CMBlockBufferGetDataLength(blockBuffer);
                if (dataLength > 0) {
                    // Found a valid frame with data
                    break;
                }
            }

            // Empty sample (possibly sync/header marker), skip it
            CFRelease(sampleBuffer);
            sampleBuffer = nil;
            blockBuffer = nil;
        }

        if (!sampleBuffer || !blockBuffer) {
            std::cerr << "[HAPDecoder] No valid frames found after " << maxAttempts << " attempts" << std::endl;
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
            std::cerr << "[HAPDecoder] Invalid HAP frame (size=" << dataLength << ")" << std::endl;
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

        // Determine Diligent texture format from HAP format
        TEXTURE_FORMAT texFormat = TEX_FORMAT_UNKNOWN;
        size_t bytesPerBlock = 0;

        switch (impl_->hapTextureFormat) {
            case HapTextureFormat_RGB_DXT1:
                texFormat = TEX_FORMAT_BC1_UNORM;
                bytesPerBlock = 8;  // 4x4 pixels = 8 bytes
                std::cout << "[HAPDecoder] Format: HAP (DXT1/BC1)" << std::endl;
                break;
            case HapTextureFormat_RGBA_DXT5:
            case HapTextureFormat_YCoCg_DXT5:
                texFormat = TEX_FORMAT_BC3_UNORM;
                bytesPerBlock = 16;  // 4x4 pixels = 16 bytes
                std::cout << "[HAPDecoder] Format: HAP Alpha/Q (DXT5/BC3)" << std::endl;
                break;
            case HapTextureFormat_A_RGTC1:
                texFormat = TEX_FORMAT_BC4_UNORM;
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

        // Create compressed texture
        TextureDesc desc;
        desc.Name = "HAPVideoFrame";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = width_;
        desc.Height = height_;
        desc.MipLevels = 1;
        desc.Format = texFormat;
        desc.BindFlags = BIND_SHADER_RESOURCE;
        desc.Usage = USAGE_DEFAULT;

        // Initial data
        TextureSubResData subResData;
        subResData.pData = dxtBuffer_.data();
        subResData.Stride = blocksX * bytesPerBlock;

        TextureData initData;
        initData.NumSubresources = 1;
        initData.pSubResources = &subResData;

        RefCntAutoPtr<ITexture> tex;
        device_->CreateTexture(desc, &initData, &tex);

        if (!tex) {
            std::cerr << "[HAPDecoder] Failed to create texture" << std::endl;
            close();
            return false;
        }

        texture_ = tex.Detach();
        srv_ = texture_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

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
        const uint32_t targetFrames = 48000 / 2;  // ~0.5 seconds

        while (audioPlayer_->getBufferedFrames() < targetFrames) {
            CMSampleBufferRef sampleBuffer = [impl_->audioOutput copyNextSampleBuffer];
            if (!sampleBuffer) break;

            CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
            if (blockBuffer) {
                size_t dataLength = 0;
                char* dataPtr = nullptr;
                CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &dataLength, &dataPtr);

                // Data is interleaved float stereo
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

    if (texture_) {
        texture_->Release();
        texture_ = nullptr;
    }
    srv_ = nullptr;

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
    playbackTime_ += ctx.dt();

    // Keep audio buffer fed
    @autoreleasepool {
        if (audioPlayer_ && impl_->audioOutput) {
            const uint32_t minBufferFrames = 48000 / 4;  // ~0.25 seconds
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
        // Read next valid sample (skip empty sync samples)
        CMSampleBufferRef sampleBuffer = nil;
        CMBlockBufferRef blockBuffer = nil;
        size_t dataLength = 0;

        const int maxAttempts = 5;
        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            sampleBuffer = [impl_->videoOutput copyNextSampleBuffer];
            if (!sampleBuffer) {
                // EOF
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

            // Check if this sample has actual data
            CMItemCount numSamples = CMSampleBufferGetNumSamples(sampleBuffer);
            blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);

            if (numSamples > 0 && blockBuffer) {
                dataLength = CMBlockBufferGetDataLength(blockBuffer);
                if (dataLength > 0) {
                    break;  // Found valid frame
                }
            }

            // Empty sample, skip
            CFRelease(sampleBuffer);
            sampleBuffer = nil;
            blockBuffer = nil;
        }

        if (!sampleBuffer || !blockBuffer || dataLength == 0) {
            return;  // No valid frame found
        }

        // Get presentation time
        CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        currentTime_ = CMTimeGetSeconds(pts);

        // Ensure we have contiguous data - copy if necessary
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

            Box region;
            region.MinX = 0;
            region.MaxX = width_;
            region.MinY = 0;
            region.MaxY = height_;

            TextureSubResData subResData;
            subResData.pData = dxtBuffer_.data();
            subResData.Stride = blocksX * bytesPerBlock;

            context_->UpdateTexture(texture_, 0, 0, region, subResData,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                                   RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }

        // Schedule next frame
        float frameDuration = 1.0f / frameRate_;
        nextFrameTime_ = playbackTime_ + frameDuration;
    }
}

void HAPDecoder::seek(float seconds) {
    if (!impl_->asset) return;

    // Clamp seek time
    if (seconds < 0) seconds = 0;
    if (seconds > duration_) seconds = duration_;

    @autoreleasepool {
        // Need to recreate reader for seeking
        impl_->cleanup();

        NSError* error = nil;
        impl_->reader = [[AVAssetReader alloc] initWithAsset:impl_->asset error:&error];
        if (error) {
            std::cerr << "[HAPDecoder] Failed to recreate reader for seek" << std::endl;
            return;
        }

        // Get video track
        NSArray* videoTracks = [impl_->asset tracksWithMediaType:AVMediaTypeVideo];
        AVAssetTrack* videoTrack = videoTracks[0];

        // Setup video output
        impl_->videoOutput = [[AVAssetReaderTrackOutput alloc]
                              initWithTrack:videoTrack
                              outputSettings:nil];
        impl_->videoOutput.alwaysCopiesSampleData = NO;
        [impl_->reader addOutput:impl_->videoOutput];

        // Setup audio output if we have audio
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

        // Set time range
        CMTime startTime = CMTimeMakeWithSeconds(seconds, 600);
        CMTime endTime = impl_->asset.duration;
        impl_->reader.timeRange = CMTimeRangeMake(startTime, CMTimeSubtract(endTime, startTime));

        // Start reading
        [impl_->reader startReading];

        // Flush audio
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

ITexture* HAPDecoder::texture() const {
    return texture_;
}

ITextureView* HAPDecoder::textureView() const {
    return srv_;
}

} // namespace vivid::video
