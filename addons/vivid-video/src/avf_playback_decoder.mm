/**
 * @file avf_playback_decoder.mm
 * @brief AVFoundation playback decoder using AVPlayer + AVAssetReader
 *
 * Uses a hybrid approach for synchronized A/V playback:
 * - AVQueuePlayer for video timing and playback control
 * - AVPlayerItemVideoOutput for video frame extraction
 * - AVAssetReader for audio sample extraction (separate from AVPlayer audio)
 *
 * Audio is extracted via AVAssetReader and routed through the vivid audio chain,
 * rather than playing directly through AVPlayer's audio output.
 */

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>

#include <vivid/video/avf_playback_decoder.h>
#include <vivid/context.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <deque>

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

    // Audio extraction via AVAssetReader
    AVAssetReader* audioReader = nil;
    AVAssetReaderTrackOutput* audioOutput = nil;
    std::deque<float> audioBuffer;  // Ring buffer for extracted audio
    std::mutex audioMutex;
    double audioReadPosition = 0.0;  // Current read position in seconds
    uint32_t sampleRate = 48000;
    uint32_t channels = 2;
    bool audioReaderExhausted = false;
    std::string filePath;  // Store for recreating reader on loop

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

        // Clean up audio reader
        if (audioReader) {
            [audioReader cancelReading];
            audioReader = nil;
        }
        audioOutput = nil;
        {
            std::lock_guard<std::mutex> lock(audioMutex);
            audioBuffer.clear();
        }

        playerItem = nil;
        videoOutput = nil;
        asset = nil;
        isReady = false;
        isPlaying = false;
        audioReaderExhausted = false;
        audioReadPosition = 0.0;
    }

    bool setupAudioReader(AVAsset* asset) {
        if (audioReader) {
            [audioReader cancelReading];
            audioReader = nil;
        }
        audioOutput = nil;
        audioReaderExhausted = false;

        NSError* error = nil;
        audioReader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (error || !audioReader) {
            std::cerr << "[AVFPlaybackDecoder] Failed to create audio reader: "
                      << (error ? error.localizedDescription.UTF8String : "unknown") << std::endl;
            return false;
        }

        NSArray* audioTracks = [asset tracksWithMediaType:AVMediaTypeAudio];
        if (audioTracks.count == 0) {
            audioReader = nil;
            return false;
        }

        AVAssetTrack* audioTrack = audioTracks[0];

        // Get actual audio format from track
        NSArray* formatDescriptions = audioTrack.formatDescriptions;
        if (formatDescriptions.count > 0) {
            CMAudioFormatDescriptionRef formatDesc = (__bridge CMAudioFormatDescriptionRef)formatDescriptions[0];
            const AudioStreamBasicDescription* asbd = CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
            if (asbd) {
                sampleRate = static_cast<uint32_t>(asbd->mSampleRate);
                channels = asbd->mChannelsPerFrame;
                if (channels > 2) channels = 2;  // Downmix to stereo max
            }
        }

        // Output settings: PCM float, our target sample rate and channels
        NSDictionary* outputSettings = @{
            AVFormatIDKey: @(kAudioFormatLinearPCM),
            AVLinearPCMBitDepthKey: @32,
            AVLinearPCMIsFloatKey: @YES,
            AVLinearPCMIsNonInterleaved: @NO,
            AVLinearPCMIsBigEndianKey: @NO,
            AVSampleRateKey: @(sampleRate),
            AVNumberOfChannelsKey: @(channels)
        };

        audioOutput = [[AVAssetReaderTrackOutput alloc] initWithTrack:audioTrack
                                                       outputSettings:outputSettings];
        audioOutput.alwaysCopiesSampleData = NO;

        if (![audioReader canAddOutput:audioOutput]) {
            std::cerr << "[AVFPlaybackDecoder] Cannot add audio output to reader" << std::endl;
            audioReader = nil;
            audioOutput = nil;
            return false;
        }

        [audioReader addOutput:audioOutput];

        if (![audioReader startReading]) {
            std::cerr << "[AVFPlaybackDecoder] Failed to start audio reader: "
                      << audioReader.error.localizedDescription.UTF8String << std::endl;
            audioReader = nil;
            audioOutput = nil;
            return false;
        }

        return true;
    }

    void readMoreAudio() {
        if (!audioReader || !audioOutput || audioReaderExhausted) {
            return;
        }

        // Read audio samples and add to buffer
        while (audioReader.status == AVAssetReaderStatusReading) {
            CMSampleBufferRef sampleBuffer = [audioOutput copyNextSampleBuffer];
            if (!sampleBuffer) {
                if (audioReader.status == AVAssetReaderStatusCompleted) {
                    audioReaderExhausted = true;
                }
                break;
            }

            CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
            if (blockBuffer) {
                size_t length = 0;
                char* dataPointer = nullptr;
                CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &length, &dataPointer);

                if (dataPointer && length > 0) {
                    std::lock_guard<std::mutex> lock(audioMutex);
                    size_t sampleCount = length / sizeof(float);
                    const float* samples = reinterpret_cast<const float*>(dataPointer);
                    for (size_t i = 0; i < sampleCount; i++) {
                        audioBuffer.push_back(samples[i]);
                    }
                }
            }

            CFRelease(sampleBuffer);

            // Don't read too far ahead (limit buffer to ~1 second)
            {
                std::lock_guard<std::mutex> lock(audioMutex);
                if (audioBuffer.size() > sampleRate * channels * 2) {
                    break;
                }
            }
        }
    }

    void resetAudioForLoop() {
        // Recreate audio reader for looping
        if (!filePath.empty() && asset) {
            {
                std::lock_guard<std::mutex> lock(audioMutex);
                audioBuffer.clear();
            }
            audioReadPosition = 0.0;
            setupAudioReader(asset);
        }
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

        // Check for audio and set up extraction
        NSArray* audioTracks = [impl_->asset tracksWithMediaType:AVMediaTypeAudio];
        hasAudio_ = (audioTracks.count > 0);
        impl_->filePath = path;

        if (hasAudio_) {
            if (impl_->setupAudioReader(impl_->asset)) {
                audioSampleRate_ = impl_->sampleRate;
                audioChannels_ = impl_->channels;
                std::cout << "[AVFPlaybackDecoder] Audio: " << audioSampleRate_ << "Hz, "
                          << audioChannels_ << " channels" << std::endl;
            }
        }

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
        // Mute AVPlayer audio - we're extracting it via AVAssetReader and routing through our audio chain
        impl_->player.volume = 0.0f;

        impl_->isLooping = loop;

        // Insert item directly - we handle looping manually via notifications
        // AVPlayerLooper creates copies that don't inherit video output
        [impl_->player insertItem:impl_->playerItem afterItem:nil];

        // Capture impl for block
        auto* implPtr = impl_.get();

        // For looping: register for end notification to seek back to start
        if (loop) {
            [[NSNotificationCenter defaultCenter]
                addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                object:impl_->playerItem
                queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification* note) {
                    // Seek back to start for seamless looping
                    [implPtr->player seekToTime:kCMTimeZero toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
                    [implPtr->player play];
                    // Reset audio reader for loop
                    implPtr->resetAudioForLoop();
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
    // Only set AVPlayer volume if internal audio is enabled
    if (impl_->player && internalAudioEnabled_) {
        impl_->player.volume = impl_->volume;
    }
}

float AVFPlaybackDecoder::getVolume() const {
    return impl_->volume;
}

uint32_t AVFPlaybackDecoder::readAudioSamples(float* buffer, uint32_t maxFrames) {
    if (!hasAudio_ || !buffer || maxFrames == 0) {
        return 0;
    }

    // Read more audio from the asset reader if needed
    impl_->readMoreAudio();

    // Copy samples from our buffer
    uint32_t samplesNeeded = maxFrames * audioChannels_;
    uint32_t samplesCopied = 0;

    {
        std::lock_guard<std::mutex> lock(impl_->audioMutex);

        while (samplesCopied < samplesNeeded && !impl_->audioBuffer.empty()) {
            buffer[samplesCopied] = impl_->audioBuffer.front();
            impl_->audioBuffer.pop_front();
            samplesCopied++;
        }
    }

    // Zero-fill any remaining samples
    while (samplesCopied < samplesNeeded) {
        buffer[samplesCopied++] = 0.0f;
    }

    return maxFrames;
}

void AVFPlaybackDecoder::setInternalAudioEnabled(bool enable) {
    internalAudioEnabled_ = enable;
    if (impl_->player) {
        // When internal audio is disabled, mute AVPlayer
        // When enabled, restore to user-set volume
        impl_->player.volume = enable ? impl_->volume : 0.0f;
    }
}

bool AVFPlaybackDecoder::isInternalAudioEnabled() const {
    return internalAudioEnabled_;
}

} // namespace vivid::video
