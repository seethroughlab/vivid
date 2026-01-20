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
#include <vivid/render_lock.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <deque>
#include <atomic>
#include <thread>
#include <chrono>

// Helper to load tracks synchronously using async API (avoids deprecation warning on macOS 15+)
// IMPORTANT: The completion handler is dispatched to a global queue to avoid deadlock
// when this function is called from the main thread (the semaphore wait would block
// the main thread, and if the completion handler needs the main thread, it deadlocks)
static NSArray* loadTracksWithMediaType(AVAsset* asset, AVMediaType mediaType) {
    __block NSArray* result = nil;
    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

    // Dispatch the async load to a background queue to ensure the completion handler
    // doesn't need to run on the main thread
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        [asset loadTracksWithMediaType:mediaType completionHandler:^(NSArray<AVAssetTrack*>* tracks, NSError* error) {
            if (!error && tracks) {
                // Copy to ensure the array survives the async callback
                result = [tracks copy];
            }
            dispatch_semaphore_signal(semaphore);
        }];
    });

    dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    return result;
}

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

    // Timestamped audio chunk for A/V sync
    struct AudioChunk {
        std::vector<float> samples;
        double pts;  // Presentation timestamp in seconds
    };
    std::deque<AudioChunk> audioChunks;  // Queue of timestamped audio chunks
    std::mutex audioMutex;
    double audioBufferHeadPTS = 0.0;  // PTS of next sample to be read (front of buffer)
    double audioBufferTailPTS = 0.0;  // PTS of last sample added (back of buffer)
    uint32_t sampleRate = 48000;
    uint32_t channels = 2;
    bool audioReaderExhausted = false;
    std::string filePath;  // Store for recreating reader on loop

    // Video time for A/V sync (atomic for thread-safe access from audio thread)
    std::atomic<double> lastVideoTimeSeconds{-1.0};  // -1 = not yet set (startup)
    std::atomic<bool> videoHasStarted{false};  // True after first video frame decoded
    std::atomic<bool> initialSyncDone{false};  // True after initial A/V alignment
    std::atomic<double> resyncToTime{-1.0};  // If >= 0, recreate audio reader at this time
    std::atomic<bool> isShuttingDown{false};  // True during cleanup - prevents audio thread access
    std::atomic<bool> isLoopTransition{false};  // True during loop seek - skip rendering

    void cleanup() {
        // Signal shutdown to prevent audio thread from accessing resources
        isShuttingDown.store(true);

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
            audioChunks.clear();
        }
        audioBufferHeadPTS = 0.0;
        audioBufferTailPTS = 0.0;
        lastVideoTimeSeconds.store(-1.0);
        videoHasStarted.store(false);
        initialSyncDone.store(false);
        resyncToTime.store(-1.0);
        // Note: isShuttingDown stays true until next open()

        playerItem = nil;
        videoOutput = nil;
        asset = nil;
        isReady = false;
        isPlaying = false;
        audioReaderExhausted = false;
    }

    bool setupAudioReader(AVAsset* asset, double startTimeSeconds = 0.0) {
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

        NSArray* audioTracks = loadTracksWithMediaType(asset, AVMediaTypeAudio);
        if (audioTracks.count == 0) {
            audioReader = nil;
            return false;
        }

        AVAssetTrack* audioTrack = audioTracks[0];

        // Get actual audio format from track (for channel count)
        NSArray* formatDescriptions = audioTrack.formatDescriptions;
        if (formatDescriptions.count > 0) {
            CMAudioFormatDescriptionRef formatDesc = (__bridge CMAudioFormatDescriptionRef)formatDescriptions[0];
            const AudioStreamBasicDescription* asbd = CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
            if (asbd) {
                channels = asbd->mChannelsPerFrame;
                if (channels > 2) channels = 2;  // Downmix to stereo max
            }
        }

        // IMPORTANT: Always output at 48000Hz to match AudioOutput
        // AVAssetReader will resample from the file's native rate (e.g., 44100Hz)
        // If we don't do this, there will be a sample rate mismatch causing drift
        sampleRate = 48000;

        // Output settings: PCM float, 48kHz stereo (matches AudioOutput)
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

        // Set time range if seeking to a specific position
        if (startTimeSeconds > 0.0) {
            CMTime startTime = CMTimeMakeWithSeconds(startTimeSeconds, 600);
            CMTime duration = asset.duration;
            CMTime remaining = CMTimeSubtract(duration, startTime);
            audioReader.timeRange = CMTimeRangeMake(startTime, remaining);
        }

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

        // Read audio samples and add to buffer with timestamps
        while (audioReader.status == AVAssetReaderStatusReading) {
            CMSampleBufferRef sampleBuffer = [audioOutput copyNextSampleBuffer];
            if (!sampleBuffer) {
                if (audioReader.status == AVAssetReaderStatusCompleted) {
                    audioReaderExhausted = true;
                }
                break;
            }

            // Extract presentation timestamp for A/V sync
            CMTime pts = CMSampleBufferGetOutputPresentationTimeStamp(sampleBuffer);
            double ptsSeconds = CMTimeGetSeconds(pts);

            CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
            if (blockBuffer) {
                size_t length = 0;
                char* dataPointer = nullptr;
                CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &length, &dataPointer);

                if (dataPointer && length > 0) {
                    size_t sampleCount = length / sizeof(float);
                    const float* samples = reinterpret_cast<const float*>(dataPointer);

                    // Create a new chunk with timestamp
                    AudioChunk chunk;
                    chunk.pts = ptsSeconds;
                    chunk.samples.reserve(sampleCount);
                    for (size_t i = 0; i < sampleCount; i++) {
                        chunk.samples.push_back(samples[i]);
                    }

                    {
                        std::lock_guard<std::mutex> lock(audioMutex);
                        // Update tail PTS (end of buffer)
                        double chunkDurationSec = static_cast<double>(sampleCount / channels) / sampleRate;
                        audioBufferTailPTS = ptsSeconds + chunkDurationSec;

                        // If this is the first chunk, set head PTS too
                        if (audioChunks.empty()) {
                            audioBufferHeadPTS = ptsSeconds;
                        }

                        audioChunks.push_back(std::move(chunk));
                    }
                }
            }

            CFRelease(sampleBuffer);

            // Don't read too far ahead (limit buffer to ~1 second)
            {
                std::lock_guard<std::mutex> lock(audioMutex);
                size_t totalSamples = 0;
                for (const auto& chunk : audioChunks) {
                    totalSamples += chunk.samples.size();
                }
                if (totalSamples > sampleRate * channels * 2) {
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
                audioChunks.clear();
            }
            audioBufferHeadPTS = 0.0;
            audioBufferTailPTS = 0.0;
            initialSyncDone.store(false);  // Re-sync on loop
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

    // Reset shutdown flag for new session
    impl_->isShuttingDown.store(false);

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
        NSArray* videoTracks = loadTracksWithMediaType(impl_->asset, AVMediaTypeVideo);
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
        NSArray* audioTracks = loadTracksWithMediaType(impl_->asset, AVMediaTypeAudio);
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

        // TODO: Use AVPlayerLooper for gapless video looping
        //
        // PROBLEM: The current notification-based looping (seek to start on end) causes a visible
        // gap/stutter when the video loops. AVPlayerLooper provides truly seamless, gapless looping
        // by pre-loading the next iteration.
        //
        // WHY IT DOESN'T WORK NOW: AVPlayerLooper creates internal copies of the AVPlayerItem,
        // and these copies don't inherit our AVPlayerItemVideoOutput. When the looper switches
        // to the copied item, copyPixelBufferForItemTime returns NULL because the output isn't
        // attached to the active item.
        //
        // PLAN TO FIX:
        // 1. Use AVPlayerLooper normally: looper = [AVPlayerLooper looperWithPlayer:player templateItem:playerItem]
        // 2. Instead of attaching output to the template item, observe AVPlayerLooper's loopingPlayerItems
        //    property and attach a fresh AVPlayerItemVideoOutput to EACH item in the array
        // 3. Keep track of which output corresponds to the current item (use player.currentItem)
        // 4. In update(), get the output for the current item and use that for copyPixelBufferForItemTime
        // 5. Handle audio reader recreation when looper switches items (similar to current resetAudioForLoop)
        //
        // REFERENCE: Apple's AVPlayerLooper documentation notes that you must add outputs to each
        // looping item individually, not just the template.
        //
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
                    // Lock rendering globally to prevent GPU state conflicts
                    vivid::RenderLock::instance().lock();
                    implPtr->isLoopTransition.store(true);

                    // Seek back to start with completion handler
                    [implPtr->player seekToTime:kCMTimeZero
                             toleranceBefore:kCMTimeZero
                              toleranceAfter:kCMTimeZero
                           completionHandler:^(BOOL finished) {
                        // Clear flags and unlock rendering after seek completes
                        implPtr->isLoopTransition.store(false);
                        vivid::RenderLock::instance().unlock();
                    }];
                    [implPtr->player play];
                    // Reset audio reader for loop
                    implPtr->resetAudioForLoop();
                }];
        }

        // Wait for ready to play
        // Use a short timeout - if AVPlayer isn't ready quickly, fall back to AVAssetReader
        // Note: CFRunLoopRunInMode causes freezes when embedded in Tauri's event loop
        bool ready = false;
        NSError* failureError = nil;

        for (int i = 0; i < 30; i++) {  // 300ms max - quick fallback to AVAssetReader
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Check current item status
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
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
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

bool AVFPlaybackDecoder::update(Context& ctx) {
    if (!isOpen()) {
        return false;
    }

    // Skip update during loop transition to avoid GPU state issues
    if (impl_->isLoopTransition.load()) {
        return false;
    }

    @autoreleasepool {
        // Ensure player is actually playing
        if (impl_->isPlaying && impl_->player.rate == 0) {
            [impl_->player play];
        }

        // Get current item (should be our playerItem with the video output)
        AVPlayerItem* currentItem = impl_->player.currentItem;
        if (!currentItem) {
            return false;
        }

        // Get current playback time and publish for A/V sync
        CMTime currentTime = currentItem.currentTime;
        double currentTimeSeconds = CMTimeGetSeconds(currentTime);
        impl_->lastVideoTimeSeconds.store(currentTimeSeconds);
        impl_->videoHasStarted.store(true);

        // Use the stored video output (attached to our playerItem)
        AVPlayerItemVideoOutput* output = impl_->videoOutput;
        if (!output) {
            return false;
        }

        // Get pixel buffer for current time
        CVPixelBufferRef pixelBuffer = [output copyPixelBufferForItemTime:currentTime
                                                       itemTimeForDisplay:NULL];

        if (!pixelBuffer) {
            // This can happen briefly during seeks or loop transitions
            return false;
        }

        // Lock the pixel buffer for CPU access
        CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);

        // Get pixel data
        uint8_t* baseAddress = (uint8_t*)CVPixelBufferGetBaseAddress(pixelBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);

        // Copy to CPU buffer for operators that need CPU pixel access (e.g., OpenCV)
        size_t expectedSize = static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4;
        if (pixelBuffer_.size() != expectedSize) {
            pixelBuffer_.resize(expectedSize);
        }
        // Copy row by row (bytesPerRow may include padding)
        for (int y = 0; y < height_; ++y) {
            memcpy(pixelBuffer_.data() + y * width_ * 4,
                   baseAddress + y * bytesPerRow,
                   width_ * 4);
        }

        // Upload to GPU texture
        uploadFrame(baseAddress, bytesPerRow);

        // Unlock and release
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(pixelBuffer);

        // Pre-fill audio buffer on main thread (AVFoundation is not thread-safe)
        // This ensures the audio thread can read from the buffer without calling AVFoundation
        if (hasAudio_) {
            // Check if we need to resync audio reader to a new position
            double resyncTime = impl_->resyncToTime.exchange(-1.0);
            if (resyncTime >= 0.0) {
                impl_->setupAudioReader(impl_->asset, resyncTime);
            }

            impl_->readMoreAudio();
        }

        return true;  // New frame was decoded
    }
}

