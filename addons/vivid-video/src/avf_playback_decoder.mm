/**
 * @file avf_playback_decoder.mm
 * @brief AVFoundation playback decoder using AVPlayer + AVPlayerLooper
 *
 * Uses Apple's recommended approach for synchronized A/V playback:
 * - AVQueuePlayer for playback
 * - AVPlayerLooper for seamless looping
 * - AVPlayerItemVideoOutput for frame extraction
 *
 * Audio plays through system speakers via AVPlayer's default audio routing.
 */

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>

#include <vivid/video/avf_playback_decoder.h>
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

struct AVFPlaybackDecoder::Impl {
    AVQueuePlayer* player = nil;
    AVPlayerLooper* looper = nil;
    AVPlayerItem* playerItem = nil;
    AVPlayerItemVideoOutput* videoOutput = nil;

    AVAsset* asset = nil;
    bool isLooping = false;
    bool isPlaying = false;
    float volume = 1.0f;

    // KVO observer for status
    id statusObserver = nil;
    bool isReady = false;

    void cleanup() {
        // Remove notification observer for looping
        if (playerItem) {
            [[NSNotificationCenter defaultCenter] removeObserver:playerItem];
        }

        if (statusObserver) {
            [player removeTimeObserver:statusObserver];
            statusObserver = nil;
        }

        // Disable looper before cleaning up player
        if (looper) {
            [looper disableLooping];
            looper = nil;
        }

        if (player) {
            [player pause];
            [player removeAllItems];
            player = nil;
        }

        playerItem = nil;
        videoOutput = nil;
        asset = nil;
        isReady = false;
        isPlaying = false;
    }
};

AVFPlaybackDecoder::AVFPlaybackDecoder() : impl_(std::make_unique<Impl>()) {}

AVFPlaybackDecoder::~AVFPlaybackDecoder() {
    close();
}

bool AVFPlaybackDecoder::open(Context& ctx, const std::string& path, bool loop) {
    close();

    device_ = ctx.device();
    queue_ = ctx.queue();

    @autoreleasepool {
        // Create asset
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        impl_->asset = [AVAsset assetWithURL:url];

        if (!impl_->asset) {
            std::cerr << "[AVFPlaybackDecoder] Failed to create asset: " << path << std::endl;
            return false;
        }

        // Get video track info synchronously (for dimensions/framerate)
        // Note: In production, should use async loading, but sync is simpler here
        NSArray* videoTracks = [impl_->asset tracksWithMediaType:AVMediaTypeVideo];
        if (videoTracks.count == 0) {
            std::cerr << "[AVFPlaybackDecoder] No video track found" << std::endl;
            close();
            return false;
        }

        AVAssetTrack* videoTrack = videoTracks[0];
        CGSize size = videoTrack.naturalSize;
        width_ = static_cast<int>(size.width);
        height_ = static_cast<int>(size.height);
        frameRate_ = videoTrack.nominalFrameRate;
        if (frameRate_ <= 0) frameRate_ = 30.0f;
        duration_ = CMTimeGetSeconds(impl_->asset.duration);

        // Check for audio
        NSArray* audioTracks = [impl_->asset tracksWithMediaType:AVMediaTypeAudio];
        hasAudio_ = (audioTracks.count > 0);

        // Create player item with video output attached BEFORE creating looper
        // This way, when AVPlayerLooper copies the template, the copies should include the output
        impl_->playerItem = [AVPlayerItem playerItemWithAsset:impl_->asset];

        NSDictionary* outputSettings = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferMetalCompatibilityKey: @YES
        };
        impl_->videoOutput = [[AVPlayerItemVideoOutput alloc]
                               initWithPixelBufferAttributes:outputSettings];
        impl_->videoOutput.suppressesPlayerRendering = NO;
        [impl_->playerItem addOutput:impl_->videoOutput];

        // Create queue player
        impl_->player = [[AVQueuePlayer alloc] init];
        impl_->player.actionAtItemEnd = AVPlayerActionAtItemEndNone;
        impl_->player.volume = impl_->volume;

        impl_->isLooping = loop;

        // Insert item directly - we handle looping manually via notifications
        // AVPlayerLooper creates copies that don't inherit video output
        [impl_->player insertItem:impl_->playerItem afterItem:nil];

        // For looping: register for end notification to seek back to start
        if (loop) {
            [[NSNotificationCenter defaultCenter]
                addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                object:impl_->playerItem
                queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
                    // Seek back to start for seamless looping
                    [impl_->player seekToTime:kCMTimeZero toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
                    [impl_->player play];
                }];
        }

        // Wait for ready to play - process runloop to allow AVFoundation to load
        bool ready = false;
        NSError* failureError = nil;

        for (int i = 0; i < 300; i++) {  // 3 seconds max
            // Process the runloop to allow AVFoundation async loading
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);

            // Check current item status (looper creates its own items)
            AVPlayerItem* currentItem = impl_->player.currentItem;
            AVPlayerItemStatus itemStatus = currentItem ? currentItem.status : AVPlayerItemStatusUnknown;

            if (itemStatus == AVPlayerItemStatusReadyToPlay) {
                ready = true;
                break;
            } else if (itemStatus == AVPlayerItemStatusFailed) {
                failureError = currentItem.error;
                break;
            }
        }

        if (!ready) {
            if (failureError) {
                std::cerr << "[AVFPlaybackDecoder] Player failed: "
                          << failureError.localizedDescription.UTF8String << std::endl;
            } else {
                std::cerr << "[AVFPlaybackDecoder] Timeout waiting for player ready (status="
                          << (impl_->player.currentItem ? impl_->player.currentItem.status : -1) << ")" << std::endl;
            }
            close();
            return false;
        }

        impl_->isReady = true;

        // Create GPU texture
        createTexture();
        if (!texture_) {
            close();
            return false;
        }

        // Allocate pixel buffer for frame transfer
        pixelBuffer_.resize(width_ * height_ * 4);

        std::cout << "[AVFPlaybackDecoder] Opened " << path
                  << " (" << width_ << "x" << height_
                  << ", " << frameRate_ << "fps"
                  << ", audio: " << (hasAudio_ ? "yes" : "no")
                  << ", loop: " << (loop ? "yes" : "no") << ")" << std::endl;

        // Auto-play
        impl_->isPlaying = true;
        [impl_->player play];

        return true;
    }
}

