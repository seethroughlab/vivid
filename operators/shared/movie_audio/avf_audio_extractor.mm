#import "avf_audio_extractor.h"

#import <AVFoundation/AVFoundation.h>
#import <AVFAudio/AVFAudio.h>
#import <CoreMedia/CoreMedia.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

// =============================================================================
// Lock-free SPSC ring buffer for stereo audio
// =============================================================================

struct AudioRingBuffer {
    static constexpr uint32_t kCapacity = 48000;  // ~1 second at 48kHz

    float left[kCapacity];
    float right[kCapacity];
    std::atomic<uint32_t> write_pos{0};
    std::atomic<uint32_t> read_pos{0};

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
// AVFAudioExtractor::Impl — AVAudioEngine manual rendering pipeline
// =============================================================================

struct AVFAudioExtractor::Impl {
    // Asset / reader state
    AVAsset*                  asset        = nil;
    AVAssetReader*            reader       = nil;
    AVAssetReaderTrackOutput* track_output = nil;

    // AVAudioEngine pipeline for pitch-preserving time stretch
    AVAudioEngine*            engine       = nil;
    AVAudioPlayerNode*        playerNode   = nil;
    AVAudioUnitTimePitch*     timePitch    = nil;
    AVAudioFormat*            processingFormat = nil;

    AudioRingBuffer ring;
    uint32_t        target_sample_rate = 48000;
    float           media_duration     = 0.0f;
    bool            opened             = false;
    bool            has_audio_track    = false;
    bool            finished_reading   = false;
    bool            loop_enabled       = true;

    // Speed / PTS tracking
    std::atomic<float>  current_speed{1.0f};
    double              media_time_written = 0.0;   // media time corresponding to ring write head
    std::atomic<double> read_head_media_time{0.0};  // media time at ring buffer read head

    static constexpr uint32_t kRenderFrameCount  = 4096;
    static constexpr uint32_t kTargetAhead       = 24000;  // 0.5s at 48kHz
    static constexpr uint32_t kTimePitchLatency  = 4096;   // AVAudioUnitTimePitch look-ahead

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

            // Set up AVAudioEngine for manual offline rendering
            if (!setup_engine()) {
                return false;
            }

            if (!create_reader_at_time(0.0)) {
                return false;
            }

            ring.clear();
            media_time_written = 0.0;
            read_head_media_time.store(0.0, std::memory_order_relaxed);
            opened = true;

            std::fprintf(stderr, "[avf_audio_extractor] Opened audio: %s (%.1fs, %uHz)\n",
                         path.c_str(), media_duration, target_sample_rate);

            // Pre-fill ring buffer so audio thread doesn't read silence initially
            fill_buffer();

