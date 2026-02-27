#import "avf_audio_extractor.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>

// =============================================================================
// Lock-free SPSC ring buffer for stereo audio
// =============================================================================

struct AudioRingBuffer {
    static constexpr uint32_t kCapacity = 48000;  // ~1 second at 48kHz

    float left[kCapacity];
    float right[kCapacity];
    std::atomic<uint32_t> write_pos{0};
    std::atomic<uint32_t> read_pos{0};

    double pts_base = 0.0;       // PTS (seconds) corresponding to write_pos=0
    double sample_rate = 48000;  // for PTS computation

    void clear() {
        std::memset(left, 0, sizeof(left));
        std::memset(right, 0, sizeof(right));
        write_pos.store(0, std::memory_order_relaxed);
        read_pos.store(0, std::memory_order_relaxed);
    }

    uint32_t available_read() const {
        uint32_t w = write_pos.load(std::memory_order_acquire);
        uint32_t r = read_pos.load(std::memory_order_relaxed);
        return (w - r + kCapacity) % kCapacity;
    }

    uint32_t available_write() const {
        uint32_t w = write_pos.load(std::memory_order_relaxed);
        uint32_t r = read_pos.load(std::memory_order_acquire);
        // Leave one slot empty to distinguish full from empty
        return (r - w - 1 + kCapacity) % kCapacity;
    }
};

// =============================================================================
// AVFAudioExtractor::Impl
// =============================================================================

struct AVFAudioExtractor::Impl {
    AVAsset*                  asset        = nil;
    AVAssetReader*            reader       = nil;
    AVAssetReaderTrackOutput* track_output = nil;

    AudioRingBuffer ring;
    uint32_t        target_sample_rate = 48000;
    float           media_duration     = 0.0f;
    bool            opened             = false;
    bool            has_audio_track    = false;
    bool            finished_reading   = false;

    // Total samples written since last resync (for PTS tracking)
    uint64_t        samples_written = 0;

