#include "runtime/av_exporter.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/CABase.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>

namespace vivid {

struct AVExporter::Impl {
    AVAssetWriter* writer = nil;
    AVAssetWriterInput* video_input = nil;
    AVAssetWriterInput* audio_input = nil;
    AVAssetWriterInputPixelBufferAdaptor* pixel_adaptor = nil;

    std::string path;
    uint32_t width = 0;
    uint32_t height = 0;
    double fps = 60.0;
    uint32_t sample_rate = 48000;

    uint64_t video_frame_count = 0;
    uint64_t audio_samples_written = 0;
    double start_time = 0.0; // CACurrentMediaTime() at recording start
    bool recording = false;
};

AVExporter::AVExporter() = default;

AVExporter::~AVExporter() {
    if (impl_) {
        if (impl_->recording)
            finish();
        delete impl_;
    }
}

bool AVExporter::start(const std::string& path, uint32_t width, uint32_t height,
                        double fps, uint32_t sample_rate) {
    if (impl_ && impl_->recording) {
        std::fprintf(stderr, "[vivid] AVExporter: already recording\n");
        return false;
    }

    if (!impl_)
        impl_ = new Impl();

    impl_->path = path;
    impl_->width = width;
    impl_->height = height;
    impl_->fps = fps;
    impl_->sample_rate = sample_rate;
    impl_->video_frame_count = 0;
    impl_->audio_samples_written = 0;

    @autoreleasepool {
        NSError* error = nil;
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];

        // Remove existing file
        [[NSFileManager defaultManager] removeItemAtURL:url error:nil];

        impl_->writer = [[AVAssetWriter alloc] initWithURL:url
                                                  fileType:AVFileTypeQuickTimeMovie
                                                     error:&error];
        if (error || !impl_->writer) {
            std::fprintf(stderr, "[vivid] AVExporter: failed to create writer: %s\n",
                         error ? error.localizedDescription.UTF8String : "unknown");
            return false;
        }

        // Video input — H.264 via VideoToolbox
        NSDictionary* video_settings = @{
            AVVideoCodecKey: AVVideoCodecTypeH264,
            AVVideoWidthKey: @(width),
            AVVideoHeightKey: @(height),
            AVVideoCompressionPropertiesKey: @{
                AVVideoAverageBitRateKey: @(width * height * 4), // ~4 bits/pixel
                AVVideoExpectedSourceFrameRateKey: @(fps),
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
            }
        };

        impl_->video_input = [[AVAssetWriterInput alloc]
            initWithMediaType:AVMediaTypeVideo
               outputSettings:video_settings];
        impl_->video_input.expectsMediaDataInRealTime = YES;

        // Pixel buffer adaptor for efficient frame submission
        NSDictionary* pb_attrs = @{
            (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (NSString*)kCVPixelBufferWidthKey: @(width),
            (NSString*)kCVPixelBufferHeightKey: @(height),
        };
        impl_->pixel_adaptor = [[AVAssetWriterInputPixelBufferAdaptor alloc]
            initWithAssetWriterInput:impl_->video_input
            sourcePixelBufferAttributes:pb_attrs];

        if ([impl_->writer canAddInput:impl_->video_input])
            [impl_->writer addInput:impl_->video_input];
        else {
            std::fprintf(stderr, "[vivid] AVExporter: cannot add video input\n");
            return false;
        }

        // Audio input — AAC (only if sample_rate > 0)
        if (sample_rate > 0) {
            AudioChannelLayout layout = {};
            layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;

            NSDictionary* audio_settings = @{
                AVFormatIDKey: @(kAudioFormatMPEG4AAC),
                AVSampleRateKey: @(sample_rate),
                AVNumberOfChannelsKey: @(2),
                AVChannelLayoutKey: [NSData dataWithBytes:&layout length:sizeof(layout)],
                AVEncoderBitRateKey: @(192000),
            };

            impl_->audio_input = [[AVAssetWriterInput alloc]
                initWithMediaType:AVMediaTypeAudio
                   outputSettings:audio_settings];
            impl_->audio_input.expectsMediaDataInRealTime = YES;

            if ([impl_->writer canAddInput:impl_->audio_input])
                [impl_->writer addInput:impl_->audio_input];
            else {
                std::fprintf(stderr, "[vivid] AVExporter: cannot add audio input\n");
                // Continue without audio
                impl_->audio_input = nil;
            }
        }

        if (![impl_->writer startWriting]) {
            std::fprintf(stderr, "[vivid] AVExporter: startWriting failed: %s\n",
                         impl_->writer.error.localizedDescription.UTF8String);
            return false;
        }

        [impl_->writer startSessionAtSourceTime:kCMTimeZero];
        impl_->start_time = CACurrentMediaTime();
        impl_->recording = true;
        std::fprintf(stderr, "[vivid] AVExporter: recording to %s (%ux%u @ %.0ffps)\n",
                     path.c_str(), width, height, fps);
        return true;
    }
}

bool AVExporter::write_video_frame(const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!impl_ || !impl_->recording) return false;

