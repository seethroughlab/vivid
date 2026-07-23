#import "hap_decoder.h"
#include "hap_codec.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

extern "C" {
#include "hap.h"
}

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

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

static void hap_decode_callback(HapDecodeWorkFunction function, void* p,
                                unsigned int count, void* /*info*/) {
    for (unsigned int i = 0; i < count; ++i) function(p, i);
}

} // namespace

struct HAPDecoder::Impl {
    AVAsset* asset = nil;
    AVAssetReader* reader = nil;
    AVAssetReaderTrackOutput* video_output = nil;

    std::vector<uint8_t> sample_data;
    std::vector<uint8_t> decoded_data;

    uint32_t frame_width = 0;
    uint32_t frame_height = 0;
    float media_duration = 0.0f;
    float frame_rate = 30.0f;
    float speed = 1.0f;
    bool loop = true;
    bool opened = false;
    bool playing = true;

    VideoCompressedFormat format = VideoCompressedFormat::None;
    bool ycocg_encoded = false;
    float current_time_s = 0.0f;
    uint64_t nil_frame_counter = 0;
    std::chrono::steady_clock::time_point last_decode{};

    void reset_reader(double start_seconds = 0.0) {
        if (!asset) return;
        if (reader) {
            [reader cancelReading];
            reader = nil;
        }
        video_output = nil;

        NSError* error = nil;
        reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
        if (error || !reader) {
            std::fprintf(stderr, "[hap_decoder] Failed to create asset reader\n");
            return;
        }

        NSArray<AVAssetTrack*>* tracks = load_video_tracks(asset);
        if (!tracks || tracks.count == 0) {
            std::fprintf(stderr, "[hap_decoder] No video tracks\n");
            [reader cancelReading];
            reader = nil;
            return;
        }

        AVAssetTrack* track = tracks[0];
        video_output = [[AVAssetReaderTrackOutput alloc] initWithTrack:track outputSettings:nil];
        video_output.alwaysCopiesSampleData = YES;
        if (![reader canAddOutput:video_output]) {
            std::fprintf(stderr, "[hap_decoder] Cannot add track output\n");
            [reader cancelReading];
            reader = nil;
            video_output = nil;
            return;
        }
        [reader addOutput:video_output];
        if (start_seconds > 0.0) {
            CMTime start = CMTimeMakeWithSeconds(start_seconds, 600);
            CMTime duration = CMTimeSubtract(asset.duration, start);
            if (CMTimeCompare(duration, kCMTimeZero) <= 0) {
                duration = kCMTimeZero;
            }
            reader.timeRange = CMTimeRangeMake(start, duration);
        }
        if (![reader startReading]) {
            std::fprintf(stderr, "[hap_decoder] Failed to start reading\n");
            [reader cancelReading];
            reader = nil;
            video_output = nil;
        }
    }
};

HAPDecoder::HAPDecoder() : impl_(std::make_unique<Impl>()) {}
HAPDecoder::~HAPDecoder() { close(); }

bool HAPDecoder::is_hap_file(const std::string& path) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        AVAsset* asset = [AVAsset assetWithURL:url];
        NSArray<AVAssetTrack*>* tracks = load_video_tracks(asset);
        if (!tracks || tracks.count == 0) return false;
        AVAssetTrack* track = tracks[0];
        NSArray* descs = track.formatDescriptions;
        if (!descs || descs.count == 0) return false;
        CMFormatDescriptionRef desc = (__bridge CMFormatDescriptionRef)descs[0];
        FourCharCode codec = CMFormatDescriptionGetMediaSubType(desc);
        return vivid_is_hap_fourcc(codec);
    }
}

