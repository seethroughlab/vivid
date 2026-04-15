#import "avf_decoder.h"
#include "decoded_frame_queue.h"
#include <chrono>

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreGraphics/CoreGraphics.h>
#import <IOSurface/IOSurface.h>
#import <QuartzCore/QuartzCore.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <atomic>
#include <memory>
#include <cassert>
#include <algorithm>

// =============================================================================
// dispatch_to_main_with_timeout — bounded alternative to dispatch_sync
// =============================================================================

/// Dispatch a block to the main queue with a bounded timeout.
/// Returns true if the block executed, false on timeout.
/// If already on the main thread, executes inline.
static bool dispatch_to_main_with_timeout(void (^block)(void), double timeout_seconds) {
    if ([NSThread isMainThread]) {
        block();
        return true;
    }
    auto cancelled = std::make_shared<std::atomic<bool>>(false);
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!cancelled->load(std::memory_order_acquire)) {
            block();
        }
        dispatch_semaphore_signal(sem);
    });
    long result = dispatch_semaphore_wait(sem,
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout_seconds * NSEC_PER_SEC)));
    if (result != 0) {
        cancelled->store(true, std::memory_order_release);
        return false;
    }
    return true;
}

// =============================================================================
// AVFDecoder::Impl — Objective-C++ internals
// =============================================================================

struct AVFDecoder::Impl {
    AVAsset*                 asset        = nil;
    AVQueuePlayer*           player       = nil;
    AVPlayerItem*            player_item  = nil;
    AVPlayerLooper*          player_looper = nil;
    AVPlayerItemVideoOutput* video_output = nil;
    std::string              source_path;

    std::vector<uint8_t> pixel_buffer;
    uint32_t frame_width  = 0;
    uint32_t frame_height = 0;
    float    media_duration = 0.0f;
    float    frame_rate_    = 30.0f;
    bool     is_looping    = true;
    bool     opened        = false;
    float    current_speed_ = 1.0f;
    uint64_t no_frame_counter_ = 0;
    uint64_t manual_loop_seek_count_ = 0;