    @autoreleasepool {
        if (![impl_->video_input isReadyForMoreMediaData])
            return false;

        CVPixelBufferRef pixel_buffer = nullptr;
        CVPixelBufferPoolRef pool = impl_->pixel_adaptor.pixelBufferPool;
        if (!pool) return false;

        CVReturn status = CVPixelBufferPoolCreatePixelBuffer(nullptr, pool, &pixel_buffer);
        if (status != kCVReturnSuccess || !pixel_buffer) return false;

        CVPixelBufferLockBaseAddress(pixel_buffer, 0);
        uint8_t* dst = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(pixel_buffer));
        size_t dst_stride = CVPixelBufferGetBytesPerRow(pixel_buffer);
        size_t src_stride = width * 4;

        // Convert RGBA → BGRA
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t* src_row = rgba + y * src_stride;
            uint8_t* dst_row = dst + y * dst_stride;
            for (uint32_t x = 0; x < width; ++x) {
                dst_row[x * 4 + 0] = src_row[x * 4 + 2]; // B
                dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G
                dst_row[x * 4 + 2] = src_row[x * 4 + 0]; // R
                dst_row[x * 4 + 3] = src_row[x * 4 + 3]; // A
            }
        }
        CVPixelBufferUnlockBaseAddress(pixel_buffer, 0);

        double elapsed = CACurrentMediaTime() - impl_->start_time;
        CMTime pts = CMTimeMakeWithSeconds(elapsed, 90000); // 90kHz timescale (standard)
        bool ok = [impl_->pixel_adaptor appendPixelBuffer:pixel_buffer
                                     withPresentationTime:pts];
        CVPixelBufferRelease(pixel_buffer);

        if (ok) impl_->video_frame_count++;
        return ok;
    }
}

