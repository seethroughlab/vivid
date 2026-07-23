#import "avf_decoder.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// AVAssetReader-based sequential decoder (ARC). Frames come out as tightly-packed BGRA8. Because we
// drive the reader ourselves (not an AVPlayer), present_at(t) can decode forward to EXACTLY the
// audio master time and hand back that frame — frame-accurate A/V lock, no seeking, no rate control.
namespace {
static NSArray<AVAssetTrack*>* load_video_tracks(AVAsset* asset) {
    __block NSArray<AVAssetTrack*>* tracks = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [asset loadTracksWithMediaType:AVMediaTypeVideo completionHandler:^(NSArray<AVAssetTrack*>* t, NSError* e) {
        if (e) std::fprintf(stderr, "[avf_decoder] loadTracks error: %s\n", e.localizedDescription.UTF8String);
        if (!e && t) tracks = [t copy];
        dispatch_semaphore_signal(sem);
    }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    if (!tracks.count) {   // the async load can come back empty (racy); fall back to the sync property
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
#pragma clang diagnostic pop
    }
    return tracks;
}
}  // namespace

struct AVFDecoder::Impl {
    AVAsset*                  asset  = nil;
    AVAssetReader*            reader = nil;
    AVAssetReaderTrackOutput* output = nil;

    std::vector<uint8_t> frame;   // tightly-packed BGRA8 of the currently-held frame
    uint32_t w = 0, h = 0;
    float    duration = 0.f;
    float    fps = 30.f;
    float    speed = 1.f;
    bool     loop = true;
    bool     opened = false;
    double   cur_pts = -1.0;      // media time of the held frame
    std::chrono::steady_clock::time_point last_decode{};   // self-clock throttle (no-audio path)

    // (Re)create the reader starting at `start_seconds`. Frames come out as 32BGRA.
    void reset_reader(double start_seconds) {
        if (reader) { [reader cancelReading]; reader = nil; }
        output = nil;
        if (!asset) return;
        NSError* err = nil;
        reader = [[AVAssetReader alloc] initWithAsset:asset error:&err];
        if (err || !reader) { std::fprintf(stderr, "[avf_decoder] reader init failed: %s\n", err ? err.localizedDescription.UTF8String : "nil"); reader = nil; return; }
        NSArray<AVAssetTrack*>* tracks = load_video_tracks(asset);
        if (!tracks.count) { std::fprintf(stderr, "[avf_decoder] no video tracks\n"); [reader cancelReading]; reader = nil; return; }
        NSDictionary* settings = @{ (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
        output = [[AVAssetReaderTrackOutput alloc] initWithTrack:tracks[0] outputSettings:settings];
        output.alwaysCopiesSampleData = YES;
        if (![reader canAddOutput:output]) { std::fprintf(stderr, "[avf_decoder] canAddOutput NO\n"); [reader cancelReading]; reader = nil; output = nil; return; }
        [reader addOutput:output];
        if (start_seconds > 0.0) {
            CMTime start = CMTimeMakeWithSeconds(start_seconds, 600);
            CMTime dur = CMTimeSubtract(asset.duration, start);
            if (CMTimeCompare(dur, kCMTimeZero) <= 0) dur = kCMTimeZero;
            reader.timeRange = CMTimeRangeMake(start, dur);
        }
        if (![reader startReading]) {
            std::fprintf(stderr, "[avf_decoder] startReading failed: status=%ld %s\n",
                         (long)reader.status, reader.error ? reader.error.localizedDescription.UTF8String : "");
            [reader cancelReading]; reader = nil; output = nil;
        }
        cur_pts = start_seconds;
    }

    // Decode the next frame into `frame`; returns false at end-of-stream.
    bool read_one() {
        if (!output) return false;
        @autoreleasepool {
            CMSampleBufferRef sb = [output copyNextSampleBuffer];   // nil at end-of-stream (normal; caller loops)
            if (!sb) return false;
            cur_pts = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sb));
            CVImageBufferRef pb = CMSampleBufferGetImageBuffer(sb);
            bool got = false;
            if (pb) {
                CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
                const uint32_t w0 = (uint32_t)CVPixelBufferGetWidth(pb), h0 = (uint32_t)CVPixelBufferGetHeight(pb);
                const size_t stride = CVPixelBufferGetBytesPerRow(pb);
                const uint8_t* base = (const uint8_t*)CVPixelBufferGetBaseAddress(pb);
                if (base && w0 && h0) {
                    frame.resize((size_t)w0 * h0 * 4);
                    for (uint32_t y = 0; y < h0; ++y)
                        std::memcpy(frame.data() + (size_t)y * w0 * 4, base + y * stride, (size_t)w0 * 4);
                    w = w0; h = h0; got = true;
                }
                CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
            }
            CFRelease(sb);
            return got;
        }
    }
};

AVFDecoder::AVFDecoder() : impl_(std::make_unique<Impl>()) {}
AVFDecoder::~AVFDecoder() { close(); }

bool AVFDecoder::open(const std::string& path) {
    close();
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        impl_->asset = [AVAsset assetWithURL:url];   // matches codec_probe (which loads this file fine)
        if (!impl_->asset) return false;
        NSArray<AVAssetTrack*>* tracks = load_video_tracks(impl_->asset);
        if (!tracks.count) { std::fprintf(stderr, "[avf_decoder] open: 0 video tracks for %s\n", path.c_str()); return false; }
        AVAssetTrack* tr = tracks[0];
        CGSize sz = tr.naturalSize;
        impl_->w = (uint32_t)std::abs(sz.width); impl_->h = (uint32_t)std::abs(sz.height);
        impl_->fps = tr.nominalFrameRate > 0.f ? tr.nominalFrameRate : 30.f;
        impl_->duration = (float)CMTimeGetSeconds(impl_->asset.duration);
        impl_->reset_reader(0.0);
        if (!impl_->reader) return false;
        impl_->opened = true;
        impl_->last_decode = std::chrono::steady_clock::now() - std::chrono::seconds(1);  // first decode_frame not throttled
        std::fprintf(stderr, "[avf_decoder] opened %s: fps=%.2f dur=%.3f %ux%u\n",
                     path.c_str(), impl_->fps, impl_->duration, impl_->w, impl_->h);
        return true;
    }
}