    NSDictionary* pixel_buffer_attrs() const {
        return @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey:
                @(kCVPixelFormatType_32BGRA),
            (NSString*)kCVPixelBufferMetalCompatibilityKey:
                @YES,
            (NSString*)kCVPixelBufferIOSurfacePropertiesKey:
                @{}
        };
    }

    AVPlayerItemVideoOutput* make_video_output() const {
        return [[AVPlayerItemVideoOutput alloc]
            initWithPixelBufferAttributes:pixel_buffer_attrs()];
    }

    AVPlayerItemVideoOutput* ensure_video_output_for_item(AVPlayerItem* item) {
        if (!item) return nil;
        for (AVPlayerItemOutput* out in item.outputs) {
            if ([out isKindOfClass:[AVPlayerItemVideoOutput class]]) {
                video_output = (AVPlayerItemVideoOutput*)out;
                return video_output;
            }
        }

        AVPlayerItemVideoOutput* fresh = make_video_output();
        [item addOutput:fresh];
        video_output = fresh;
        return video_output;
    }

    AVPlayerItemVideoOutput* ensure_video_output_for_current_item() {
        AVPlayerItem* current_item = player.currentItem ? player.currentItem : player_item;
        return ensure_video_output_for_item(current_item);
    }

    void ensure_video_outputs_for_queued_items() {
        if (!player) return;
        for (AVPlayerItem* item in player.items) {
            ensure_video_output_for_item(item);
        }
    }

    AVPlayerItem* make_template_item() const {
        if (!asset) return nil;
        return [AVPlayerItem playerItemWithAsset:asset];
    }

    CMTime current_queue_time() const {
        AVPlayerItem* item = player.currentItem ? player.currentItem : player_item;
        return item ? item.currentTime : kCMTimeZero;
    }

    void rebuild_queue_preserving_time(CMTime preserve_time) {
        if (!player || !asset) return;

        const bool should_play = current_speed_ > 0.0f;
        [player pause];
        player_looper = nil;
        video_output = nil;
        [player removeAllItems];

        player_item = make_template_item();
        if (!player_item) return;

        if (is_looping) {
            player.actionAtItemEnd = AVPlayerActionAtItemEndNone;
            player_looper = [AVPlayerLooper playerLooperWithPlayer:player
                                                       templateItem:player_item];
        } else {
            player.actionAtItemEnd = AVPlayerActionAtItemEndPause;
            if ([player canInsertItem:player_item afterItem:nil]) {
                [player insertItem:player_item afterItem:nil];
            }
        }

        ensure_video_output_for_current_item();
        ensure_video_outputs_for_queued_items();
        if (CMTIME_IS_VALID(preserve_time) &&
            !CMTIME_IS_INDEFINITE(preserve_time) &&
            CMTimeCompare(preserve_time, kCMTimeZero) > 0) {
            [player seekToTime:preserve_time
               toleranceBefore:kCMTimeZero
                toleranceAfter:kCMTimeZero];
        }
        if (should_play) {
            [player play];
            player.rate = current_speed_;
        }
    }

    bool open_main_thread(NSString* path_ns) {
        @autoreleasepool {
            NSURL* url = [NSURL fileURLWithPath:path_ns];
            if (!url) return false;

            asset = [AVAsset assetWithURL:url];
            source_path = path_ns.UTF8String ?: "";

            // Synchronously load essential properties
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            __block bool ready = false;

            [asset loadValuesAsynchronouslyForKeys:@[@"tracks", @"duration"]
                completionHandler:^{
                    NSError* error = nil;
                    AVKeyValueStatus status = [asset
                        statusOfValueForKey:@"tracks" error:&error];
                    ready = (status == AVKeyValueStatusLoaded);
                    dispatch_semaphore_signal(sem);
                }];

            dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW,
                (int64_t)(5.0 * NSEC_PER_SEC)));

            if (!ready) {
                std::fprintf(stderr, "[avf_decoder] Asset not ready\n");
                return false;
            }

            // Get video dimensions from first video track
            NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:
                AVMediaTypeVideo];
            if (tracks.count == 0) {
                std::fprintf(stderr, "[avf_decoder] No video tracks\n");
                return false;
            }

            CGSize size = tracks[0].naturalSize;
            CGAffineTransform transform = tracks[0].preferredTransform;
            CGSize transformed = CGSizeApplyAffineTransform(size, transform);
            frame_width  = static_cast<uint32_t>(std::abs(transformed.width));
            frame_height = static_cast<uint32_t>(std::abs(transformed.height));

            if (frame_width == 0 || frame_height == 0) {
                frame_width  = static_cast<uint32_t>(size.width);
                frame_height = static_cast<uint32_t>(size.height);
            }

            frame_rate_ = tracks[0].nominalFrameRate > 0.0f ? tracks[0].nominalFrameRate : 30.0f;

            CMTime dur = asset.duration;
            media_duration = static_cast<float>(CMTimeGetSeconds(dur));

            player = [AVQueuePlayer queuePlayerWithItems:@[]];

            // Mute audio — we only want video frames
            player.volume = 0.0f;

            rebuild_queue_preserving_time(kCMTimeZero);

            // Do not hard-fail on AVPlayerItemStatusUnknown during open.
            // AVFoundation can remain in Unknown briefly after attaching output;
            // decode_frame() will naturally yield no frame until ready.
            AVPlayerItem* current_item = player.currentItem ? player.currentItem : player_item;
            if (current_item.status == AVPlayerItemStatusFailed) {
                std::fprintf(stderr, "[avf_decoder] Player item failed during open: %s\n",
                             current_item.error.localizedDescription.UTF8String ?: "<no error>");
                close();
                return false;
            }

            pixel_buffer.resize(frame_width * frame_height * 4);
            opened = true;
            return true;
        }
    }

    bool open(const std::string& path) {
        NSString* path_ns = [NSString stringWithUTF8String:path.c_str()];
        if (!path_ns) return false;
        __block bool ok = false;
        if (!dispatch_to_main_with_timeout(^{ ok = open_main_thread(path_ns); }, 5.0)) {
            std::fprintf(stderr, "[avf_decoder] open() timed out — main queue not "
                                 "responding (headless environment?)\n");
            return false;
        }
        return ok;
    }

    void close() {
        if (![NSThread isMainThread]) {
            if (dispatch_to_main_with_timeout(^{ close(); }, 3.0)) {
                return;  // Main thread handled cleanup.
            }
            std::fprintf(stderr, "[avf_decoder] close() timed out — forcing local cleanup\n");
            // Fall through to cleanup on this thread. ARC will release objects.
        }
        @autoreleasepool {
            if (player) {
                [player pause];
                [player removeAllItems];
            }
            player_looper = nil;
            video_output = nil;
            player_item = nil;
            player = nil;
            asset = nil;
            source_path.clear();
            pixel_buffer.clear();
            frame_width = frame_height = 0;
            media_duration = 0.0f;
            opened = false;
        }
    }

    // ==========================================================================
    // Clock ownership: SELF-CLOCK mode
    // ==========================================================================
    // AVPlayer owns the playback clock.  We never seek from within
    // decode_frame(); target-time selection is driven externally by MovieFile.
    //
    // Frame selection: AVPlayerItemVideoOutput translates the host monotonic
    // clock (CACurrentMediaTime) to the item timeline via itemTimeForHostTime:.
    // The returned display_time is the frame that should be on screen NOW.
    //
    // hasNewPixelBufferForItemTime: returns YES only when display_time maps
    // to a different decoded frame than the last one returned.  This prevents
    // redundant CPU copies when the render loop runs faster than the media
    // frame rate (e.g. 60 Hz loop with 24 fps content).
    //
    // acquire_pixel_buffer_impl() — Phase 1 (main thread, fast):
    //   AVF API calls only, returns a retained CVPixelBuffer.
    //
    // copy_pixel_buffer() — Phase 2 (any thread, slow):
    //   Lock, row-by-row memcpy, unlock, release.
    // ==========================================================================

    AcquiredPixelBuffer acquire_pixel_buffer_impl() {
        assert([NSThread isMainThread]);
        @autoreleasepool {
            AcquiredPixelBuffer result;
            if (!opened || !player) {
                result.status = DecodeStatus::NilFrame;
                return result;
            }

            // Recover from transient paused/stalled state (seen after seeks/loops).
            if (current_speed_ > 0.0f &&
                player.timeControlStatus != AVPlayerTimeControlStatusPlaying) {
                [player play];
                player.rate = current_speed_;
            }

            AVPlayerItem* current_item = player.currentItem ? player.currentItem : player_item;
            if (!current_item) { result.status = DecodeStatus::NilFrame; return result; }
            AVPlayerItemVideoOutput* active_output = ensure_video_output_for_current_item();
            if (!active_output) { result.status = DecodeStatus::NilFrame; return result; }
            CMTime display_time = [active_output itemTimeForHostTime:CACurrentMediaTime()];
            if (!CMTIME_IS_VALID(display_time) || CMTIME_IS_INDEFINITE(display_time)) {
                display_time = current_item.currentTime;
            }

            if (![active_output hasNewPixelBufferForItemTime:display_time]) {
                result.status = DecodeStatus::ReusedFrame;
                return result;
            }

            CVPixelBufferRef cv_buf = [active_output
                copyPixelBufferForItemTime:display_time
                itemTimeForDisplay:nil];
            if (!cv_buf) {
                CMTime now_time = current_item.currentTime;
                cv_buf = [active_output copyPixelBufferForItemTime:now_time itemTimeForDisplay:nil];
            }
            if (!cv_buf) {
                no_frame_counter_++;
                if ((no_frame_counter_ % 240) == 0) {
                    std::fprintf(stderr, "[avf_decoder] no pixel buffer for ~%llu decode ticks\n",
                                 static_cast<unsigned long long>(no_frame_counter_));
                }
                result.status = DecodeStatus::NilFrame;
                return result;
            }
            no_frame_counter_ = 0;

            result.buffer = cv_buf;  // retained by copyPixelBufferForItemTime:
            result.pts = CMTimeGetSeconds(display_time);
            result.status = DecodeStatus::NewFrame;
            return result;
        }
    }

    AcquiredPixelBuffer acquire_pixel_buffer_at_time_impl(double time_seconds) {
        assert([NSThread isMainThread]);
        @autoreleasepool {
            AcquiredPixelBuffer result;
            if (!opened || !player) {
                result.status = DecodeStatus::NilFrame;
                return result;
            }

            AVPlayerItem* current_item = player.currentItem ? player.currentItem : player_item;
            if (!current_item) {
                result.status = DecodeStatus::NilFrame;
                return result;
            }

            ensure_video_output_for_current_item();
            ensure_video_outputs_for_queued_items();
            if (!video_output) {
                result.status = DecodeStatus::NilFrame;
                return result;
            }

            const double clamped = std::max(0.0, time_seconds);
            CMTime requested = CMTimeMakeWithSeconds(clamped, 600);
            CVPixelBufferRef cv_buf = nullptr;
            for (AVPlayerItem* item in player.items) {
                AVPlayerItemVideoOutput* output = ensure_video_output_for_item(item);
                if (!output) continue;
                cv_buf = [output copyPixelBufferForItemTime:requested itemTimeForDisplay:nil];
                if (cv_buf) break;
            }
            if (!cv_buf && current_item) {
                AVPlayerItemVideoOutput* output = ensure_video_output_for_item(current_item);
                if (output) {
                    cv_buf = [output copyPixelBufferForItemTime:requested itemTimeForDisplay:nil];
                }
            }
            if (!cv_buf) {
                result.status = DecodeStatus::ReusedFrame;
                return result;
            }

            result.buffer = cv_buf;
            result.pts = clamped;
            result.status = DecodeStatus::NewFrame;
            return result;
        }
    }

    // Synchronous decode: acquire + copy in one call (backward compatible).
    DecodeStatus decode_frame() {
        auto acquired = acquire_pixel_buffer_impl();
        if (!acquired.valid()) return acquired.status;

        auto frame = AVFDecoder::copy_pixel_buffer(std::move(acquired));
        if (frame.empty()) return DecodeStatus::NilFrame;

        // Update Impl state for backward-compatible pixel_data() access.
        if (frame.width != frame_width || frame.height != frame_height) {
            frame_width = frame.width;
            frame_height = frame.height;
        }
        pixel_buffer = std::move(frame.data);
        return DecodeStatus::NewFrame;
    }

    void set_loop(bool loop) {
        assert([NSThread isMainThread]);
        @autoreleasepool {
            if (!opened || !player) {
                is_looping = loop;
                return;
            }
            if (is_looping == loop) return;
            CMTime preserve_time = current_queue_time();
            is_looping = loop;
            rebuild_queue_preserving_time(preserve_time);
        }
    }

    void set_speed(float speed) {
        assert([NSThread isMainThread]);
        @autoreleasepool {
            if (!player || !opened) return;
            current_speed_ = speed;
            if (speed > 0.0f) {
                [player play];
                player.rate = speed;
            } else {
                [player pause];
            }
        }
    }

    void play() {
        assert([NSThread isMainThread]);
        @autoreleasepool {
            if (player && opened) {
                [player play];
            }
        }
    }

    void pause() {
        assert([NSThread isMainThread]);
        @autoreleasepool {
            if (player && opened) {
                [player pause];
            }
        }
    }

    float current_time() const {
        assert([NSThread isMainThread]);
        @autoreleasepool {
            if (player && opened) {
                AVPlayerItem* item = player.currentItem ? player.currentItem : player_item;
                return item ? static_cast<float>(CMTimeGetSeconds(item.currentTime)) : 0.0f;
            }
            return 0.0f;
        }
    }

    bool seek(double time_seconds) {
        assert([NSThread isMainThread]);
        if (!opened || !player) return false;
        const double t = std::max(0.0, time_seconds);
        @autoreleasepool {
            CMTime seek_time = CMTimeMakeWithSeconds(t, 600);
            [player seekToTime:seek_time
                toleranceBefore:kCMTimeZero
                 toleranceAfter:kCMTimeZero];
            if (current_speed_ > 0.0f) {
                [player play];
                player.rate = current_speed_;
            }
        }
        return true;
    }

    bool native_looping_enabled() const {
        return opened && is_looping && player_looper != nil;
    }

    uint64_t manual_loop_seek_count() const {
        return manual_loop_seek_count_;
    }

    static DecodedFrame copy_frame_at_time_for_asset(AVAsset* source_asset, double time_seconds) {
        @autoreleasepool {
            DecodedFrame frame;
            if (!source_asset) return frame;

            auto t0 = std::chrono::steady_clock::now();
            AVAssetImageGenerator* generator =
                [[AVAssetImageGenerator alloc] initWithAsset:source_asset];
            generator.appliesPreferredTrackTransform = YES;
            generator.requestedTimeToleranceBefore = kCMTimeZero;
            generator.requestedTimeToleranceAfter = kCMTimeZero;

            CMTime requested = CMTimeMakeWithSeconds(std::max(0.0, time_seconds), 600);
            CMTime actual = kCMTimeInvalid;
            NSError* error = nil;
            CGImageRef image = [generator copyCGImageAtTime:requested
                                                 actualTime:&actual
                                                      error:&error];
            if (!image) return frame;

            const size_t width = CGImageGetWidth(image);
            const size_t height = CGImageGetHeight(image);
            frame.width = static_cast<uint32_t>(width);
            frame.height = static_cast<uint32_t>(height);
            frame.pts = CMTIME_IS_VALID(actual) ? CMTimeGetSeconds(actual) : time_seconds;
            frame.requested_pts = time_seconds;
            frame.data.resize(width * height * 4);

            CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
            CGContextRef bitmap = CGBitmapContextCreate(frame.data.data(),
                                                        width,
                                                        height,
                                                        8,
                                                        width * 4,
                                                        color_space,
                                                        kCGBitmapByteOrder32Little |
                                                            kCGImageAlphaPremultipliedFirst);
            if (bitmap) {
                CGContextDrawImage(bitmap, CGRectMake(0, 0, width, height), image);
                CGContextRelease(bitmap);
            } else {
                frame.clear();
            }
            CGColorSpaceRelease(color_space);
            CGImageRelease(image);

            auto t1 = std::chrono::steady_clock::now();
            frame.copy_time_us =
                std::chrono::duration<float, std::micro>(t1 - t0).count();
            return frame;
        }
    }

    DecodedFrame copy_frame_at_time(double time_seconds) const {
        return copy_frame_at_time_for_asset(asset, time_seconds);
    }
};