bool HAPDecoder::open(const std::string& path) {
    close();
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        impl_->asset = [AVAsset assetWithURL:url];
        if (!impl_->asset) return false;

        NSArray<AVAssetTrack*>* tracks = load_video_tracks(impl_->asset);
        if (!tracks || tracks.count == 0) return false;

        AVAssetTrack* track = tracks[0];
        CGSize size = track.naturalSize;
        CGAffineTransform transform = track.preferredTransform;
        CGSize transformed = CGSizeApplyAffineTransform(size, transform);
        impl_->frame_width = static_cast<uint32_t>(std::abs(transformed.width));
        impl_->frame_height = static_cast<uint32_t>(std::abs(transformed.height));
        if (impl_->frame_width == 0 || impl_->frame_height == 0) {
            impl_->frame_width = static_cast<uint32_t>(size.width);
            impl_->frame_height = static_cast<uint32_t>(size.height);
        }
        impl_->frame_rate = track.nominalFrameRate > 0.0f ? track.nominalFrameRate : 30.0f;
        impl_->media_duration = static_cast<float>(CMTimeGetSeconds(impl_->asset.duration));

        std::fprintf(stderr, "[hap_decoder] Opened: %s  frame_rate=%.2f  duration=%.3f  %ux%u\n",
            path.c_str(), impl_->frame_rate, impl_->media_duration,
            impl_->frame_width, impl_->frame_height);

        impl_->reset_reader(0.0);
        if (!impl_->reader || !impl_->video_output) {
            close();
            return false;
        }
        impl_->opened = true;
        impl_->playing = true;
        impl_->last_decode = std::chrono::steady_clock::now();
        return true;
    }
}

void HAPDecoder::close() {
    if (!impl_) return;
    @autoreleasepool {
        if (impl_->reader) {
            [impl_->reader cancelReading];
            impl_->reader = nil;
        }
        impl_->video_output = nil;
        impl_->asset = nil;
    }
    impl_->sample_data.clear();
    impl_->decoded_data.clear();
    impl_->frame_width = 0;
    impl_->frame_height = 0;
    impl_->media_duration = 0.0f;
    impl_->frame_rate = 30.0f;
    impl_->current_time_s = 0.0f;
    impl_->format = VideoCompressedFormat::None;
    impl_->ycocg_encoded = false;
    impl_->opened = false;
}

bool HAPDecoder::is_open() const { return impl_ && impl_->opened; }

DecodeStatus HAPDecoder::decode_frame() {
    if (!impl_ || !impl_->opened || !impl_->playing || !impl_->video_output)
        return DecodeStatus::NilFrame;
    const float effective_fps = std::max(1.0f, impl_->frame_rate * std::max(0.01f, impl_->speed));
    const auto now = std::chrono::steady_clock::now();
    const auto min_dt = std::chrono::duration<double>(1.0 / effective_fps);
    if (now - impl_->last_decode < min_dt) return DecodeStatus::ReusedFrame;

    CMSampleBufferRef sample = [impl_->video_output copyNextSampleBuffer];
    if (!sample) {
        if (impl_->loop) {
            impl_->reset_reader(0.0);
            if (!impl_->reader || !impl_->video_output) return DecodeStatus::NilFrame;
            sample = [impl_->video_output copyNextSampleBuffer];
            if (!sample) { impl_->nil_frame_counter++; return DecodeStatus::NilFrame; }
        } else {
            impl_->playing = false;
            return DecodeStatus::NilFrame;
        }
    }

    CMTime pts = CMSampleBufferGetPresentationTimeStamp(sample);
    impl_->current_time_s = static_cast<float>(CMTimeGetSeconds(pts));

    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
    if (!block) {
        CFRelease(sample);
        return DecodeStatus::NilFrame;
    }
    const size_t data_len = static_cast<size_t>(CMBlockBufferGetDataLength(block));
    if (data_len == 0) {
        CFRelease(sample);
        return DecodeStatus::NilFrame;
    }
    impl_->sample_data.resize(data_len);
    OSStatus copy_err = CMBlockBufferCopyDataBytes(block, 0, data_len, impl_->sample_data.data());
    if (copy_err != noErr) {
        CFRelease(sample);
        return DecodeStatus::NilFrame;
    }

    unsigned int texture_count = 0;
    if (HapGetFrameTextureCount(impl_->sample_data.data(), data_len, &texture_count) != HapResult_No_Error ||
        texture_count < 1) {
        CFRelease(sample);
        return DecodeStatus::NilFrame;
    }

    unsigned int hap_fmt = 0;
    if (HapGetFrameTextureFormat(impl_->sample_data.data(), data_len, 0, &hap_fmt) != HapResult_No_Error) {
        CFRelease(sample);
        return DecodeStatus::NilFrame;
    }
    impl_->format = vivid_hap_to_compressed_format(hap_fmt);
    impl_->ycocg_encoded = (hap_fmt == 0x01u); // HapTextureFormat_YCoCg_DXT5
    const size_t bpb = vivid_compressed_bytes_per_block(impl_->format);
    if (impl_->format == VideoCompressedFormat::None || bpb == 0) {
        CFRelease(sample);
        return DecodeStatus::NilFrame;
    }

    const size_t blocks_w = (impl_->frame_width + 3) / 4;
    const size_t blocks_h = (impl_->frame_height + 3) / 4;
    const size_t out_size = blocks_w * blocks_h * bpb;
    impl_->decoded_data.resize(out_size);
    unsigned long used = 0;
    unsigned int out_fmt = 0;
    unsigned int res = HapDecode(impl_->sample_data.data(), data_len, 0, hap_decode_callback, nullptr,
                                 impl_->decoded_data.data(), out_size, &used, &out_fmt);
    CFRelease(sample);
    if (res != HapResult_No_Error || used == 0 || used > out_size) {
        return DecodeStatus::NilFrame;
    }
    if (used != out_size) {
        impl_->decoded_data.resize(static_cast<size_t>(used));
    }
    impl_->last_decode = now;
    return DecodeStatus::NewFrame;
}