bool AVExporter::write_audio_samples(const float* pcm_interleaved, uint64_t sample_count,
                                      uint32_t channels) {
    if (!impl_ || !impl_->recording) return false;
    if (sample_count == 0) return true;
    if (!impl_->audio_input) return false;

    @autoreleasepool {
        if (![impl_->audio_input isReadyForMoreMediaData])
            return false;

        uint64_t frame_count = sample_count / channels;
        size_t data_size = sample_count * sizeof(float);

        // Format description for float32 interleaved PCM
        AudioStreamBasicDescription asbd = {};
        asbd.mSampleRate = impl_->sample_rate;
        asbd.mFormatID = kAudioFormatLinearPCM;
        asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        asbd.mChannelsPerFrame = channels;
        asbd.mBitsPerChannel = 32;
        asbd.mBytesPerFrame = channels * sizeof(float);
        asbd.mFramesPerPacket = 1;
        asbd.mBytesPerPacket = asbd.mBytesPerFrame;

        CMAudioFormatDescriptionRef format_desc = nullptr;
        OSStatus status = CMAudioFormatDescriptionCreate(
            kCFAllocatorDefault, &asbd, 0, nullptr,
            0, nullptr, nullptr, &format_desc);
        if (status != noErr) return false;

        // Block buffer with owned copy of PCM data
        CMBlockBufferRef block = nullptr;
        status = CMBlockBufferCreateWithMemoryBlock(
            kCFAllocatorDefault,
            nullptr, data_size, kCFAllocatorDefault,
            nullptr, 0, data_size, 0, &block);
        if (status != noErr) {
            CFRelease(format_desc);
            return false;
        }
        status = CMBlockBufferReplaceDataBytes(
            pcm_interleaved, block, 0, data_size);
        if (status != noErr) {
            CFRelease(block);
            CFRelease(format_desc);
            return false;
        }

        // Use monotonic PTS to keep audio in sync with video
        double elapsed = CACurrentMediaTime() - impl_->start_time;
        CMTime pts = CMTimeMakeWithSeconds(elapsed, 90000);

        // Create ready audio sample buffer in one call
        CMSampleBufferRef sample_buffer = nullptr;
        status = CMAudioSampleBufferCreateReadyWithPacketDescriptions(
            kCFAllocatorDefault,
            block,
            format_desc,
            static_cast<CMItemCount>(frame_count),
            pts,
            nullptr,  // no packet descriptions for PCM
            &sample_buffer);
        CFRelease(block);
        CFRelease(format_desc);
        if (status != noErr || !sample_buffer) return false;

        bool ok = [impl_->audio_input appendSampleBuffer:sample_buffer];
        CFRelease(sample_buffer);

        if (ok)
            impl_->audio_samples_written += frame_count;
        return ok;
    }
}

bool AVExporter::finish() {
    if (!impl_ || !impl_->recording) return false;

    @autoreleasepool {
        [impl_->video_input markAsFinished];
        if (impl_->audio_input)
            [impl_->audio_input markAsFinished];

        auto finished = std::make_shared<std::atomic<bool>>(false);
        [impl_->writer finishWritingWithCompletionHandler:^{
            finished->store(true, std::memory_order_release);
        }];
        // Pump the run loop while waiting — AVAssetWriter/VideoToolbox may
        // dispatch work to the main thread during finalization.
        NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:10.0];
        while (!finished->load(std::memory_order_acquire) &&
               [[NSDate date] compare:deadline] == NSOrderedAscending) {
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        }

        if (!finished->load(std::memory_order_acquire)) {
            std::fprintf(stderr, "[vivid] AVExporter: finishWriting timed out after 10s\n");
        }

        bool ok = (impl_->writer.status == AVAssetWriterStatusCompleted);
        if (!ok) {
            std::fprintf(stderr, "[vivid] AVExporter: finishWriting failed (status=%ld): %s\n",
                         (long)impl_->writer.status,
                         impl_->writer.error ?
                         impl_->writer.error.localizedDescription.UTF8String : "unknown");
        } else {
            std::fprintf(stderr, "[vivid] AVExporter: finished recording %s "
                         "(%llu video frames, %llu audio frames)\n",
                         impl_->path.c_str(),
                         impl_->video_frame_count,
                         impl_->audio_samples_written);
        }

        impl_->writer = nil;
        impl_->video_input = nil;
        impl_->audio_input = nil;
        impl_->pixel_adaptor = nil;
        impl_->recording = false;
        return ok;
    }
}

bool AVExporter::is_recording() const {
    return impl_ && impl_->recording;
}

const std::string& AVExporter::output_path() const {
    static const std::string empty;
    return impl_ ? impl_->path : empty;
}

uint64_t AVExporter::frame_count() const {
    return impl_ ? impl_->video_frame_count : 0;
}

double AVExporter::fps() const {
    return impl_ ? impl_->fps : 60.0;
}

double AVExporter::elapsed_sec() const {
    if (!impl_ || !impl_->recording) return 0.0;
    return CACurrentMediaTime() - impl_->start_time;
}

} // namespace vivid