// =============================================================================
// AVFDecoder public interface
// =============================================================================

AVFDecoder::AVFDecoder() : impl_(std::make_unique<Impl>()) {}
AVFDecoder::~AVFDecoder() { close(); }

bool AVFDecoder::open(const std::string& path) { return impl_->open(path); }
void AVFDecoder::close() { if (impl_) impl_->close(); }
bool AVFDecoder::is_open() const { return impl_ && impl_->opened; }
DecodeStatus AVFDecoder::decode_frame() { return impl_->decode_frame(); }
AcquiredPixelBuffer AVFDecoder::acquire_pixel_buffer() { return impl_->acquire_pixel_buffer_impl(); }
AcquiredPixelBuffer AVFDecoder::acquire_pixel_buffer_at_time(double time_seconds) {
    return impl_->acquire_pixel_buffer_at_time_impl(time_seconds);
}

DecodedFrame AVFDecoder::copy_frame_at_time(double time_seconds) const {
    return impl_ ? impl_->copy_frame_at_time(time_seconds) : DecodedFrame{};
}

DecodedFrame AVFDecoder::copy_frame_at_time(const std::string& path, double time_seconds) {
    @autoreleasepool {
        NSString* path_ns = [NSString stringWithUTF8String:path.c_str()];
        if (!path_ns) return DecodedFrame{};
        NSURL* url = [NSURL fileURLWithPath:path_ns];
        if (!url) return DecodedFrame{};
        AVAsset* asset = [AVAsset assetWithURL:url];
        return Impl::copy_frame_at_time_for_asset(asset, time_seconds);
    }
}

