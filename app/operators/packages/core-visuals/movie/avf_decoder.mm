#import "avf_decoder.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>   // CACurrentMediaTime

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// Manual retain/release (no ARC — matches the rest of the app). AVPlayer drives playback;
// AVPlayerItemVideoOutput vends the current frame as a 32BGRA CVPixelBuffer, copied tightly-packed.
struct AVFDecoder::Impl {
    AVPlayer*                player = nil;
    AVPlayerItem*            item   = nil;
    AVPlayerItemVideoOutput* output = nil;
    id                       end_observer = nil;

    std::vector<uint8_t> frame;   // tightly-packed BGRA8
    uint32_t w = 0, h = 0;
    float    duration = 0.f;
    float    fps = 30.f;
    float    speed = 1.f;
    bool     loop = true;
    bool     opened = false;
    uint64_t nil_count = 0;

    void apply_end_behaviour() {
        if (!player) return;
        // Loop: seek to zero and keep playing at the current rate. Not looping: hold the last frame
        // (AVPlayer with actionAtItemEnd=None stops at the end, leaving the final frame available).
        AVPlayer* pl = player;
        Impl* self = this;
        if (end_observer) {
            [[NSNotificationCenter defaultCenter] removeObserver:end_observer];
            [end_observer release];
            end_observer = nil;
        }
        end_observer = [[[NSNotificationCenter defaultCenter]
            addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                        object:item queue:nil
                    usingBlock:^(NSNotification*) {
                        if (self->loop) { [pl seekToTime:kCMTimeZero]; pl.rate = self->speed; }
                    }] retain];
    }
};

AVFDecoder::AVFDecoder() : impl_(std::make_unique<Impl>()) {}
AVFDecoder::~AVFDecoder() { close(); }

bool AVFDecoder::open(const std::string& path) {
    close();
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
        if (!asset) return false;
        AVPlayerItem* item = [AVPlayerItem playerItemWithAsset:asset];
        NSDictionary* attrs = @{ (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
        AVPlayerItemVideoOutput* output =
            [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attrs];   // owned (+1)
        [item addOutput:output];
        AVPlayer* player = [AVPlayer playerWithPlayerItem:item];
        player.actionAtItemEnd = AVPlayerActionAtItemEndNone;
        player.muted = YES;   // the movie's audio plays through OUR engine (the movie-audio bus), not CoreAudio

        impl_->player = [player retain];
        impl_->item   = [item retain];
        impl_->output = output;   // already +1
        impl_->duration = static_cast<float>(CMTimeGetSeconds(asset.duration));
        AVAssetTrack* vtrack = [asset tracksWithMediaType:AVMediaTypeVideo].firstObject;
        if (vtrack && vtrack.nominalFrameRate > 0.f) impl_->fps = vtrack.nominalFrameRate;
        impl_->apply_end_behaviour();
        impl_->player.rate = impl_->speed;   // start playing at the current rate
        impl_->opened = true;
        std::fprintf(stderr, "[avf_decoder] opened %s: fps=%.2f dur=%.3f\n", path.c_str(), impl_->fps, impl_->duration);
        return true;
    }
}

void AVFDecoder::close() {
    if (!impl_) return;
    @autoreleasepool {
        if (impl_->player) [impl_->player pause];
        if (impl_->end_observer) {
            [[NSNotificationCenter defaultCenter] removeObserver:impl_->end_observer];
            [impl_->end_observer release];
            impl_->end_observer = nil;
        }
        if (impl_->output) [impl_->output release];
        if (impl_->item)   [impl_->item release];
        if (impl_->player) [impl_->player release];
        impl_->player = nil; impl_->item = nil; impl_->output = nil;
    }
    impl_->frame.clear(); impl_->w = impl_->h = 0; impl_->opened = false;
}

bool AVFDecoder::is_open() const { return impl_ && impl_->opened; }

DecodeStatus AVFDecoder::decode_frame() {
    if (!impl_ || !impl_->output) return DecodeStatus::NilFrame;
    @autoreleasepool {
        CMTime t = [impl_->output itemTimeForHostTime:CACurrentMediaTime()];
        if (![impl_->output hasNewPixelBufferForItemTime:t]) {
            if (impl_->item && impl_->item.status != AVPlayerItemStatusReadyToPlay) impl_->nil_count++;
            return DecodeStatus::ReusedFrame;   // no new frame this tick (normal between video frames)
        }
        CVPixelBufferRef pb = [impl_->output copyPixelBufferForItemTime:t itemTimeForDisplay:nil];
        if (!pb) return DecodeStatus::ReusedFrame;
        CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        const uint32_t w = static_cast<uint32_t>(CVPixelBufferGetWidth(pb));
        const uint32_t h = static_cast<uint32_t>(CVPixelBufferGetHeight(pb));
        const size_t   stride = CVPixelBufferGetBytesPerRow(pb);
        const uint8_t* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pb));
        bool got = false;
        if (base && w && h) {
            impl_->frame.resize(static_cast<size_t>(w) * h * 4);
            for (uint32_t y = 0; y < h; ++y)
                std::memcpy(impl_->frame.data() + static_cast<size_t>(y) * w * 4, base + y * stride, static_cast<size_t>(w) * 4);
            impl_->w = w; impl_->h = h; got = true;
        }
        CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(pb);
        return got ? DecodeStatus::NewFrame : DecodeStatus::ReusedFrame;
    }
}

