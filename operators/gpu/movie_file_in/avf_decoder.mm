#import "avf_decoder.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#include <vector>
#include <cstdio>
#include <cstring>

// =============================================================================
// LoopObserver — restarts playback when the item reaches end
// =============================================================================

@interface LoopObserver : NSObject
@property (nonatomic, assign) BOOL shouldLoop;
@property (nonatomic, weak) AVPlayer* player;
@end

@implementation LoopObserver

- (instancetype)initWithPlayer:(AVPlayer*)player {
    self = [super init];
    if (self) {
        _player = player;
        _shouldLoop = YES;
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
            [self->_player play];
        }
    }];
}

@end

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
    bool     is_looping    = true;
    bool     opened        = false;
    float    current_speed_ = 1.0f;

    bool open(const std::string& path) {
        @autoreleasepool {
            NSURL* url = [NSURL fileURLWithPath:
                [NSString stringWithUTF8String:path.c_str()]];
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

            // Wait for player item to be ready to play
            // Must pump the run loop so AVFoundation can update status
            for (int i = 0; i < 300; ++i) {
                if (player_item.status != AVPlayerItemStatusUnknown) break;
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
            }

            if (player_item.status != AVPlayerItemStatusReadyToPlay) {
                std::fprintf(stderr, "[avf_decoder] Player item not ready (status=%ld)\n",
                             (long)player_item.status);
                close();
                return false;
            }

            pixel_buffer.resize(frame_width * frame_height * 4);
            opened = true;
            return true;
        }
    }

    void close() {
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

            CMTime time = player_item.currentTime;
            if (![video_output hasNewPixelBufferForItemTime:time])
                return false;

            CVPixelBufferRef cv_buf = [video_output
                copyPixelBufferForItemTime:time
                itemTimeForDisplay:nil];
            if (!cv_buf) return false;

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
            if (player && opened && speed != current_speed_) {
                current_speed_ = speed;
                player.rate = speed;
            }
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

std::unique_ptr<VideoDecoder> create_avf_decoder() {
    return std::make_unique<AVFDecoder>();
}