DecodedFrame AVFDecoder::copy_pixel_buffer(AcquiredPixelBuffer&& acquired) {
    DecodedFrame frame;
    if (!acquired.buffer) return frame;

    auto t0 = std::chrono::steady_clock::now();

    CVPixelBufferLockBaseAddress(acquired.buffer, kCVPixelBufferLock_ReadOnly);

    uint32_t w = static_cast<uint32_t>(CVPixelBufferGetWidth(acquired.buffer));
    uint32_t h = static_cast<uint32_t>(CVPixelBufferGetHeight(acquired.buffer));
    size_t stride = CVPixelBufferGetBytesPerRow(acquired.buffer);
    const uint8_t* base = static_cast<const uint8_t*>(
        CVPixelBufferGetBaseAddress(acquired.buffer));

    frame.width = w;
    frame.height = h;
    frame.pts = acquired.pts;
    frame.data.resize(static_cast<size_t>(w) * h * 4);

    for (uint32_t row = 0; row < h; ++row) {
        std::memcpy(frame.data.data() + row * w * 4,
                    base + row * stride,
                    w * 4);
    }

    CVPixelBufferUnlockBaseAddress(acquired.buffer, kCVPixelBufferLock_ReadOnly);
    acquired.release();

    auto t1 = std::chrono::steady_clock::now();
    frame.copy_time_us = std::chrono::duration<float, std::micro>(t1 - t0).count();

    return frame;
}