void AVFPlaybackDecoder::close() {
    @autoreleasepool {
        impl_->cleanup();
    }

    if (textureView_) {
        wgpuTextureViewRelease(textureView_);
        textureView_ = nullptr;
    }
    if (texture_) {
        wgpuTextureDestroy(texture_);
        wgpuTextureRelease(texture_);
        texture_ = nullptr;
    }

    pixelBuffer_.clear();
    width_ = 0;
    height_ = 0;
    duration_ = 0;
    hasAudio_ = false;
}

bool AVFPlaybackDecoder::isOpen() const {
    return impl_->isReady && impl_->player != nil;
}

void AVFPlaybackDecoder::createTexture() {
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
    desc.label = toStringView("AVFPlaybackFrame");
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};
    desc.format = WGPUTextureFormat_BGRA8Unorm;  // Match CVPixelBuffer format
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    texture_ = wgpuDeviceCreateTexture(device_, &desc);
    if (!texture_) {
        std::cerr << "[AVFPlaybackDecoder] Failed to create texture" << std::endl;
        return;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.label = toStringView("AVFPlaybackFrameView");
    viewDesc.format = WGPUTextureFormat_BGRA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    textureView_ = wgpuTextureCreateView(texture_, &viewDesc);
}

void AVFPlaybackDecoder::uploadFrame(const uint8_t* pixels, size_t bytesPerRow) {
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = texture_;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = static_cast<uint32_t>(bytesPerRow);
    dataLayout.rowsPerImage = static_cast<uint32_t>(height_);

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_), 1};

    wgpuQueueWriteTexture(queue_, &destination, pixels,
                          bytesPerRow * height_, &dataLayout, &writeSize);
}

void AVFPlaybackDecoder::update(Context& ctx) {
    if (!isOpen()) {
        return;
    }

    @autoreleasepool {
        // Ensure player is actually playing
        if (impl_->isPlaying && impl_->player.rate == 0) {
            [impl_->player play];
        }

        // Get current item (should be our playerItem with the video output)
        AVPlayerItem* currentItem = impl_->player.currentItem;
        if (!currentItem) {
            return;
        }

        // Get current playback time
        CMTime currentTime = currentItem.currentTime;

        // Use the stored video output (attached to our playerItem)
        AVPlayerItemVideoOutput* output = impl_->videoOutput;
        if (!output) {
            return;
        }

        // Get pixel buffer for current time
        CVPixelBufferRef pixelBuffer = [output copyPixelBufferForItemTime:currentTime
                                                       itemTimeForDisplay:NULL];

        if (!pixelBuffer) {
            // This can happen briefly during seeks or loop transitions
            return;
        }

        // Lock the pixel buffer for CPU access
        CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

        // Get pixel data
        uint8_t* baseAddress = (uint8_t*)CVPixelBufferGetBaseAddress(pixelBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);

        // Upload to GPU texture
        uploadFrame(baseAddress, bytesPerRow);

        // Unlock and release
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(pixelBuffer);
    }
}

void AVFPlaybackDecoder::seek(float seconds) {
    if (!isOpen()) return;

    @autoreleasepool {
        CMTime time = CMTimeMakeWithSeconds(seconds, 600);
        [impl_->player seekToTime:time toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
    }
}

void AVFPlaybackDecoder::play() {
    if (!isOpen()) return;

    impl_->isPlaying = true;
    [impl_->player play];
}

void AVFPlaybackDecoder::pause() {
    if (!isOpen()) return;

    impl_->isPlaying = false;
    [impl_->player pause];
}

bool AVFPlaybackDecoder::isPlaying() const {
    return impl_->isPlaying;
}

bool AVFPlaybackDecoder::isFinished() const {
    if (!isOpen() || impl_->isLooping) {
        return false;
    }

    // Check if playback reached the end
    CMTime current = impl_->player.currentTime;
    CMTime duration = impl_->playerItem.duration;

    return CMTimeCompare(current, duration) >= 0;
}

float AVFPlaybackDecoder::currentTime() const {
    if (!isOpen()) return 0.0f;

    return CMTimeGetSeconds(impl_->player.currentTime);
}

float AVFPlaybackDecoder::duration() const {
    return duration_;
}

void AVFPlaybackDecoder::setVolume(float volume) {
    impl_->volume = std::max(0.0f, std::min(1.0f, volume));
    if (impl_->player) {
        impl_->player.volume = impl_->volume;
    }
}

float AVFPlaybackDecoder::getVolume() const {
    return impl_->volume;
}

} // namespace vivid::video
