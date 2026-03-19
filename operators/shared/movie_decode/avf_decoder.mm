#import "avf_decoder.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <atomic>
#include <memory>

// =============================================================================
// LoopObserver — restarts playback when the item reaches end
// =============================================================================

@interface LoopObserver : NSObject
@property (nonatomic, assign) BOOL shouldLoop;
@property (nonatomic, weak) AVPlayer* player;
@property (nonatomic, assign) float desiredRate;
@property (atomic, assign) BOOL loopFired;
@end

@implementation LoopObserver

- (instancetype)initWithPlayer:(AVPlayer*)player {
    self = [super init];
    if (self) {
        _player = player;
        _shouldLoop = YES;
        _desiredRate = 1.0f;
        _loopFired = NO;
        [[NSNotificationCenter defaultCenter]
            addObserver:self
            selector:@selector(playerDidFinish:)
            name:AVPlayerItemDidPlayToEndTimeNotification
            object:nil];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)playerDidFinish:(NSNotification*)note {
    if (!_shouldLoop || !_player) return;
    [_player seekToTime:kCMTimeZero completionHandler:^(BOOL finished) {
        if (finished && self->_shouldLoop) {
            self->_player.rate = self->_desiredRate;
            self.loopFired = YES;
        }
    }];
}

@end

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
    AVPlayer*                player       = nil;
    AVPlayerItem*            player_item  = nil;
    AVPlayerItemVideoOutput* video_output = nil;
    LoopObserver*            loop_observer = nil;

    std::vector<uint8_t> pixel_buffer;
    uint32_t frame_width  = 0;
    uint32_t frame_height = 0;
    float    media_duration = 0.0f;
    float    frame_rate_    = 30.0f;
    bool     is_looping    = true;
    bool     opened        = false;
    float    current_speed_ = 1.0f;
    uint64_t no_frame_counter_ = 0;

    bool open_main_thread(NSString* path_ns) {
        @autoreleasepool {
            NSURL* url = [NSURL fileURLWithPath:path_ns];
            if (!url) return false;

            AVAsset* asset = [AVAsset assetWithURL:url];

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

            // Create player item and attach video output
            player_item = [AVPlayerItem playerItemWithAsset:asset];

            NSDictionary* attrs = @{
                (NSString*)kCVPixelBufferPixelFormatTypeKey:
                    @(kCVPixelFormatType_32BGRA)
            };
            video_output = [[AVPlayerItemVideoOutput alloc]
                initWithPixelBufferAttributes:attrs];
            [player_item addOutput:video_output];

            // Create plain AVPlayer (not queue player)
            player = [AVPlayer playerWithPlayerItem:player_item];
            player.actionAtItemEnd = AVPlayerActionAtItemEndPause;

            // Mute audio — we only want video frames
            player.volume = 0.0f;

            // Set up looping via notification
            loop_observer = [[LoopObserver alloc] initWithPlayer:player];
            loop_observer.shouldLoop = is_looping;

            // Do not hard-fail on AVPlayerItemStatusUnknown during open.
            // AVFoundation can remain in Unknown briefly after attaching output;
            // decode_frame() will naturally yield no frame until ready.
            if (player_item.status == AVPlayerItemStatusFailed) {
                std::fprintf(stderr, "[avf_decoder] Player item failed during open: %s\n",
                             player_item.error.localizedDescription.UTF8String ?: "<no error>");
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
            }
            loop_observer = nil;
            video_output = nil;
            player_item = nil;
            player = nil;
            pixel_buffer.clear();
            frame_width = frame_height = 0;
            media_duration = 0.0f;
            opened = false;
        }
    }

    bool decode_frame() {
        @autoreleasepool {
            if (!opened || !video_output || !player) return false;

            // Recover from transient paused/stalled state (seen after seeks/loops).
            if (current_speed_ > 0.0f &&
                player.timeControlStatus != AVPlayerTimeControlStatusPlaying) {
                [player play];
                player.rate = current_speed_;
            }

            AVPlayerItem* current_item = player.currentItem ? player.currentItem : player_item;
            if (!current_item) return false;
            AVPlayerItemVideoOutput* active_output = video_output;
            if (current_item != player_item) {
                for (AVPlayerItemOutput* out in current_item.outputs) {
                    if ([out isKindOfClass:[AVPlayerItemVideoOutput class]]) {
                        active_output = (AVPlayerItemVideoOutput*)out;
                        break;
                    }
                }
            }
            if (!active_output) return false;
            CMTime display_time = [video_output itemTimeForHostTime:CACurrentMediaTime()];
            if (!CMTIME_IS_VALID(display_time) || CMTIME_IS_INDEFINITE(display_time)) {
                display_time = current_item.currentTime;
            }

            CVPixelBufferRef cv_buf = [active_output
                copyPixelBufferForItemTime:display_time
                itemTimeForDisplay:nil];
            if (!cv_buf) {
                // Fallback path for some containers/codecs where host-time lookup can miss.
                CMTime now_time = current_item.currentTime;
                cv_buf = [active_output copyPixelBufferForItemTime:now_time itemTimeForDisplay:nil];
            }
            if (!cv_buf) {
                no_frame_counter_++;
                if ((no_frame_counter_ % 240) == 0) {
                    std::fprintf(stderr, "[avf_decoder] no pixel buffer for ~%llu decode ticks\n",
                                 static_cast<unsigned long long>(no_frame_counter_));
                }
                return false;
            }
            no_frame_counter_ = 0;

            CVPixelBufferLockBaseAddress(cv_buf, kCVPixelBufferLock_ReadOnly);

            uint32_t w = static_cast<uint32_t>(CVPixelBufferGetWidth(cv_buf));
            uint32_t h = static_cast<uint32_t>(CVPixelBufferGetHeight(cv_buf));
            size_t stride = CVPixelBufferGetBytesPerRow(cv_buf);
            const uint8_t* base = static_cast<const uint8_t*>(
                CVPixelBufferGetBaseAddress(cv_buf));

            // Update dimensions if they differ (rare but possible)
            if (w != frame_width || h != frame_height) {
                frame_width  = w;
                frame_height = h;
                pixel_buffer.resize(w * h * 4);
            }

            // Copy row by row (stride may differ from w*4)
            for (uint32_t row = 0; row < h; ++row) {
                std::memcpy(pixel_buffer.data() + row * w * 4,
                           base + row * stride,
                           w * 4);
            }

            CVPixelBufferUnlockBaseAddress(cv_buf, kCVPixelBufferLock_ReadOnly);
            CVPixelBufferRelease(cv_buf);
            return true;
        }
    }

    void set_loop(bool loop) {
        @autoreleasepool {
            is_looping = loop;
            if (loop_observer) {
                loop_observer.shouldLoop = loop;
            }
        }
    }

    void set_speed(float speed) {
        @autoreleasepool {
            if (!player || !opened) return;
            bool force = loop_observer && loop_observer.loopFired;
            if (force) loop_observer.loopFired = NO;
            current_speed_ = speed;
            if (speed > 0.0f) {
                [player play];
                player.rate = speed;
            } else {
                [player pause];
            }
            if (loop_observer) loop_observer.desiredRate = speed;
        }
    }

    void play() {
        @autoreleasepool {
            if (player && opened) {
                [player play];
            }
        }
    }

    void pause() {
        @autoreleasepool {
            if (player && opened) {
                [player pause];
            }
        }
    }

    float current_time() const {
        @autoreleasepool {
            if (player && opened) {
                return static_cast<float>(CMTimeGetSeconds(player_item.currentTime));
            }
            return 0.0f;
        }
    }

    bool seek(double time_seconds) {
        if (!opened || !player_item || !player) return false;
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
};

// =============================================================================
// AVFDecoder public interface
// =============================================================================

AVFDecoder::AVFDecoder() : impl_(std::make_unique<Impl>()) {}
AVFDecoder::~AVFDecoder() { close(); }

bool AVFDecoder::open(const std::string& path) { return impl_->open(path); }
void AVFDecoder::close() { if (impl_) impl_->close(); }
bool AVFDecoder::is_open() const { return impl_ && impl_->opened; }
bool AVFDecoder::decode_frame() { return impl_->decode_frame(); }
const uint8_t* AVFDecoder::pixel_data() const { return impl_->pixel_buffer.data(); }
uint32_t AVFDecoder::width() const { return impl_->frame_width; }
uint32_t AVFDecoder::height() const { return impl_->frame_height; }
float AVFDecoder::duration() const { return impl_->media_duration; }
void AVFDecoder::set_loop(bool loop) { impl_->set_loop(loop); }
void AVFDecoder::set_speed(float speed) { impl_->set_speed(speed); }
void AVFDecoder::play() { impl_->play(); }
void AVFDecoder::pause() { impl_->pause(); }
float AVFDecoder::current_time() const { return impl_->current_time(); }
bool AVFDecoder::seek(double time_seconds) { return impl_->seek(time_seconds); }
float AVFDecoder::frame_rate() const { return impl_->frame_rate_; }

std::unique_ptr<VideoDecoder> create_avf_decoder() {
    return std::make_unique<AVFDecoder>();
}
