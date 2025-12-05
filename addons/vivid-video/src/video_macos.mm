// Vivid Video Addon - macOS Implementation
// Uses AVFoundation for hardware-accelerated video decoding

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include "vivid/video/video.h"
#include "vivid/context.h"

// Diligent Engine includes
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Texture.h"
#include "RefCntAutoPtr.hpp"

#include <iostream>

namespace vivid::video {

using namespace Diligent;

struct VideoPlayer::Impl {
    // AVFoundation objects
    AVAsset* asset = nil;
    AVAssetReader* reader = nil;
    AVAssetReaderTrackOutput* videoOutput = nil;

    // Video properties
    int videoWidth = 0;
    int videoHeight = 0;
    float videoDuration = 0.0f;
    float videoFrameRate = 30.0f;

    // Playback state
    bool isPlaying_ = false;
    bool isFinished_ = false;
    bool isLooping = false;
    float currentTime_ = 0.0f;
    double lastUpdateTime = 0.0;
    std::string filePath;

    // GPU texture
    RefCntAutoPtr<ITexture> texture;
    ITextureView* srv = nullptr;

    // Staging buffer for pixel upload
    std::vector<uint8_t> pixelBuffer;

    // Diligent references
    IRenderDevice* device = nullptr;
    IDeviceContext* context = nullptr;

    ~Impl() {
        close();
    }

    void close() {
        if (reader) {
            [reader cancelReading];
            reader = nil;
        }
        videoOutput = nil;
        asset = nil;
        texture.Release();
        srv = nullptr;
        isPlaying_ = false;
        isFinished_ = true;
        currentTime_ = 0.0f;
    }

    bool open(Context& ctx, const std::string& path, bool loop) {
        close();

        device = ctx.device();
        context = ctx.immediateContext();
        filePath = path;
        isLooping = loop;

        @autoreleasepool {
            // Create asset from file
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
            asset = [AVAsset assetWithURL:url];

            if (!asset) {
                std::cerr << "[VideoPlayer] Failed to load asset: " << path << std::endl;
                return false;
            }

            // Get video track
            NSArray* videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
            if ([videoTracks count] == 0) {
                std::cerr << "[VideoPlayer] No video track found: " << path << std::endl;
                return false;
            }

            AVAssetTrack* videoTrack = [videoTracks objectAtIndex:0];

            // Get video properties
            CGSize naturalSize = videoTrack.naturalSize;
            videoWidth = (int)naturalSize.width;
            videoHeight = (int)naturalSize.height;
            videoDuration = (float)CMTimeGetSeconds(asset.duration);
            videoFrameRate = videoTrack.nominalFrameRate;

            if (videoFrameRate <= 0) {
                videoFrameRate = 30.0f;
            }

            std::cout << "[VideoPlayer] Opened: " << path << std::endl;
            std::cout << "[VideoPlayer] Size: " << videoWidth << "x" << videoHeight << std::endl;
            std::cout << "[VideoPlayer] Duration: " << videoDuration << "s @ " << videoFrameRate << " fps" << std::endl;

            // Create asset reader
            if (!createReader()) {
                return false;
            }

            // Create GPU texture
            if (!createTexture()) {
                return false;
            }

            // Allocate staging buffer
            pixelBuffer.resize(videoWidth * videoHeight * 4);

            isPlaying_ = true;
            isFinished_ = false;
            currentTime_ = 0.0f;
            lastUpdateTime = 0.0;

            return true;
        }
    }

    bool createReader() {
        @autoreleasepool {
            NSError* error = nil;
            reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];

            if (error || !reader) {
                std::cerr << "[VideoPlayer] Failed to create reader: "
                          << (error ? [[error localizedDescription] UTF8String] : "unknown") << std::endl;
                return false;
            }

            // Get video track
            NSArray* videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
            AVAssetTrack* videoTrack = [videoTracks objectAtIndex:0];

            // Configure output format - BGRA for easy texture upload
            NSDictionary* outputSettings = @{
                (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
            };

            videoOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack
                                                           outputSettings:outputSettings];
            videoOutput.alwaysCopiesSampleData = NO;

            [reader addOutput:videoOutput];

            if (![reader startReading]) {
                std::cerr << "[VideoPlayer] Failed to start reading" << std::endl;
                return false;
            }

            return true;
        }
    }

    bool createTexture() {
        TextureDesc desc;
        desc.Name = "VideoFrame";
        desc.Type = RESOURCE_DIM_TEX_2D;
        desc.Width = videoWidth;
        desc.Height = videoHeight;
        desc.MipLevels = 1;
        desc.Format = TEX_FORMAT_BGRA8_UNORM;  // Match AVFoundation output
        desc.BindFlags = BIND_SHADER_RESOURCE;
        desc.Usage = USAGE_DEFAULT;

        device->CreateTexture(desc, nullptr, &texture);

        if (!texture) {
            std::cerr << "[VideoPlayer] Failed to create texture" << std::endl;
            return false;
        }

        srv = texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
        return true;
    }