void AVFDecoder::close() {
    if (!impl_) return;
    @autoreleasepool {
        if (impl_->reader) { [impl_->reader cancelReading]; impl_->reader = nil; }
        impl_->output = nil; impl_->asset = nil;
    }
    impl_->frame.clear(); impl_->w = impl_->h = 0; impl_->opened = false; impl_->cur_pts = -1.0;
}

bool AVFDecoder::is_open() const { return impl_ && impl_->opened; }

// Self-clock (no audio master): advance one frame at ~fps·speed by wall time; loop at EOF.
DecodeStatus AVFDecoder::decode_frame() {
    if (!impl_ || !impl_->opened) return DecodeStatus::NilFrame;
    const float efps = std::max(1.f, impl_->fps * std::max(0.01f, impl_->speed));
    const auto now = std::chrono::steady_clock::now();
    if (now - impl_->last_decode < std::chrono::duration<double>(1.0 / efps)) return DecodeStatus::ReusedFrame;
    impl_->last_decode = now;
    if (!impl_->read_one()) {
        if (!impl_->loop) return DecodeStatus::ReusedFrame;
        impl_->reset_reader(0.0);
        if (!impl_->read_one()) return DecodeStatus::NilFrame;
    }
    return DecodeStatus::NewFrame;
}

// Audio-master: decode FORWARD to media time `t` and hand back that frame. Recreate the reader when
// `t` jumps backward (a loop wrap or seek). Frame-accurate: the frame shown is the one at `t`.
DecodeStatus AVFDecoder::present_at(double t) {
    if (!impl_ || !impl_->opened) return DecodeStatus::NilFrame;
    // Only a BIG backward jump (loop wrap / seek) recreates the reader — NOT the sub-frame overshoot
    // from decoding forward past t (present_at stops at the first frame >= t, so cur_pts is normally a
    // fraction of a frame ahead; treating that as "backwards" would thrash the reader every frame).
    if (t < impl_->cur_pts - 0.5) {
        impl_->reset_reader(impl_->loop ? 0.0 : std::max(0.0, t));
        impl_->read_one();
    }
    bool advanced = false;
    int guard = 0;
    while (impl_->cur_pts < t && guard++ < 8) {
        if (!impl_->read_one()) {                     // hit EOF while catching up
            if (impl_->loop) { impl_->reset_reader(0.0); if (!impl_->read_one()) break; }
            else break;
        }
        advanced = true;
    }
    return advanced ? DecodeStatus::NewFrame : DecodeStatus::ReusedFrame;
}

const uint8_t* AVFDecoder::pixel_data() const { return impl_->frame.empty() ? nullptr : impl_->frame.data(); }
uint32_t AVFDecoder::width() const { return impl_->w; }
uint32_t AVFDecoder::height() const { return impl_->h; }
float AVFDecoder::duration() const { return impl_->duration; }
void AVFDecoder::set_loop(bool loop) { if (impl_) impl_->loop = loop; }
void AVFDecoder::set_speed(float speed) { if (impl_) impl_->speed = std::max(0.f, speed); }
float AVFDecoder::current_time() const { return impl_ ? (float)std::max(0.0, impl_->cur_pts) : 0.f; }
bool AVFDecoder::seek(double t) {
    if (!impl_ || !impl_->opened) return false;
    impl_->reset_reader(std::max(0.0, t));
    impl_->read_one();
    return impl_->reader != nil;
}
float AVFDecoder::frame_rate() const { return impl_ ? impl_->fps : 30.f; }

std::unique_ptr<VideoDecoder> create_avf_decoder() { return std::make_unique<AVFDecoder>(); }