            return true;
        }
    }

    bool setup_engine() {
        @autoreleasepool {
            // Create processing format: 48kHz stereo non-interleaved float
            processingFormat = [[AVAudioFormat alloc]
                initStandardFormatWithSampleRate:target_sample_rate
                channels:2];

            engine = [[AVAudioEngine alloc] init];
            playerNode = [[AVAudioPlayerNode alloc] init];
            timePitch = [[AVAudioUnitTimePitch alloc] init];
            timePitch.rate = current_speed;

            [engine attachNode:playerNode];
            [engine attachNode:timePitch];

            [engine connect:playerNode to:timePitch format:processingFormat];
            [engine connect:timePitch to:engine.mainMixerNode format:processingFormat];

            // Enable manual offline rendering
            NSError* error = nil;
            BOOL ok = [engine enableManualRenderingMode:AVAudioEngineManualRenderingModeOffline
                                                format:processingFormat
                                     maximumFrameCount:kRenderFrameCount
                                                 error:&error];
            if (!ok) {
                std::fprintf(stderr, "[avf_audio_extractor] enableManualRenderingMode failed: %s\n",
                             error.localizedDescription.UTF8String);
                return false;
            }

            if (![engine startAndReturnError:&error]) {
                std::fprintf(stderr, "[avf_audio_extractor] engine start failed: %s\n",
                             error.localizedDescription.UTF8String);
                return false;
            }

            [playerNode play];
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

            // Configure output: 32-bit float, stereo, target sample rate, non-interleaved
            // (AVAudioPCMBuffer expects non-interleaved for standard format)
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

    // Decode a chunk of PCM from AVAssetReader, wrap in AVAudioPCMBuffer,
    // and schedule it on the playerNode.
    // Returns number of frames scheduled, or 0 if no more data.
    uint32_t decode_and_schedule() {
        @autoreleasepool {
            if (finished_reading) return 0;
            if (!reader || reader.status != AVAssetReaderStatusReading) return 0;

            CMSampleBufferRef sample_buf = [track_output copyNextSampleBuffer];
            if (!sample_buf) {
                if (reader.status == AVAssetReaderStatusCompleted) {
                    if (loop_enabled) {
                        if (!create_reader_at_time(0.0)) {
                            finished_reading = true;
                            return 0;
                        }
                        sample_buf = [track_output copyNextSampleBuffer];
                        if (!sample_buf) {
                            finished_reading = true;
                            return 0;
                        }
                    } else {
                        finished_reading = true;
                        return 0;
                    }
                } else {
                    return 0;
                }
            }

            CMItemCount num_samples = CMSampleBufferGetNumSamples(sample_buf);
            uint32_t frames = static_cast<uint32_t>(num_samples);

            // Get raw interleaved data
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample_buf);
            size_t data_length = 0;
            char* data_ptr = nullptr;
            CMBlockBufferGetDataPointer(block, 0, nullptr, &data_length, &data_ptr);
            const float* interleaved = reinterpret_cast<const float*>(data_ptr);

            // Create AVAudioPCMBuffer in standard (non-interleaved) format
            AVAudioPCMBuffer* pcmBuffer = [[AVAudioPCMBuffer alloc]
                initWithPCMFormat:processingFormat frameCapacity:frames];
            pcmBuffer.frameLength = frames;

            // Deinterleave: [L R L R ...] → separate L and R channel buffers
            float* chL = pcmBuffer.floatChannelData[0];
            float* chR = pcmBuffer.floatChannelData[1];
            for (uint32_t i = 0; i < frames; ++i) {
                chL[i] = interleaved[i * 2];
                chR[i] = interleaved[i * 2 + 1];
            }

            // Schedule on player node (non-blocking)
            [playerNode scheduleBuffer:pcmBuffer completionHandler:nil];

            CFRelease(sample_buf);
            return frames;
        }
    }

    void fill_buffer() {
        @autoreleasepool {
            if (!opened || !has_audio_track) return;
            if (!engine) return;

            // Scale target ahead by speed — at higher speeds, keep more buffered
            float speed = current_speed.load(std::memory_order_relaxed);
            uint32_t target_ahead = std::min(AudioRingBuffer::kCapacity - 1,
                static_cast<uint32_t>(kTargetAhead * std::max(speed, 1.0f)));
            uint32_t avail = ring.available_read();
            if (avail >= target_ahead) return;

            uint32_t frames_needed = target_ahead - avail;

            // Pre-feed: account for speed and time-pitch algorithm look-ahead
            uint32_t input_needed = static_cast<uint32_t>(
                std::ceil(frames_needed * std::max(speed, 1.0f))) + kTimePitchLatency;
            uint32_t input_fed = 0;
            while (input_fed < input_needed) {
                uint32_t fed = decode_and_schedule();
                if (fed == 0) break;  // EOF or error
                input_fed += fed;
            }

            // Render loop: pull time-stretched output from engine
            AVAudioPCMBuffer* outputBuffer = [[AVAudioPCMBuffer alloc]
                initWithPCMFormat:processingFormat frameCapacity:kRenderFrameCount];

            uint32_t total_rendered = 0;
            while (total_rendered < frames_needed) {
                NSError* error = nil;
                uint32_t chunk = std::min(kRenderFrameCount, frames_needed - total_rendered);

                AVAudioEngineManualRenderingStatus status =
                    [engine renderOffline:chunk toBuffer:outputBuffer error:&error];

                if (status == AVAudioEngineManualRenderingStatusSuccess) {
                    uint32_t rendered = outputBuffer.frameLength;
                    if (rendered == 0) break;

                    // Write rendered output to ring buffer
                    uint32_t can_write = ring.available_write();
                    uint32_t to_write = std::min(rendered, can_write);

                    const float* outL = outputBuffer.floatChannelData[0];
                    const float* outR = outputBuffer.floatChannelData[1];
                    uint32_t wp = ring.write_pos.load(std::memory_order_relaxed);

                    for (uint32_t i = 0; i < to_write; ++i) {
                        uint32_t idx = (wp + i) % AudioRingBuffer::kCapacity;
                        ring.left[idx]  = outL[i];
                        ring.right[idx] = outR[i];
                    }
                    ring.write_pos.store((wp + to_write) % AudioRingBuffer::kCapacity,
                                         std::memory_order_release);

                    // Update media time tracking:
                    // Each rendered output frame represents current_speed/sample_rate seconds
                    // of media time (because time-pitch speeds up playback)
                    media_time_written += static_cast<double>(to_write) * speed / ring.sample_rate;

                    total_rendered += to_write;

                    if (to_write < rendered) break;  // Ring buffer full
                } else if (status == AVAudioEngineManualRenderingStatusInsufficientDataFromInputNode) {
                    // Need more input data — feed multiple chunks (higher speeds burn through faster)
                    uint32_t extra_fed = 0;
                    while (extra_fed < kRenderFrameCount * 2) {
                        uint32_t fed = decode_and_schedule();
                        if (fed == 0) break;  // EOF
                        extra_fed += fed;
                    }
                    if (extra_fed == 0) break;  // No more data available
                } else {
                    // Error or other status
                    break;
                }
            }
        }
    }

    void set_speed(float speed) {
        current_speed = speed;
        if (timePitch) {
            // AVAudioUnitTimePitch.rate range: 1/32 to 32.0
            timePitch.rate = std::clamp(speed, 1.0f / 32.0f, 32.0f);
        }
    }

    void set_loop(bool loop) { loop_enabled = loop; }

    void resync(double time_seconds) {
        @autoreleasepool {
            if (!opened || !has_audio_track) return;

            ring.clear();
            media_time_written = time_seconds;
            read_head_media_time.store(time_seconds, std::memory_order_relaxed);

            // Reset engine state to clear time-pitch internal buffers
            if (playerNode) {
                [playerNode stop];
            }
            if (engine) {
                [engine reset];
            }
            if (playerNode) {
                [playerNode play];
            }

            create_reader_at_time(time_seconds);

            std::fprintf(stderr, "[avf_audio_extractor] Resync to %.3fs\n", time_seconds);

            // Pre-fill ring buffer so audio thread doesn't read silence
            fill_buffer();
        }
    }

    void close() {
        @autoreleasepool {
            // Tear down engine
            if (playerNode) {
                [playerNode stop];
            }
            if (engine) {
                [engine stop];
            }
            engine = nil;
            playerNode = nil;
            timePitch = nil;
            processingFormat = nil;

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
            media_time_written = 0.0;
            read_head_media_time.store(0.0, std::memory_order_relaxed);
            current_speed = 1.0f;
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

        // Advance read head media time: each consumed output frame represents
        // current_speed/sample_rate seconds of media time
        if (to_read > 0) {
            float speed = current_speed.load(std::memory_order_relaxed);
            double advance = static_cast<double>(to_read) * speed / ring.sample_rate;
            double old_time = read_head_media_time.load(std::memory_order_relaxed);
            read_head_media_time.store(old_time + advance, std::memory_order_relaxed);
        }

        return to_read;
    }

    uint32_t discard_samples(uint32_t max_frames) {
        uint32_t avail = ring.available_read();
        uint32_t to_drop = std::min(max_frames, avail);
        if (to_drop == 0) return 0;

        uint32_t rp = ring.read_pos.load(std::memory_order_relaxed);
        ring.read_pos.store((rp + to_drop) % AudioRingBuffer::kCapacity,
                            std::memory_order_release);

        float speed = current_speed.load(std::memory_order_relaxed);
        double advance = static_cast<double>(to_drop) * speed / ring.sample_rate;
        double old_time = read_head_media_time.load(std::memory_order_relaxed);
        read_head_media_time.store(old_time + advance, std::memory_order_relaxed);
        return to_drop;
    }

    double read_head_pts() const {
        return read_head_media_time.load(std::memory_order_relaxed);
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
void AVFAudioExtractor::set_speed(float speed) { impl_->set_speed(speed); }
void AVFAudioExtractor::set_loop(bool loop) { impl_->set_loop(loop); }
void AVFAudioExtractor::fill_buffer() { impl_->fill_buffer(); }
void AVFAudioExtractor::resync(double time_seconds) { impl_->resync(time_seconds); }
uint32_t AVFAudioExtractor::read_samples(float* left, float* right, uint32_t max_frames) {
    return impl_->read_samples(left, right, max_frames);
}
uint32_t AVFAudioExtractor::discard_samples(uint32_t max_frames) {
    return impl_->discard_samples(max_frames);
}
double AVFAudioExtractor::read_head_pts() const { return impl_->read_head_pts(); }
