#include "platform/review_player.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>   // CACurrentMediaTime

#include <cmath>
#include <cstring>
#include <vector>

// AVFoundation review player (ARC). One AVPlayer per media file: audio goes to the system default
// output through AVFoundation (never miniaudio / the session mix); video frames are pulled through an
// AVPlayerItemVideoOutput as BGRA8 and copied into a CPU buffer the caller uploads to a 2D texture.
// Synchronized A/B uses -[AVPlayer setRate:time:atHostTime:] with a shared host time, so several
// players start in lockstep and stay locked to the same clock.
namespace vivid {

namespace {

class AVFReviewPlayer final : public ReviewPlayer {
public:
    ~AVFReviewPlayer() override { close(); }

    bool open(const std::string& path) override {
        close();
        if (path.empty()) { err_ = "empty path"; return false; }
        @autoreleasepool {
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
            if (![[NSFileManager defaultManager] fileExistsAtPath:url.path]) { err_ = "file not found: " + path; return false; }
            asset_ = [AVURLAsset URLAssetWithURL:url options:nil];
            if (!asset_) { err_ = "cannot open asset: " + path; return false; }
            item_ = [AVPlayerItem playerItemWithAsset:asset_];
            NSDictionary* attrs = @{ (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                                     (id)kCVPixelBufferIOSurfacePropertiesKey: @{} };
            output_ = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attrs];
            [item_ addOutput:output_];
            player_ = [AVPlayer playerWithPlayerItem:item_];
            // Local files: never let AVPlayer delay a start "to minimize stalling" — a synchronized
            // A/B needs every player to honor the requested host time exactly.
            player_.automaticallyWaitsToMinimizeStalling = NO;
            player_.actionAtItemEnd = AVPlayerActionAtItemEndPause;
            player_.volume = muted_ ? 0.f : gain_;
            __block bool* ended = &ended_;
            end_observer_ = [[NSNotificationCenter defaultCenter]
                addObserverForName:AVPlayerItemDidPlayToEndTimeNotification object:item_
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification*) { *ended = true; }];
            // Kick the track/duration load so is_ready() can read dimensions without blocking.
            [asset_ loadValuesAsynchronouslyForKeys:@[@"tracks", @"duration"] completionHandler:^{}];
        }
        path_ = path; open_ = true; ready_ = false; ended_ = false; err_.clear();
        vw_ = vh_ = 0; duration_ = 0.0;
        return true;
    }

    void close() override {
        @autoreleasepool {
            if (player_) [player_ pause];
            if (end_observer_) { [[NSNotificationCenter defaultCenter] removeObserver:end_observer_]; end_observer_ = nil; }
            if (item_ && output_) [item_ removeOutput:output_];
            output_ = nil; item_ = nil; player_ = nil; asset_ = nil;
        }
        open_ = ready_ = ended_ = false; path_.clear(); frame_.clear(); fw_ = fh_ = fbpr_ = 0;
        vw_ = vh_ = 0; duration_ = 0.0;
    }

    bool is_open() const override { return open_; }
    bool is_ready() const override {
        if (!open_ || !item_) return false;
        if (ready_) return true;
        @autoreleasepool {
            const AVPlayerItemStatus st = item_.status;
            if (st == AVPlayerItemStatusFailed) {
                const_cast<AVFReviewPlayer*>(this)->err_ =
                    item_.error ? std::string(item_.error.localizedDescription.UTF8String) : "load failed";
                return false;
            }
            if (st != AVPlayerItemStatusReadyToPlay) return false;
            auto* self = const_cast<AVFReviewPlayer*>(this);
            self->duration_ = CMTimeGetSeconds(item_.duration);
            if (!std::isfinite(self->duration_) || self->duration_ < 0.0) self->duration_ = 0.0;
            // Video dimensions from the first video track, honoring its preferred transform (rotation).
            NSArray<AVAssetTrack*>* tracks = nil;
            if ([asset_ statusOfValueForKey:@"tracks" error:nil] == AVKeyValueStatusLoaded) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                tracks = [asset_ tracksWithMediaType:AVMediaTypeVideo];
#pragma clang diagnostic pop
            }
            if (tracks.count) {
                AVAssetTrack* tr = tracks[0];
                const CGSize sz = CGSizeApplyAffineTransform(tr.naturalSize, tr.preferredTransform);
                self->vw_ = static_cast<uint32_t>(std::lround(std::fabs(sz.width)));
                self->vh_ = static_cast<uint32_t>(std::lround(std::fabs(sz.height)));
            }
            self->ready_ = true;
        }
        return true;
    }
    std::string error() const override { return err_; }
    const std::string& path() const override { return path_; }