void AVFPlaybackDecoder::seek(float seconds) {
    if (!isOpen()) return;

    @autoreleasepool {
        CMTime time = CMTimeMakeWithSeconds(seconds, 600);
        [impl_->player seekToTime:time toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];

        // Reset audio for seek - clear buffer and recreate reader at new position
        if (hasAudio_) {
            {
                std::lock_guard<std::mutex> lock(impl_->audioMutex);
                impl_->audioChunks.clear();
            }
            impl_->audioBufferHeadPTS = seconds;
            impl_->audioBufferTailPTS = seconds;
            impl_->initialSyncDone.store(false);  // Re-sync after seek
            impl_->setupAudioReader(impl_->asset, seconds);
        }
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
    // Safety check for shutdown - audio thread may call this during cleanup
    if (!impl_ || impl_->isShuttingDown.load()) {
        if (buffer) {
            for (uint32_t i = 0; i < maxFrames * audioChannels_; i++) {
                buffer[i] = 0.0f;
            }
        }
        return maxFrames;
    }

    if (!hasAudio_ || !buffer || maxFrames == 0) {
        return 0;
    }

    // NOTE: Don't call readMoreAudio() here - this function may be called from
    // the audio thread, and AVFoundation is not thread-safe. The audio buffer
    // is pre-filled by update() on the main thread.

    uint32_t samplesNeeded = maxFrames * audioChannels_;
    uint32_t samplesCopied = 0;

    // Get current video time for A/V sync
    double videoPTS = impl_->lastVideoTimeSeconds.load();
    bool videoStarted = impl_->videoHasStarted.load();
    bool initialSyncDone = impl_->initialSyncDone.load();

    {
        std::lock_guard<std::mutex> lock(impl_->audioMutex);

        // Skip sync correction until video has started playing
        // This prevents false sync errors during startup
        if (!videoStarted || videoPTS < 0.0) {
            // Output silence during startup - don't play audio until we can sync
            for (uint32_t i = 0; i < samplesNeeded; i++) {
                buffer[i] = 0.0f;
            }
            return maxFrames;
        }

        // Calculate sync error: positive = audio behind, negative = audio ahead
        double syncError = videoPTS - impl_->audioBufferHeadPTS;

        // Initial sync: when video first starts, align audio to video time
        if (!initialSyncDone) {
            // For large offsets (>1 second), we need to recreate the audio reader
            // at the correct position rather than trying to skip samples
            if (std::abs(syncError) > 1.0) {
                std::cout << "[AVFPlaybackDecoder] Initial A/V sync: resyncing audio to video time "
                          << videoPTS << "s (was " << (syncError * 1000) << "ms off)" << std::endl;
                // Clear current buffer and signal for resync
                impl_->audioChunks.clear();
                impl_->audioBufferHeadPTS = videoPTS;
                impl_->audioBufferTailPTS = videoPTS;
                impl_->initialSyncDone.store(true);
                impl_->resyncToTime.store(videoPTS);  // Signal update() to recreate reader

                // Return silence for this block - audio will resync on next update()
                for (uint32_t i = 0; i < samplesNeeded; i++) {
                    buffer[i] = 0.0f;
                }
                return maxFrames;
            }
            else if (syncError > 0.050) {
                // Audio is slightly behind video - discard audio to catch up
                double samplesToSkipSec = syncError;
                size_t samplesToSkip = static_cast<size_t>(samplesToSkipSec * impl_->sampleRate * impl_->channels);

                size_t skipped = 0;
                while (skipped < samplesToSkip && !impl_->audioChunks.empty()) {
                    auto& chunk = impl_->audioChunks.front();
                    size_t canSkip = std::min(chunk.samples.size(), samplesToSkip - skipped);
                    chunk.samples.erase(chunk.samples.begin(), chunk.samples.begin() + canSkip);
                    skipped += canSkip;
                    if (chunk.samples.empty()) {
                        impl_->audioChunks.pop_front();
                    }
                }
                if (!impl_->audioChunks.empty()) {
                    impl_->audioBufferHeadPTS = impl_->audioChunks.front().pts;
                }
            }
            else if (syncError < -0.050) {
                // Audio is slightly ahead of video - set head PTS to match video
                impl_->audioBufferHeadPTS = videoPTS;
            }

            impl_->initialSyncDone.store(true);
            syncError = videoPTS - impl_->audioBufferHeadPTS;  // Recalculate after adjustment
        }

        // Sync thresholds (in seconds)
        constexpr double SYNC_TOLERANCE = 0.100;      // ±100ms is imperceptible
        constexpr double SYNC_WARNING = 0.300;        // 300ms triggers gradual correction
        constexpr double SYNC_CRITICAL = 0.500;       // 500ms triggers aggressive correction

        // Handle sync correction
        if (syncError > SYNC_CRITICAL) {
            // Audio is significantly behind video - skip audio samples to catch up
            double samplesToSkipSec = syncError - SYNC_TOLERANCE;
            size_t samplesToSkip = static_cast<size_t>(samplesToSkipSec * impl_->sampleRate * impl_->channels);

            // Skip samples from the front of the buffer
            size_t skipped = 0;
            while (skipped < samplesToSkip && !impl_->audioChunks.empty()) {
                auto& chunk = impl_->audioChunks.front();
                size_t canSkip = std::min(chunk.samples.size(), samplesToSkip - skipped);
                chunk.samples.erase(chunk.samples.begin(), chunk.samples.begin() + canSkip);
                skipped += canSkip;

                if (chunk.samples.empty()) {
                    impl_->audioChunks.pop_front();
                }
            }

            // Update head PTS after skipping
            if (!impl_->audioChunks.empty()) {
                impl_->audioBufferHeadPTS = impl_->audioChunks.front().pts;
            }
        }
        else if (syncError < -SYNC_CRITICAL) {
            // Audio is significantly ahead of video - insert silence
            // Just return silence for this block; audio will naturally catch up
            for (uint32_t i = 0; i < samplesNeeded; i++) {
                buffer[i] = 0.0f;
            }
            return maxFrames;
        }
        // Note: Moderate drift (SYNC_WARNING) is tolerated without correction

        // Copy samples from chunks to output buffer
        while (samplesCopied < samplesNeeded && !impl_->audioChunks.empty()) {
            auto& chunk = impl_->audioChunks.front();

            while (samplesCopied < samplesNeeded && !chunk.samples.empty()) {
                buffer[samplesCopied++] = chunk.samples.front();
                chunk.samples.erase(chunk.samples.begin());
            }

            if (chunk.samples.empty()) {
                impl_->audioChunks.pop_front();
                // Update head PTS to the next chunk
                if (!impl_->audioChunks.empty()) {
                    impl_->audioBufferHeadPTS = impl_->audioChunks.front().pts;
                }
            }
        }

        // Update head PTS based on samples consumed
        double consumedSec = static_cast<double>(samplesCopied / impl_->channels) / impl_->sampleRate;
        impl_->audioBufferHeadPTS += consumedSec;
    }

    // Zero-fill any remaining samples (buffer underrun)
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