// Audio-master presentation: decode sequentially (real-time throttle in decode_frame tracks the
// audio when both play at speed), snapping back with a seek when the decode drifts from `t` beyond a
// couple of frames or `t` jumped backwards (a loop wrap).
DecodeStatus HAPDecoder::present_at(double t) {
    if (!impl_ || !impl_->opened) return DecodeStatus::NilFrame;
    const double cur = static_cast<double>(impl_->current_time_s);
    const double frame_dt = 1.0 / std::max(1.0f, impl_->frame_rate);
    if (t + 0.001 < cur || t - cur > 2.0 * frame_dt) seek(t);
    return decode_frame();
}

const uint8_t* HAPDecoder::pixel_data() const { return nullptr; }
uint32_t HAPDecoder::width() const { return impl_->frame_width; }
uint32_t HAPDecoder::height() const { return impl_->frame_height; }
float HAPDecoder::duration() const { return impl_->media_duration; }
void HAPDecoder::set_loop(bool loop) { impl_->loop = loop; }
void HAPDecoder::set_speed(float speed) { impl_->speed = std::max(0.01f, speed); }
float HAPDecoder::current_time() const { return impl_->current_time_s; }
bool HAPDecoder::seek(double time_seconds) {
    if (!impl_ || !impl_->opened) return false;
    const double t = std::max(0.0, time_seconds);
    impl_->current_time_s = static_cast<float>(t);
    // Allow immediate decode of the seek target frame on next decode_frame().
    impl_->last_decode = std::chrono::steady_clock::time_point{};
    impl_->reset_reader(t);
    return (impl_->reader && impl_->video_output);
}
float HAPDecoder::frame_rate() const { return impl_->frame_rate; }
uint64_t HAPDecoder::nil_frame_count() const { return impl_->nil_frame_counter; }
VideoFrameCompressionMode HAPDecoder::compression_mode() const {
    return VideoFrameCompressionMode::CompressedBC;
}
VideoCompressedFormat HAPDecoder::compressed_format() const { return impl_->format; }
bool HAPDecoder::requires_ycocg_decode() const { return impl_->ycocg_encoded; }
const uint8_t* HAPDecoder::compressed_data() const { return impl_->decoded_data.data(); }
size_t HAPDecoder::compressed_size() const { return impl_->decoded_data.size(); }

bool is_hap_video_file(const std::string& path) {
    return HAPDecoder::is_hap_file(path);
}

std::unique_ptr<VideoDecoder> create_hap_decoder() {
    return std::make_unique<HAPDecoder>();
}