    double duration() const override { return duration_; }
    double current_time() const override {
        if (!player_) return 0.0;
        const double t = CMTimeGetSeconds(player_.currentTime);
        return std::isfinite(t) && t >= 0.0 ? t : 0.0;
    }
    bool playing() const override { return player_ && player_.rate != 0.f; }
    bool at_end() const override { return ended_; }

    void play() override {
        if (!player_) return;
        if (ended_) { ended_ = false; [player_ seekToTime:kCMTimeZero toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero]; }
        [player_ play];
    }
    void pause() override { if (player_) [player_ pause]; }
    void seek(double sec) override {
        if (!player_) return;
        ended_ = false;
        const double s = sec < 0.0 ? 0.0 : (duration_ > 0.0 && sec > duration_ ? duration_ : sec);
        [player_ seekToTime:CMTimeMakeWithSeconds(s, 600) toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
    }
    void play_at(double media_sec, double host_time_sec) override {
        if (!player_) return;
        ended_ = false;
        const double s = media_sec < 0.0 ? 0.0 : media_sec;
        // Preroll at the target so the first frame + audio are primed, then arm the synchronized start.
        [player_ seekToTime:CMTimeMakeWithSeconds(s, 600) toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        // The host time is a CMTime on the host-time clock (seconds from review_host_time_now()).
        [player_ setRate:1.0f time:CMTimeMakeWithSeconds(s, 600)
              atHostTime:CMTimeMakeWithSeconds(host_time_sec, 1000000000)];
    }

    void  set_gain(float g) override { gain_ = g < 0.f ? 0.f : g; if (player_) player_.volume = muted_ ? 0.f : gain_; }
    float gain() const override { return gain_; }
    void  set_muted(bool m) override { muted_ = m; if (player_) player_.volume = muted_ ? 0.f : gain_; }
    bool  muted() const override { return muted_; }

    bool poll_frame(ReviewFrame& out) override {
        if (!open_ || !output_ || !player_) return false;
        @autoreleasepool {
            const CMTime now = [output_ itemTimeForHostTime:CACurrentMediaTime()];
            if (![output_ hasNewPixelBufferForItemTime:now]) return false;
            CMTime shown = kCMTimeInvalid;
            CVPixelBufferRef pb = [output_ copyPixelBufferForItemTime:now itemTimeForDisplay:&shown];
            if (!pb) return false;
            CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
            const uint32_t w = static_cast<uint32_t>(CVPixelBufferGetWidth(pb));
            const uint32_t h = static_cast<uint32_t>(CVPixelBufferGetHeight(pb));
            const size_t   src_bpr = CVPixelBufferGetBytesPerRow(pb);
            const auto*    src = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pb));
            const uint32_t dst_bpr = w * 4;   // repack tightly: the caller uploads with bytesPerRow = w*4
            frame_.resize(static_cast<size_t>(dst_bpr) * h);
            for (uint32_t y = 0; y < h; ++y)
                std::memcpy(frame_.data() + static_cast<size_t>(y) * dst_bpr, src + static_cast<size_t>(y) * src_bpr, dst_bpr);
            CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
            CVBufferRelease(pb);
            fw_ = w; fh_ = h; fbpr_ = dst_bpr;
            out.bgra = frame_.data(); out.width = w; out.height = h; out.bytes_per_row = dst_bpr;
            out.time_sec = CMTIME_IS_VALID(shown) ? CMTimeGetSeconds(shown) : current_time();
        }
        return true;
    }

    uint32_t video_width()  const override { return vw_; }
    uint32_t video_height() const override { return vh_; }

private:
    AVURLAsset*              asset_  = nil;
    AVPlayerItem*            item_   = nil;
    AVPlayerItemVideoOutput* output_ = nil;
    AVPlayer*                player_ = nil;
    id                       end_observer_ = nil;

    std::string path_, err_;
    bool   open_ = false, ready_ = false, ended_ = false, muted_ = false;
    float  gain_ = 1.f;
    double duration_ = 0.0;
    uint32_t vw_ = 0, vh_ = 0;

    std::vector<uint8_t> frame_;
    uint32_t fw_ = 0, fh_ = 0, fbpr_ = 0;
};

}  // namespace

std::unique_ptr<ReviewPlayer> make_platform_review_player() { return std::make_unique<AVFReviewPlayer>(); }

// The host-time clock AVPlayer's atHostTime: is expressed on (mach-absolute based, the same time base
// CACurrentMediaTime reports; kept on CMClock so play_at's CMTime and this value agree exactly).
double review_host_time_now() { return CMTimeGetSeconds(CMClockGetTime(CMClockGetHostTimeClock())); }

}  // namespace vivid
