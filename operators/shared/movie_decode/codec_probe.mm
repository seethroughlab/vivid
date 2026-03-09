#import "codec_probe.h"
#include "hap_codec.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

namespace {
static std::string fourcc_to_string(uint32_t code) {
    char s[5];
    s[0] = static_cast<char>((code >> 24) & 0xFF);
    s[1] = static_cast<char>((code >> 16) & 0xFF);
    s[2] = static_cast<char>((code >> 8) & 0xFF);
    s[3] = static_cast<char>(code & 0xFF);
    s[4] = '\0';
    for (int i = 0; i < 4; ++i) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        if (ch < 32 || ch > 126) s[i] = '?';
    }
    return std::string(s);
}

static NSArray<AVAssetTrack*>* load_video_tracks(AVAsset* asset) {
    __block NSArray<AVAssetTrack*>* tracks = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    [asset loadTracksWithMediaType:AVMediaTypeVideo completionHandler:^(NSArray<AVAssetTrack*>* t, NSError* error) {
        if (!error && t) tracks = [t copy];
        dispatch_semaphore_signal(sem);
    }];
    dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    return tracks;
}
} // namespace

VideoCodecProbeResult probe_video_codec_fourcc(const std::string& path) {
    VideoCodecProbeResult out{};
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        AVAsset* asset = [AVAsset assetWithURL:url];
        if (!asset) return out;
        NSArray<AVAssetTrack*>* tracks = load_video_tracks(asset);
        if (!tracks || tracks.count == 0) return out;
        AVAssetTrack* track = tracks[0];
        NSArray* descs = track.formatDescriptions;
        if (!descs || descs.count == 0) return out;
        CMFormatDescriptionRef desc = (__bridge CMFormatDescriptionRef)descs[0];
        FourCharCode codec = CMFormatDescriptionGetMediaSubType(desc);
        out.ok = true;
        out.fourcc = codec;
        out.fourcc_text = fourcc_to_string(codec);
        out.is_hap = vivid_is_hap_fourcc(codec);
        out.is_notchlc = vivid_is_notchlc_fourcc(codec);
    }
    return out;
}