// Audio-master presentation: hand back the frame at media time `t`. The muted AVPlayer plays near
// `t` in real time so the frame is buffered; if the player's clock drifts from the master beyond a
// tolerance we seek it back. copyPixelBufferForItemTime is frame-accurate to the requested time.
DecodeStatus AVFDecoder::present_at(double t) {
    if (!impl_ || !impl_->output || !impl_->player) return DecodeStatus::NilFrame;
    @autoreleasepool {
        const double cur = CMTimeGetSeconds(impl_->player.currentTime);
        if (std::abs(cur - t) > 0.25) {   // drifted (or looped) — snap the player back to the master clock
            [impl_->player seekToTime:CMTimeMakeWithSeconds(t, 600)
                      toleranceBefore:kCMTimePositiveInfinity toleranceAfter:kCMTimePositiveInfinity];
        }
        const CMTime it = CMTimeMakeWithSeconds(t, 600);
        CVPixelBufferRef pb = [impl_->output copyPixelBufferForItemTime:it itemTimeForDisplay:nil];
        if (!pb) return DecodeStatus::ReusedFrame;   // no buffer for this time yet — hold the last frame
        CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        const uint32_t w = static_cast<uint32_t>(CVPixelBufferGetWidth(pb));
        const uint32_t h = static_cast<uint32_t>(CVPixelBufferGetHeight(pb));
        const size_t   stride = CVPixelBufferGetBytesPerRow(pb);
        const uint8_t* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pb));
        bool got = false;
        if (base && w && h) {
            impl_->frame.resize(static_cast<size_t>(w) * h * 4);
            for (uint32_t y = 0; y < h; ++y)
                std::memcpy(impl_->frame.data() + static_cast<size_t>(y) * w * 4, base + y * stride, static_cast<size_t>(w) * 4);
            impl_->w = w; impl_->h = h; got = true;
        }
        CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(pb);
        return got ? DecodeStatus::NewFrame : DecodeStatus::ReusedFrame;
    }
}

const uint8_t* AVFDecoder::pixel_data() const { return impl_->frame.empty() ? nullptr : impl_->frame.data(); }
uint32_t AVFDecoder::width() const { return impl_->w; }
uint32_t AVFDecoder::height() const { return impl_->h; }
float AVFDecoder::duration() const { return impl_->duration; }
void AVFDecoder::set_loop(bool loop) { if (impl_) { impl_->loop = loop; impl_->apply_end_behaviour(); } }
void AVFDecoder::set_speed(float speed) {
    if (!impl_ || !impl_->player) return;
    impl_->speed = std::max(0.f, speed);
    impl_->player.rate = impl_->speed;   // 0 pauses; >0 plays at that multiple
}
float AVFDecoder::current_time() const {
    return (impl_ && impl_->player) ? static_cast<float>(CMTimeGetSeconds(impl_->player.currentTime)) : 0.f;
}
bool AVFDecoder::seek(double time_seconds) {
    if (!impl_ || !impl_->player) return false;
    [impl_->player seekToTime:CMTimeMakeWithSeconds(std::max(0.0, time_seconds), 600)];
    return true;
}
float AVFDecoder::frame_rate() const { return impl_->fps; }

std::unique_ptr<VideoDecoder> create_avf_decoder() { return std::make_unique<AVFDecoder>(); }