    void update(Context& ctx) {
        if (!isPlaying_ || isFinished_ || !reader) {
            return;
        }

        @autoreleasepool {
            // Check reader status
            if (reader.status == AVAssetReaderStatusFailed) {
                std::cerr << "[VideoPlayer] Reader failed" << std::endl;
                isFinished_ = true;
                isPlaying_ = false;
                return;
            }

            if (reader.status == AVAssetReaderStatusCompleted) {
                if (isLooping) {
                    // Restart from beginning
                    seek(0.0f);
                    return;
                } else {
                    isFinished_ = true;
                    isPlaying_ = false;
                    return;
                }
            }

            // Get next sample buffer
            CMSampleBufferRef sampleBuffer = [videoOutput copyNextSampleBuffer];
            if (!sampleBuffer) {
                // No more frames
                if (isLooping) {
                    seek(0.0f);
                } else {
                    isFinished_ = true;
                    isPlaying_ = false;
                }
                return;
            }

            // Get presentation time
            CMTime pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
            currentTime_ = (float)CMTimeGetSeconds(pts);

            // Get pixel buffer
            CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
            if (imageBuffer) {
                uploadFrame(imageBuffer);
            }

            CFRelease(sampleBuffer);
        }
    }

    void uploadFrame(CVImageBufferRef imageBuffer) {
        CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

        void* baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
        size_t height = CVPixelBufferGetHeight(imageBuffer);
        size_t width = CVPixelBufferGetWidth(imageBuffer);

        // Copy to staging buffer (handle potential row padding)
        uint8_t* dst = pixelBuffer.data();
        uint8_t* src = (uint8_t*)baseAddress;
        size_t dstRowBytes = width * 4;

        for (size_t y = 0; y < height; y++) {
            memcpy(dst + y * dstRowBytes, src + y * bytesPerRow, dstRowBytes);
        }

        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);

        // Upload to GPU texture
        Box region;
        region.MinX = 0;
        region.MaxX = (Uint32)width;
        region.MinY = 0;
        region.MaxY = (Uint32)height;

        TextureSubResData subResData;
        subResData.pData = pixelBuffer.data();
        subResData.Stride = (Uint32)dstRowBytes;

        context->UpdateTexture(texture, 0, 0, region, subResData,
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                              RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    void seek(float seconds) {
        if (!asset) return;

        @autoreleasepool {
            // Need to recreate the reader for seeking
            if (reader) {
                [reader cancelReading];
                reader = nil;
                videoOutput = nil;
            }

            // Clamp seek time
            if (seconds < 0) seconds = 0;
            if (seconds > videoDuration) seconds = videoDuration;

            currentTime_ = seconds;

            // Create new reader starting from seek position
            NSError* error = nil;
            reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];

            if (error || !reader) {
                std::cerr << "[VideoPlayer] Failed to create reader for seek" << std::endl;
                return;
            }

            // Set time range from seek position to end
            CMTime startTime = CMTimeMakeWithSeconds(seconds, 600);
            CMTime duration = CMTimeSubtract(asset.duration, startTime);
            reader.timeRange = CMTimeRangeMake(startTime, duration);

            // Get video track
            NSArray* videoTracks = [asset tracksWithMediaType:AVMediaTypeVideo];
            AVAssetTrack* videoTrack = [videoTracks objectAtIndex:0];

            NSDictionary* outputSettings = @{
                (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
            };

            videoOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:videoTrack
                                                           outputSettings:outputSettings];
            videoOutput.alwaysCopiesSampleData = NO;

            [reader addOutput:videoOutput];

            if (![reader startReading]) {
                std::cerr << "[VideoPlayer] Failed to start reading after seek" << std::endl;
                return;
            }

            isFinished_ = false;
        }
    }
};

// VideoPlayer implementation

VideoPlayer::VideoPlayer() : impl_(std::make_unique<Impl>()) {}

VideoPlayer::~VideoPlayer() = default;

VideoPlayer::VideoPlayer(VideoPlayer&& other) noexcept = default;
VideoPlayer& VideoPlayer::operator=(VideoPlayer&& other) noexcept = default;

bool VideoPlayer::open(Context& ctx, const std::string& path, bool loop) {
    return impl_->open(ctx, path, loop);
}

void VideoPlayer::close() {
    impl_->close();
}

void VideoPlayer::update(Context& ctx) {
    impl_->update(ctx);
}

void VideoPlayer::seek(float seconds) {
    impl_->seek(seconds);
}

void VideoPlayer::pause() {
    impl_->isPlaying_ = false;
}

void VideoPlayer::play() {
    if (!impl_->isFinished_) {
        impl_->isPlaying_ = true;
    }
}

bool VideoPlayer::isPlaying() const {
    return impl_->isPlaying_;
}

bool VideoPlayer::isFinished() const {
    return impl_->isFinished_;
}

bool VideoPlayer::isOpen() const {
    return impl_->asset != nil;
}

float VideoPlayer::currentTime() const {
    return impl_->currentTime_;
}

float VideoPlayer::duration() const {
    return impl_->videoDuration;
}

int VideoPlayer::width() const {
    return impl_->videoWidth;
}

int VideoPlayer::height() const {
    return impl_->videoHeight;
}

float VideoPlayer::frameRate() const {
    return impl_->videoFrameRate;
}

Diligent::ITexture* VideoPlayer::texture() const {
    return impl_->texture;
}

Diligent::ITextureView* VideoPlayer::textureView() const {
    return impl_->srv;
}

// VideoSource implementation

VideoSource::VideoSource() = default;
VideoSource::~VideoSource() = default;

bool VideoSource::open(Context& ctx, const std::string& path, bool loop) {
    return player_.open(ctx, path, loop);
}

void VideoSource::close() {
    player_.close();
}

void VideoSource::process(Context& ctx) {
    player_.update(ctx);
}

Diligent::ITextureView* VideoSource::getOutputSRV() const {
    return player_.textureView();
}

VideoPlayer& VideoSource::player() {
    return player_;
}

const VideoPlayer& VideoSource::player() const {
    return player_;
}

bool VideoSource::isOpen() const {
    return player_.isOpen();
}

} // namespace vivid::video
