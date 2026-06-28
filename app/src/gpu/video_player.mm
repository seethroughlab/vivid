#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>   // CACurrentMediaTime
#include "gpu/video_player.h"
#include <vector>
#include <cstring>
#include <cstdio>

// Minimal AVFoundation real-time player (manual retain/release — no ARC, matching
// the rest of the app). AVPlayer drives playback; AVPlayerItemVideoOutput vends
// the current frame as a 32BGRA CVPixelBuffer, copied tightly-packed for upload.
struct VideoPlayer {
    AVPlayer*                 player = nil;
    AVPlayerItem*             item   = nil;
    AVPlayerItemVideoOutput*  output = nil;
    id                        end_observer = nil;
    std::vector<uint8_t>      frame;   // tightly-packed BGRA
    uint32_t                  w = 0, h = 0;
};

VideoPlayer* video_open(const char* path) {
    if (!path) return nullptr;
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
        if (!asset) return nullptr;
        AVPlayerItem* item = [AVPlayerItem playerItemWithAsset:asset];
        NSDictionary* attrs = @{ (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA) };
        AVPlayerItemVideoOutput* output =
            [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attrs];  // owned (+1)
        [item addOutput:output];
        AVPlayer* player = [AVPlayer playerWithPlayerItem:item];
        player.actionAtItemEnd = AVPlayerActionAtItemEndNone;

        VideoPlayer* vp = new VideoPlayer();
        vp->player = [player retain];
        vp->item   = [item retain];
        vp->output = output;  // already +1
        AVPlayer* pl = vp->player;
        vp->end_observer = [[[NSNotificationCenter defaultCenter]
            addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                        object:item queue:nil
                    usingBlock:^(NSNotification*) { [pl seekToTime:kCMTimeZero]; [pl play]; }] retain];
        [vp->player play];
        return vp;
    }
}

void video_play(VideoPlayer* vp, bool playing) {
    if (!vp || !vp->player) return;
    if (playing) [vp->player play]; else [vp->player pause];
}

bool video_next_frame(VideoPlayer* vp, const uint8_t** out, uint32_t* ow, uint32_t* oh) {
    if (!vp || !vp->output) return false;
    @autoreleasepool {
        CMTime t = [vp->output itemTimeForHostTime:CACurrentMediaTime()];
        if (![vp->output hasNewPixelBufferForItemTime:t]) return false;
        CVPixelBufferRef pb = [vp->output copyPixelBufferForItemTime:t itemTimeForDisplay:nil];
        if (!pb) return false;
        CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        const uint32_t w = static_cast<uint32_t>(CVPixelBufferGetWidth(pb));
        const uint32_t h = static_cast<uint32_t>(CVPixelBufferGetHeight(pb));
        const size_t   stride = CVPixelBufferGetBytesPerRow(pb);
        const uint8_t* base = static_cast<const uint8_t*>(CVPixelBufferGetBaseAddress(pb));
        if (base && w && h) {
            vp->frame.resize(static_cast<size_t>(w) * h * 4);
            for (uint32_t y = 0; y < h; ++y)
                std::memcpy(vp->frame.data() + static_cast<size_t>(y) * w * 4, base + y * stride, static_cast<size_t>(w) * 4);
            vp->w = w; vp->h = h;
        }
        CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(pb);
        if (!vp->w) return false;
        *out = vp->frame.data(); *ow = vp->w; *oh = vp->h;
        return true;
    }
}

void video_close(VideoPlayer* vp) {
    if (!vp) return;
    @autoreleasepool {
        if (vp->player) [vp->player pause];
        if (vp->end_observer) {
            [[NSNotificationCenter defaultCenter] removeObserver:vp->end_observer];
            [vp->end_observer release];
        }
        [vp->output release];
        [vp->item release];
        [vp->player release];
    }
    delete vp;
}
