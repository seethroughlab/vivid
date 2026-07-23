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
// AVFAudioExtractor::Impl — decodes directly into caller buffers
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

    uint32_t        target_sample_rate = 48000;
    float           media_duration     = 0.0f;
    bool            opened             = false;
    bool            has_audio_track    = false;
    bool            finished_reading   = false;
    bool            loop_enabled       = true;
    bool            pitch_preserve_    = true;

    // Speed / PTS tracking
    std::atomic<float>  current_speed{1.0f};
    double              media_time_written = 0.0;

    static constexpr uint32_t kRenderFrameCount  = 4096;
    static constexpr uint32_t kTimePitchLatency  = 4096;
    // Residual buffer: TimePitch may produce more frames than the caller wants
    // in a single render pass. We store the leftovers here.
    float residual_left[kRenderFrameCount];
    float residual_right[kRenderFrameCount];
    uint32_t residual_count = 0;
    uint32_t residual_offset = 0;

    // NOTE: TimePitch latency compensation (~85ms) was attempted here but
    // reverted — the step-function transition when the pipeline primes
    // interacts with the AV sync seek threshold (~83ms at 24fps), causing
    // oscillation. Proper fix requires proportional video speed adjustment
    // (see "What We'd Do Differently" in docs/movie-player-fixes.md).

    bool open(const std::string& path, uint32_t sample_rate) {
        @autoreleasepool {
            target_sample_rate = sample_rate;

            NSURL* url = [NSURL fileURLWithPath:
                [NSString stringWithUTF8String:path.c_str()]];
            if (!url) return false;

            asset = [AVAsset assetWithURL:url];

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
                return true;
            }

            has_audio_track = true;

            if (pitch_preserve_) {
                if (!setup_engine()) return false;
            }

            if (!create_reader_at_time(0.0)) return false;

            media_time_written = 0.0;
            residual_count = 0;
            residual_offset = 0;
            opened = true;

            std::fprintf(stderr, "[avf_audio_extractor] Opened audio: %s (%.1fs, %uHz, pitch_preserve=%d)\n",
                         path.c_str(), media_duration, target_sample_rate, pitch_preserve_ ? 1 : 0);
            return true;
        }
    }

    bool setup_engine() {
        @autoreleasepool {
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

    void teardown_engine() {
        @autoreleasepool {
            if (playerNode) [playerNode stop];
            if (engine) [engine stop];
            engine = nil;
            playerNode = nil;
            timePitch = nil;
        }
    }

    bool create_reader_at_time(double start_seconds) {
        @autoreleasepool {
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

    // Decode a chunk from AVAssetReader into an AVAudioPCMBuffer and schedule
    // on the player node (for TimePitch path).
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

            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample_buf);
            size_t data_length = 0;
            char* data_ptr = nullptr;
            CMBlockBufferGetDataPointer(block, 0, nullptr, &data_length, &data_ptr);
            const float* interleaved = reinterpret_cast<const float*>(data_ptr);

            if (!processingFormat) {
                processingFormat = [[AVAudioFormat alloc]
                    initStandardFormatWithSampleRate:target_sample_rate
                    channels:2];
            }

            AVAudioPCMBuffer* pcmBuffer = [[AVAudioPCMBuffer alloc]
                initWithPCMFormat:processingFormat frameCapacity:frames];
            pcmBuffer.frameLength = frames;

            float* chL = pcmBuffer.floatChannelData[0];
            float* chR = pcmBuffer.floatChannelData[1];
            for (uint32_t i = 0; i < frames; ++i) {
                chL[i] = interleaved[i * 2];
                chR[i] = interleaved[i * 2 + 1];
            }

            [playerNode scheduleBuffer:pcmBuffer completionHandler:nil];

            CFRelease(sample_buf);
            return frames;
        }
    }

    // Decode raw PCM directly (bypass TimePitch), deinterleave into L/R.
    // Returns frames decoded (0 at EOF).
    uint32_t decode_raw(float* left, float* right, uint32_t max_frames) {
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
            uint32_t frames = static_cast<uint32_t>(std::min(static_cast<CMItemCount>(max_frames), num_samples));

            CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample_buf);
            size_t data_length = 0;
            char* data_ptr = nullptr;
            CMBlockBufferGetDataPointer(block, 0, nullptr, &data_length, &data_ptr);
            const float* interleaved = reinterpret_cast<const float*>(data_ptr);

            for (uint32_t i = 0; i < frames; ++i) {
                left[i]  = interleaved[i * 2];
                right[i] = interleaved[i * 2 + 1];
            }

            CFRelease(sample_buf);

            float speed = current_speed.load(std::memory_order_relaxed);
            media_time_written += static_cast<double>(frames) * speed / target_sample_rate;

            return frames;
        }
    }

    // Decode samples using the TimePitch engine pipeline.
    uint32_t decode_with_timepitch(float* left, float* right, uint32_t max_frames) {
        @autoreleasepool {
            if (!engine) return 0;

            uint32_t total_out = 0;
            float speed = current_speed.load(std::memory_order_relaxed);

            // First drain any residual from previous render pass
            if (residual_count > 0) {
                uint32_t from_residual = std::min(max_frames, residual_count);
                std::memcpy(left, residual_left + residual_offset, from_residual * sizeof(float));
                std::memcpy(right, residual_right + residual_offset, from_residual * sizeof(float));
                residual_count -= from_residual;
                residual_offset += from_residual;
                media_time_written += static_cast<double>(from_residual) * speed / target_sample_rate;
                total_out += from_residual;
                if (total_out >= max_frames) return total_out;
            }

            // Feed input and render in chunks
            while (total_out < max_frames) {
                uint32_t remaining = max_frames - total_out;

                // Pre-feed: schedule enough input for the engine
                uint32_t input_needed = static_cast<uint32_t>(
                    std::ceil(remaining * std::max(speed, 1.0f))) + kTimePitchLatency;
                uint32_t input_fed = 0;
                while (input_fed < input_needed) {
                    uint32_t fed = decode_and_schedule();
                    if (fed == 0) break;
                    input_fed += fed;
                }

                AVAudioPCMBuffer* outputBuffer = [[AVAudioPCMBuffer alloc]
                    initWithPCMFormat:processingFormat frameCapacity:kRenderFrameCount];

                uint32_t chunk = std::min(kRenderFrameCount, remaining);
                NSError* error = nil;
                AVAudioEngineManualRenderingStatus status =
                    [engine renderOffline:chunk toBuffer:outputBuffer error:&error];

                if (status == AVAudioEngineManualRenderingStatusSuccess) {
                    uint32_t rendered = outputBuffer.frameLength;
                    if (rendered == 0) break;

                    const float* outL = outputBuffer.floatChannelData[0];
                    const float* outR = outputBuffer.floatChannelData[1];

                    uint32_t to_copy = std::min(rendered, max_frames - total_out);
                    std::memcpy(left + total_out, outL, to_copy * sizeof(float));
                    std::memcpy(right + total_out, outR, to_copy * sizeof(float));

                    // Store residual if we rendered more than needed
                    if (rendered > to_copy) {
                        uint32_t leftover = rendered - to_copy;
                        std::memcpy(residual_left, outL + to_copy, leftover * sizeof(float));
                        std::memcpy(residual_right, outR + to_copy, leftover * sizeof(float));
                        residual_count = leftover;
                        residual_offset = 0;
                    }

                    media_time_written += static_cast<double>(to_copy) * speed / target_sample_rate;
                    total_out += to_copy;
                } else if (status == AVAudioEngineManualRenderingStatusInsufficientDataFromInputNode) {
                    uint32_t extra_fed = 0;
                    while (extra_fed < kRenderFrameCount * 2) {
                        uint32_t fed = decode_and_schedule();
                        if (fed == 0) break;
                        extra_fed += fed;
                    }
                    if (extra_fed == 0) break;
                } else {
                    break;
                }
            }

            return total_out;
        }
    }

    uint32_t decode_samples(float* left, float* right, uint32_t max_frames) {
        if (!opened || !has_audio_track) return 0;
        if (max_frames == 0) return 0;

        if (pitch_preserve_) {
            return decode_with_timepitch(left, right, max_frames);
        } else {
            return decode_raw(left, right, max_frames);
        }
    }

    void set_speed(float speed) {
        current_speed = speed;
        if (timePitch) {
            timePitch.rate = std::clamp(speed, 1.0f / 32.0f, 32.0f);
        }
    }

    void set_loop(bool loop) { loop_enabled = loop; }

    void set_pitch_preserve(bool preserve) {
        if (pitch_preserve_ == preserve) return;
        pitch_preserve_ = preserve;
        if (!opened || !has_audio_track) return;
        @autoreleasepool {
            if (preserve && !engine) {
                setup_engine();
            } else if (!preserve && engine) {
                teardown_engine();
            }
        }
    }

    void resync(double time_seconds) {
        @autoreleasepool {
            if (!opened || !has_audio_track) return;

            media_time_written = time_seconds;
            residual_count = 0;
            residual_offset = 0;

            if (playerNode) [playerNode stop];
            if (engine) [engine reset];
            if (playerNode) [playerNode play];

            create_reader_at_time(time_seconds);

            std::fprintf(stderr, "[avf_audio_extractor] Resync to %.3fs\n", time_seconds);
        }
    }

    void close() {
        @autoreleasepool {
            teardown_engine();
            processingFormat = nil;

            if (reader && reader.status == AVAssetReaderStatusReading) {
                [reader cancelReading];
            }
            reader = nil;
            track_output = nil;
            asset = nil;
            opened = false;
            has_audio_track = false;
            media_duration = 0.0f;
            media_time_written = 0.0;
            current_speed = 1.0f;
            residual_count = 0;
            residual_offset = 0;
        }
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
void AVFAudioExtractor::set_pitch_preserve(bool preserve) { impl_->set_pitch_preserve(preserve); }
bool AVFAudioExtractor::pitch_preserve() const { return impl_ ? impl_->pitch_preserve_ : true; }
uint32_t AVFAudioExtractor::decode_samples(float* left, float* right, uint32_t max_frames) {
    return impl_->decode_samples(left, right, max_frames);
}
void AVFAudioExtractor::resync(double time_seconds) { impl_->resync(time_seconds); }
double AVFAudioExtractor::write_head_pts() const { return impl_ ? impl_->media_time_written : 0.0; }