DecodedFrame AVFDecoder::copy_pixel_buffer_ref(void* pixel_buffer, double pts) {
    if (!pixel_buffer) return DecodedFrame{};
    CVPixelBufferRef ref = static_cast<CVPixelBufferRef>(pixel_buffer);
    CVPixelBufferRetain(ref);
    AcquiredPixelBuffer acquired;
    acquired.buffer = ref;
    acquired.pts = pts;
    acquired.status = DecodeStatus::NewFrame;
    DecodedFrame frame = copy_pixel_buffer(std::move(acquired));
    frame.cpu_fallback = true;
    return frame;
}

DecodedFrame AVFDecoder::make_native_frame(AcquiredPixelBuffer&& acquired) {
    DecodedFrame frame;
    if (!acquired.buffer) return frame;
    CVPixelBufferRef ref = acquired.buffer;
    frame.width = static_cast<uint32_t>(CVPixelBufferGetWidth(ref));
    frame.height = static_cast<uint32_t>(CVPixelBufferGetHeight(ref));
    frame.pts = acquired.pts;
    frame.requested_pts = acquired.pts;
    frame.native_pixel_buffer = std::shared_ptr<void>(
        ref,
        [](void* p) {
            if (p) CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p));
        });
    acquired.buffer = nullptr;
    acquired.pts = 0.0;
    acquired.status = DecodeStatus::NilFrame;
    return frame;
}