    bool open(const std::string& path, uint32_t sample_rate) {
        @autoreleasepool {
            target_sample_rate = sample_rate;
            ring.sample_rate = sample_rate;

            NSURL* url = [NSURL fileURLWithPath:
                [NSString stringWithUTF8String:path.c_str()]];
            if (!url) return false;

            asset = [AVAsset assetWithURL:url];

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
                std::fprintf(stderr, "[avf_audio_extractor] Asset not ready\n");
                return false;
            }

            media_duration = static_cast<float>(CMTimeGetSeconds(asset.duration));

            NSArray<AVAssetTrack*>* audio_tracks = [asset tracksWithMediaType:AVMediaTypeAudio];
            if (audio_tracks.count == 0) {
                std::fprintf(stderr, "[avf_audio_extractor] No audio tracks in file\n");
                has_audio_track = false;
                opened = true;
                return true;  // Not an error — file just has no audio
            }

            has_audio_track = true;

            if (!create_reader_at_time(0.0)) {
                return false;
            }

            ring.clear();
            ring.pts_base = 0.0;
            samples_written = 0;
            opened = true;

            std::fprintf(stderr, "[avf_audio_extractor] Opened audio: %s (%.1fs, %uHz)\n",
                         path.c_str(), media_duration, target_sample_rate);
            return true;
        }
    }

    bool create_reader_at_time(double start_seconds) {
        @autoreleasepool {
            // Clean up existing reader
            reader = nil;
            track_output = nil;
            finished_reading = false;

            NSError* error = nil;
            reader = [[AVAssetReader alloc] initWithAsset:asset error:&error];
            if (error) {
                std::fprintf(stderr, "[avf_audio_extractor] AVAssetReader init failed: %s\n",
                             error.localizedDescription.UTF8String);
                return false;
            }

            NSArray<AVAssetTrack*>* audio_tracks = [asset tracksWithMediaType:AVMediaTypeAudio];
            if (audio_tracks.count == 0) return false;

            // Configure output: 32-bit float, stereo, target sample rate
            NSDictionary* settings = @{
                AVFormatIDKey:                @(kAudioFormatLinearPCM),
                AVLinearPCMBitDepthKey:       @32,
                AVLinearPCMIsFloatKey:        @YES,
                AVLinearPCMIsNonInterleaved:  @NO,
                AVLinearPCMIsBigEndianKey:    @NO,
                AVNumberOfChannelsKey:        @2,
                AVSampleRateKey:              @(target_sample_rate),
            };

            track_output = [AVAssetReaderTrackOutput
                assetReaderTrackOutputWithTrack:audio_tracks[0]
                outputSettings:settings];
            track_output.alwaysCopiesSampleData = NO;

            [reader addOutput:track_output];

            // Set time range from start_seconds to end of file
            CMTime start = CMTimeMakeWithSeconds(start_seconds, 600);
            CMTime duration = CMTimeSubtract(asset.duration, start);
            if (CMTimeCompare(duration, kCMTimeZero) <= 0) {
                duration = kCMTimeZero;
            }
            reader.timeRange = CMTimeRangeMake(start, duration);

            if (![reader startReading]) {
                std::fprintf(stderr, "[avf_audio_extractor] startReading failed: %s\n",
                             reader.error.localizedDescription.UTF8String);
                return false;
            }

            return true;
        }
    }

    void close() {
        @autoreleasepool {
            if (reader && reader.status == AVAssetReaderStatusReading) {
                [reader cancelReading];
            }
            reader = nil;
            track_output = nil;
            asset = nil;
            ring.clear();
            opened = false;
            has_audio_track = false;
            media_duration = 0.0f;
            samples_written = 0;
        }
    }

    void fill_buffer() {
        @autoreleasepool {
            if (!opened || !has_audio_track || finished_reading) return;
            if (!reader || reader.status != AVAssetReaderStatusReading) return;

            // Keep ~0.5 seconds ahead of read position
            static constexpr uint32_t kTargetAhead = 24000;  // 0.5s at 48kHz
            uint32_t avail = ring.available_read();
            if (avail >= kTargetAhead) return;

            uint32_t to_fill = kTargetAhead - avail;

            while (to_fill > 0) {
                CMSampleBufferRef sample_buf = [track_output copyNextSampleBuffer];
                if (!sample_buf) {
                    if (reader.status == AVAssetReaderStatusCompleted) {
                        finished_reading = true;
                    }
                    break;
                }

                CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample_buf);
                size_t data_length = 0;
                char* data_ptr = nullptr;
                CMBlockBufferGetDataPointer(block, 0, nullptr, &data_length, &data_ptr);

                CMItemCount num_samples = CMSampleBufferGetNumSamples(sample_buf);
                uint32_t frames = static_cast<uint32_t>(num_samples);

                // Data is interleaved stereo float32: [L R L R ...]
                const float* interleaved = reinterpret_cast<const float*>(data_ptr);

                uint32_t can_write = ring.available_write();
                uint32_t to_write = std::min(frames, can_write);

                uint32_t wp = ring.write_pos.load(std::memory_order_relaxed);
                for (uint32_t i = 0; i < to_write; ++i) {
                    uint32_t idx = (wp + i) % AudioRingBuffer::kCapacity;
                    ring.left[idx]  = interleaved[i * 2];
                    ring.right[idx] = interleaved[i * 2 + 1];
                }
                ring.write_pos.store((wp + to_write) % AudioRingBuffer::kCapacity,
                                     std::memory_order_release);
                samples_written += to_write;

                if (to_write < frames) {
                    to_fill = 0;  // Ring buffer full
                } else {
                    to_fill = (to_fill > to_write) ? (to_fill - to_write) : 0;
                }

                CFRelease(sample_buf);
            }
        }
    }

    void resync(double time_seconds) {
        @autoreleasepool {
            if (!opened || !has_audio_track) return;

            ring.clear();
            ring.pts_base = time_seconds;
            samples_written = 0;

            create_reader_at_time(time_seconds);

            std::fprintf(stderr, "[avf_audio_extractor] Resync to %.3fs\n", time_seconds);
        }
    }

    uint32_t read_samples(float* left_out, float* right_out, uint32_t max_frames) {
        uint32_t avail = ring.available_read();
        uint32_t to_read = std::min(max_frames, avail);
        uint32_t rp = ring.read_pos.load(std::memory_order_relaxed);

        for (uint32_t i = 0; i < to_read; ++i) {
            uint32_t idx = (rp + i) % AudioRingBuffer::kCapacity;
            left_out[i]  = ring.left[idx];
            right_out[i] = ring.right[idx];
        }

        // Zero-fill remainder on underrun
        if (to_read < max_frames) {
            std::memset(left_out + to_read, 0, (max_frames - to_read) * sizeof(float));
            std::memset(right_out + to_read, 0, (max_frames - to_read) * sizeof(float));
        }

        ring.read_pos.store((rp + to_read) % AudioRingBuffer::kCapacity,
                            std::memory_order_release);
        return to_read;
    }

    double read_head_pts() const {
        // Compute PTS from how many samples the read head has consumed since pts_base
        uint32_t rp = ring.read_pos.load(std::memory_order_relaxed);
        uint32_t wp = ring.write_pos.load(std::memory_order_relaxed);

        // Total samples consumed = samples_written - samples still in buffer
        uint32_t in_buffer = (wp - rp + AudioRingBuffer::kCapacity) % AudioRingBuffer::kCapacity;
        uint64_t consumed = (samples_written > in_buffer) ? (samples_written - in_buffer) : 0;

        return ring.pts_base + static_cast<double>(consumed) / ring.sample_rate;
    }
};

// =============================================================================
// AVFAudioExtractor public interface
// =============================================================================

AVFAudioExtractor::AVFAudioExtractor() : impl_(std::make_unique<Impl>()) {}
AVFAudioExtractor::~AVFAudioExtractor() { close(); }

bool AVFAudioExtractor::open(const std::string& path, uint32_t target_sample_rate) {
    return impl_->open(path, target_sample_rate);
}
void AVFAudioExtractor::close() { if (impl_) impl_->close(); }
bool AVFAudioExtractor::is_open() const { return impl_ && impl_->opened; }
bool AVFAudioExtractor::has_audio() const { return impl_ && impl_->has_audio_track; }
float AVFAudioExtractor::duration() const { return impl_ ? impl_->media_duration : 0.0f; }
void AVFAudioExtractor::fill_buffer() { impl_->fill_buffer(); }
void AVFAudioExtractor::resync(double time_seconds) { impl_->resync(time_seconds); }
uint32_t AVFAudioExtractor::read_samples(float* left, float* right, uint32_t max_frames) {
    return impl_->read_samples(left, right, max_frames);
}
double AVFAudioExtractor::read_head_pts() const { return impl_->read_head_pts(); }