AcquiredPixelBuffer::~AcquiredPixelBuffer() {
    release();
}

AcquiredPixelBuffer::AcquiredPixelBuffer(AcquiredPixelBuffer&& other) noexcept
    : buffer(other.buffer), pts(other.pts), status(other.status) {
    other.buffer = nullptr;
    other.pts = 0.0;
    other.status = DecodeStatus::NilFrame;
}

AcquiredPixelBuffer& AcquiredPixelBuffer::operator=(AcquiredPixelBuffer&& other) noexcept {
    if (this == &other) return *this;
    release();
    buffer = other.buffer;
    pts = other.pts;
    status = other.status;
    other.buffer = nullptr;
    other.pts = 0.0;
    other.status = DecodeStatus::NilFrame;
    return *this;
}

void AcquiredPixelBuffer::release() {
    if (buffer) {
        CVPixelBufferRelease(buffer);
        buffer = nullptr;
    }
}

const uint8_t* AVFDecoder::pixel_data() const { return impl_->pixel_buffer.data(); }
uint32_t AVFDecoder::width() const { return impl_->frame_width; }
uint32_t AVFDecoder::height() const { return impl_->frame_height; }
float AVFDecoder::duration() const { return impl_->media_duration; }
void AVFDecoder::set_loop(bool loop) { impl_->set_loop(loop); }
void AVFDecoder::set_speed(float speed) { impl_->set_speed(speed); }
float AVFDecoder::current_time() const { return impl_->current_time(); }
bool AVFDecoder::seek(double time_seconds) { return impl_->seek(time_seconds); }
float AVFDecoder::frame_rate() const { return impl_->frame_rate_; }
uint64_t AVFDecoder::nil_frame_count() const { return impl_->no_frame_counter_; }
bool AVFDecoder::native_looping_enabled() const { return impl_->native_looping_enabled(); }
uint64_t AVFDecoder::manual_loop_seek_count() const { return impl_->manual_loop_seek_count(); }

std::unique_ptr<VideoDecoder> create_avf_decoder() {
    return std::make_unique<AVFDecoder>();
}
