#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/media_clock.h"
#include "operator_api/media_stream.h"
#include "../../shared/media_session/media_session.h"
#include "../../shared/movie_audio/avf_audio_extractor.h"

#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// =============================================================================
// AudioFillThread — dedicated thread that fills the session ring buffer
// from the AVFAudioExtractor, independent of GPU frame rate.
// =============================================================================

class AudioFillThread {
public:
    void start(vivid::media::MediaSession* session, AVFAudioExtractor* extractor) {
        stop();
        session_.store(session, std::memory_order_release);
        extractor_.store(extractor, std::memory_order_release);
        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this]() { run(); });
    }

    void stop() {
        if (!running_.load(std::memory_order_acquire)) return;
        running_.store(false, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mu_);
            wake_.notify_one();
        }
        if (thread_.joinable()) thread_.join();
    }

    void update_session(vivid::media::MediaSession* session) {
        session_.store(session, std::memory_order_release);
    }

    void update_extractor(AVFAudioExtractor* extractor) {
        extractor_.store(extractor, std::memory_order_release);
    }

    void notify() {
        std::lock_guard<std::mutex> lock(mu_);
        wake_.notify_one();
    }

    ~AudioFillThread() { stop(); }

private:
    static constexpr uint32_t kFillChunk = 2048;
    // Target: keep ring ~2s ahead (at 48kHz)
    static constexpr uint32_t kTargetAhead = 96000;
    // Pre-roll threshold: 0.5s at 48kHz
    static constexpr uint32_t kPrerollFrames = 24000;

    void run() {
        while (running_.load(std::memory_order_acquire)) {
            pump();
            std::unique_lock<std::mutex> lock(mu_);
            wake_.wait_for(lock, std::chrono::milliseconds(5));
        }
    }

    void pump() {
        auto* session = session_.load(std::memory_order_acquire);
        auto* ext = extractor_.load(std::memory_order_acquire);
        if (!session || !ext || !ext->is_open() || !ext->has_audio()) return;

        bool any_written = false;
        uint32_t total_written = 0;

        while (true) {
            const uint32_t avail_read = vivid::media::media_session_audio_available_read(*session);
            if (avail_read >= kTargetAhead) break;

            const uint32_t writable = vivid::media::media_session_audio_available_write(*session);
            if (writable == 0) break;

            const uint32_t chunk = std::min<uint32_t>(writable, kFillChunk);
            const uint32_t got = ext->decode_samples(fill_left_.data(), fill_right_.data(), chunk);
            if (got == 0) break;

            vivid::media::media_session_audio_write(*session,
                                                     fill_left_.data(),
                                                     fill_right_.data(),
                                                     got);
            any_written = true;
            total_written += got;
        }

        if (any_written) {
            session->audio_write_head_media_time.store(ext->write_head_pts(), std::memory_order_release);
        }

        // Signal pre-roll ready once we have enough buffered
        if (session->audio_preroll_ready.load(std::memory_order_relaxed) == 0) {
            const uint32_t depth = vivid::media::media_session_audio_available_read(*session);
            if (depth >= kPrerollFrames) {
                const double rh = session->audio_read_head_media_time.load(std::memory_order_relaxed);
                const double wh = session->audio_write_head_media_time.load(std::memory_order_relaxed);
                std::fprintf(stderr,
                    "[MAO fill] PREROLL_READY  depth=%u  rh=%.4f  wh=%.4f\n",
                    depth, rh, wh);
                session->audio_preroll_ready.store(1, std::memory_order_release);
            }
        }
    }

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<vivid::media::MediaSession*> session_{nullptr};
    std::atomic<AVFAudioExtractor*> extractor_{nullptr};
    std::mutex mu_;
    std::condition_variable wake_;
    std::array<float, kFillChunk> fill_left_{};
    std::array<float, kFillChunk> fill_right_{};
};

// =============================================================================
// MovieAudioOut operator
// =============================================================================

struct MovieAudioOut : vivid::OperatorBase {
    static constexpr const char* kName   = "MovieAudioOut";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> volume {"volume", 1.0f, 0.0f, 2.0f};
    vivid::Param<int> pitch_preserve {"pitch_preserve", 1, {"Off", "On"}};

    MovieAudioOut() {
        vivid::semantic_tag(volume, "amplitude_linear");
        vivid::semantic_shape(volume, "scalar");
        vivid::semantic_tag(pitch_preserve, "x_pitch_preserve");
        vivid::semantic_shape(pitch_preserve, "enum");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&volume);
        out.push_back(&pitch_preserve);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"media_stream", VIVID_PORT_DATA, VIVID_PORT_INPUT, "media_stream_v1"});
        out.push_back({"left",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"right", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    struct MediaClockSnapshot {
        bool valid = false;
        vivid::MediaClockV1 clock{};
        uint64_t handle_id = 0;
        uint64_t session_ptr = 0;
    };

    static constexpr uint32_t kInputPortMediaStream = 0;

    static MediaClockSnapshot read_media_clock(const VividProcessContext* ctx) {
        MediaClockSnapshot s{};
        if (!ctx || !ctx->input_data) return s;
        if (ctx->input_data_count <= kInputPortMediaStream) return s;
        void* ptr = ctx->input_data[kInputPortMediaStream];
        if (!ptr) return s;
        const auto* stream = static_cast<const vivid::MediaStreamV1*>(ptr);
        s.clock = stream->clock;
        s.handle_id = stream->handle_id;
        s.session_ptr = stream->session_ptr;
        s.valid = true;
        return s;
    }

    // Main thread: called via update_sources hook each frame
    void main_thread_update(double /*time*/) override {
        const auto* hs = shared_handles_.load(std::memory_order_acquire);

        // Deferred delete: by now the audio thread has observed the new pointer
        delete deferred_delete_;
        deferred_delete_ = nullptr;
        deferred_session_releases_.clear();

        // Attach/detach shared media-stream handle (from MovieLoaded output data).
        const uint64_t pending_session_ptr = pending_stream_ptr_.exchange(0, std::memory_order_acq_rel);
        if (pending_session_ptr != 0) {
            // Verify the session is still valid via shared handles
            const uint64_t handle = pending_stream_handle_.load(std::memory_order_acquire);
            bool valid = false;
            if (hs && handle != 0) {
                auto entry = hs->resolve(handle);
                valid = entry.valid && entry.payload == reinterpret_cast<void*>(pending_session_ptr);
            }
            if (valid) {
                active_session_ = reinterpret_cast<vivid::media::MediaSession*>(pending_session_ptr);
                active_session_ptr_.store(active_session_, std::memory_order_release);
                active_stream_handle_ = handle;
            } else {
                // Session was destroyed — null everything out
                fill_thread_.update_session(nullptr);
                fill_thread_.update_extractor(nullptr);
                extractor_.store(nullptr, std::memory_order_release);
                if (session_extractor_) {
                    deferred_session_releases_.push_back(std::move(session_extractor_));
                }
                session_extractor_.reset();
                active_session_ = nullptr;
                active_session_ptr_.store(nullptr, std::memory_order_release);
            }
        } else {
            const uint64_t pending_handle = pending_stream_handle_.load(std::memory_order_acquire);
            if (pending_handle != active_stream_handle_) {
                if (hs && active_stream_handle_ != 0) {
                    hs->release(active_stream_handle_);
                    active_stream_handle_ = 0;
                }
                if (hs && pending_handle != 0 &&
                    hs->retain(pending_handle)) {
                    active_stream_handle_ = pending_handle;
                }
            }
            // Validate existing session is still alive via its handle.
            // The session may have been destroyed by MovieLoaded since last frame.
            if (active_session_ && hs && active_stream_handle_ != 0) {
                auto entry = hs->resolve(active_stream_handle_);
                if (!entry.valid || entry.payload != active_session_) {
                    fill_thread_.update_session(nullptr);
                    fill_thread_.update_extractor(nullptr);
                    extractor_.store(nullptr, std::memory_order_release);
                    if (session_extractor_) {
                        deferred_session_releases_.push_back(std::move(session_extractor_));
                    }
                    session_extractor_.reset();
                    active_session_ = nullptr;
                    active_session_ptr_.store(nullptr, std::memory_order_release);
                    hs->release(active_stream_handle_);
                    active_stream_handle_ = 0;
                }
            }
        }

        vivid::media::MediaSession* active_session = active_session_;
        if (active_session_ == nullptr) {
            const uint64_t pending_handle = pending_stream_handle_.load(std::memory_order_acquire);
            if (pending_handle != active_stream_handle_) {
                if (hs && active_stream_handle_ != 0) {
                    hs->release(active_stream_handle_);
                    active_stream_handle_ = 0;
                }
                if (hs && pending_handle != 0 &&
                    hs->retain(pending_handle)) {
                    active_stream_handle_ = pending_handle;
                }
            }
            if (hs && active_stream_handle_ != 0) {
                auto entry = hs->resolve(active_stream_handle_);
                if (entry.valid && entry.payload &&
                    entry.type && std::strcmp(entry.type, "media_stream_v1") == 0) {
                    active_session = static_cast<vivid::media::MediaSession*>(entry.payload);
                }
            }
        }

        // Track session source path and ensure extractor matches it.
        std::string effective_path;
        if (active_session) {
            std::lock_guard<std::mutex> lock(active_session->mu);
            effective_path = active_session->source_path;
        }
        if (active_session && active_session != active_session_) {
            std::shared_ptr<AVFAudioExtractor> shared_ext;
            {
                std::lock_guard<std::mutex> lock(active_session->audio_owner_mu);
                shared_ext = active_session->audio_extractor;
                if (!shared_ext) {
                    shared_ext = std::make_shared<AVFAudioExtractor>();
                    active_session->audio_extractor = shared_ext;
                }
            }
            session_extractor_ = shared_ext;
            extractor_.store(session_extractor_.get(), std::memory_order_release);
            active_session_ = active_session;
            active_session_ptr_.store(active_session_, std::memory_order_release);
            fill_thread_.update_session(active_session_);
            fill_thread_.update_extractor(session_extractor_.get());
        } else if (!active_session && active_session_ != nullptr) {
            fill_thread_.update_session(nullptr);
            fill_thread_.update_extractor(nullptr);
            extractor_.store(nullptr, std::memory_order_release);
            if (session_extractor_) {
                deferred_session_releases_.push_back(std::move(session_extractor_));
            }
            session_extractor_.reset();
            active_session_ = nullptr;
            active_session_ptr_.store(nullptr, std::memory_order_release);
        }

        if (active_session) {
            while (true) {
                auto next = vivid::media::media_session_pop_command(*active_session);
                if (!next.has_value()) break;
                auto cmd = std::move(next.value());
                if (cmd.type == vivid::media::TransportCommandType::SetPlayback) {
                    transport_speed_.store(std::max(0.01, static_cast<double>(cmd.speed)),
                                           std::memory_order_release);
                    loop_enabled_ = (cmd.loop_enabled != 0);
                } else if (cmd.type == vivid::media::TransportCommandType::Seek) {
                    pending_resync_time_.store(cmd.seek_time_s, std::memory_order_release);
                    pending_resync_generation_.store(cmd.generation, std::memory_order_release);
                } else if (cmd.type == vivid::media::TransportCommandType::SetSource) {
                    if (!cmd.source_path.empty()) effective_path = cmd.source_path;
                }
            }
        }
        if (effective_path != last_path_) {
            last_path_ = effective_path;

            pending_load_.reset();

            // Stop fill thread while we swap extractors
            fill_thread_.update_session(nullptr);
            fill_thread_.update_extractor(nullptr);

            auto* old = extractor_.load(std::memory_order_relaxed);
            extractor_.store(nullptr, std::memory_order_release);
            if (active_session_) {
                std::lock_guard<std::mutex> lock(active_session_->audio_owner_mu);
                auto old_shared = active_session_->audio_extractor;
                active_session_->audio_extractor.reset();
                session_extractor_.reset();
                if (old_shared) {
                    deferred_session_releases_.push_back(std::move(old_shared));
                }
                deferred_delete_ = nullptr;
            } else {
                deferred_delete_ = old;
            }

            if (!last_path_.empty()) {
                auto result = std::make_shared<AsyncAudioLoad>();
                pending_load_ = result;
                std::string path = last_path_;
                bool pp = (pitch_preserve.int_value() != 0);
                std::thread([result, path, pp]{
                    auto* ext = new AVFAudioExtractor();
                    ext->set_pitch_preserve(pp);
                    if (ext->open(path)) {
                        result->extractor = ext;
                        result->success = true;
                    } else {
                        delete ext;
                    }
                    result->done.store(true, std::memory_order_release);
                }).detach();
            }
            if (active_session_) {
                const double rh_before = active_session_->audio_read_head_media_time.load(std::memory_order_relaxed);
                vivid::media::media_session_audio_ring_clear(*active_session_);
                active_session_->audio_preroll_ready.store(0, std::memory_order_release);
                std::fprintf(stderr,
                    "[MAO main] SOURCE_SWITCH  path='%s'  rh_before=%.4f  rh_after=0.0  preroll→0\n",
                    last_path_.c_str(), rh_before);
            }
            return;
        }

        // Check if async load completed
        if (pending_load_ && pending_load_->done.load(std::memory_order_acquire)) {
            if (pending_load_->success) {
                auto* old = extractor_.load(std::memory_order_relaxed);
                auto* fresh = pending_load_->extractor;
                pending_load_->extractor = nullptr;
                if (active_session_) {
                    std::shared_ptr<AVFAudioExtractor> old_shared;
                    std::lock_guard<std::mutex> lock(active_session_->audio_owner_mu);
                    old_shared = active_session_->audio_extractor;
                    active_session_->audio_extractor =
                        std::shared_ptr<AVFAudioExtractor>(fresh);
                    session_extractor_ = active_session_->audio_extractor;
                    extractor_.store(session_extractor_.get(), std::memory_order_release);
                    const double rh_pre = active_session_->audio_read_head_media_time.load(std::memory_order_relaxed);
                    vivid::media::media_session_audio_ring_clear(*active_session_);
                    active_session_->audio_preroll_ready.store(0, std::memory_order_release);
                    std::fprintf(stderr,
                        "[MAO main] ASYNC_LOAD_DONE  rh_before=%.4f  rh_after=0.0  preroll→0\n",
                        rh_pre);
                    if (old_shared) {
                        deferred_session_releases_.push_back(std::move(old_shared));
                    }
                    deferred_delete_ = nullptr;
                    // Start/update fill thread with new extractor
                    fill_thread_.update_extractor(session_extractor_.get());
                    fill_thread_.update_session(active_session_);
                    if (!fill_thread_started_) {
                        fill_thread_.start(active_session_, session_extractor_.get());
                        fill_thread_started_ = true;
                    }
                    fill_thread_.notify();
                } else {
                    extractor_.store(fresh, std::memory_order_release);
                    deferred_delete_ = old;
                }
            }
            pending_load_.reset();
        }

        auto* ext = extractor_.load(std::memory_order_acquire);
        if (!ext || !ext->is_open() || !ext->has_audio()) return;

        const bool has_clock = clock_connected_.load(std::memory_order_acquire) != 0;
        if (!has_clock) return;

        // Update playback speed and pitch-preserve mode
        const float applied_speed =
            static_cast<float>(clock_speed_.load(std::memory_order_acquire));
        ext->set_speed(applied_speed);
        ext->set_loop(loop_enabled_);
        ext->set_pitch_preserve(pitch_preserve.int_value() != 0);
        if (active_session_) {
            active_session_->audio_ring_speed.store(applied_speed, std::memory_order_release);
        }

        // Handle seek/resync requests
        double requested = pending_resync_time_.exchange(-1.0, std::memory_order_acq_rel);
        uint64_t requested_gen = pending_resync_generation_.exchange(0, std::memory_order_acq_rel);
        if (requested >= 0.0) {
            if (requested_gen != 0 && has_clock) {
                uint64_t live_gen = clock_generation_.load(std::memory_order_acquire);
                if (requested_gen != live_gen) {
                    requested = -1.0;
                }
            }
        }
        if (requested >= 0.0) {
            ext->resync(requested);
            if (active_session_) {
                const double rh_pre = active_session_->audio_read_head_media_time.load(std::memory_order_relaxed);
                vivid::media::media_session_audio_ring_clear(*active_session_, requested);
                active_session_->audio_preroll_ready.store(0, std::memory_order_release);
                active_session_->sync_resync_applied.fetch_add(1, std::memory_order_relaxed);
                std::fprintf(stderr,
                    "[MAO main] RESYNC  seek_to=%.4f  rh_before=%.4f  preroll→0\n",
                    requested, rh_pre);
            }
            fill_thread_.notify();
        }

        // Wake fill thread each frame (it also self-wakes every 5ms)
        fill_thread_.notify();
    }

    // Audio thread — simplified: just read from ring, apply volume
    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        const MediaClockSnapshot clock = read_media_clock(ctx);
        if (ctx && ctx->shared_handles) {
            shared_handles_.store(ctx->shared_handles, std::memory_order_release);
        }
        if (clock.valid) {
            pending_stream_handle_.store(clock.handle_id, std::memory_order_release);
            pending_stream_ptr_.store(clock.session_ptr, std::memory_order_release);
        }
        clock_connected_.store(clock.valid ? 1u : 0u, std::memory_order_release);
        if (clock.valid) {
            clock_generation_.store(clock.clock.source_generation, std::memory_order_release);
            clock_speed_.store(static_cast<double>(clock.clock.speed), std::memory_order_release);
            if (last_seen_clock_generation_ != clock.clock.source_generation) {
                last_seen_clock_generation_ = clock.clock.source_generation;
                // New source: request resync to start
                pending_resync_generation_.store(clock.clock.source_generation, std::memory_order_release);
                pending_resync_time_.store(0.0, std::memory_order_release);
            }
        }

        float* L = audio->output_buffers[0];
        float* R = audio->output_buffers[1];
        uint32_t n = audio->buffer_size;
        auto* active_session = active_session_ptr_.load(std::memory_order_acquire);
        if (active_session) {
            active_session->audio_ring_sample_rate.store(static_cast<float>(audio->sample_rate), std::memory_order_release);
            active_session->audio_ring_speed.store(
                clock.valid ? static_cast<float>(clock.clock.speed)
                            : static_cast<float>(transport_speed_.load(std::memory_order_acquire)),
                std::memory_order_release);
        }
        callback_counter_++;

        // Read from session ring — but only after pre-roll is ready.
        // During source switches, outputting silence without advancing the read
        // head prevents drift accumulation while the fill thread buffers data.
        if (active_session && clock.valid) {
            const uint8_t preroll = active_session->audio_preroll_ready.load(std::memory_order_acquire);
            if (preroll != 0) {
                const double rh_before = active_session->audio_read_head_media_time.load(std::memory_order_relaxed);
                const uint32_t got = vivid::media::media_session_audio_read(*active_session, L, R, n);
                const double rh_after = active_session->audio_read_head_media_time.load(std::memory_order_relaxed);
                const double wh = active_session->audio_write_head_media_time.load(std::memory_order_relaxed);
                const uint32_t depth = vivid::media::media_session_audio_available_read(*active_session);
                if ((callback_counter_ % 480u) == 0u) {
                    std::fprintf(stderr,
                        "[MAO process] READ  rh=%.4f→%.4f  wh=%.4f  depth=%u  got=%u/%u  sr=%.0f  spd=%.3f\n",
                        rh_before, rh_after, wh, depth, got, n,
                        static_cast<double>(active_session->audio_ring_sample_rate.load(std::memory_order_relaxed)),
                        static_cast<double>(active_session->audio_ring_speed.load(std::memory_order_relaxed)));
                }
            } else {
                std::memset(L, 0, n * sizeof(float));
                std::memset(R, 0, n * sizeof(float));
                if ((callback_counter_ % 480u) == 0u) {
                    const double rh = active_session->audio_read_head_media_time.load(std::memory_order_relaxed);
                    const double wh = active_session->audio_write_head_media_time.load(std::memory_order_relaxed);
                    const uint32_t depth = vivid::media::media_session_audio_available_read(*active_session);
                    std::fprintf(stderr,
                        "[MAO process] PREROLL_WAIT  rh=%.4f  wh=%.4f  depth=%u\n",
                        rh, wh, depth);
                }
            }
        } else {
            std::memset(L, 0, n * sizeof(float));
            std::memset(R, 0, n * sizeof(float));
        }

        // Apply volume
        float vol = volume.value;
        for (uint32_t i = 0; i < n; i++) {
            L[i] *= vol;
            R[i] *= vol;
        }

        if (active_session && (callback_counter_ % 240u) == 0u) {
            log_session_stats(*active_session);
        }
    }

    ~MovieAudioOut() override {
        fill_thread_.stop();
        const auto* hs = shared_handles_.load(std::memory_order_acquire);
        if (hs && active_stream_handle_ != 0) {
            hs->release(active_stream_handle_);
            active_stream_handle_ = 0;
        }
        const bool had_shared_extractor =
            static_cast<bool>(session_extractor_) || !deferred_session_releases_.empty();
        session_extractor_.reset();
        deferred_session_releases_.clear();
        delete deferred_delete_;
        auto* ext = extractor_.load(std::memory_order_relaxed);
        if (!had_shared_extractor) delete ext;
    }

private:
    void log_session_stats(const vivid::media::MediaSession& session) {
        const uint64_t underrun_callbacks = session.audio_underrun_callbacks.load(std::memory_order_relaxed);
        const uint64_t underrun_frames = session.audio_underrun_frames.load(std::memory_order_relaxed);
        const uint64_t overflow_frames = session.audio_write_overflow_frames.load(std::memory_order_relaxed);
        const uint64_t resync_applied = session.sync_resync_applied.load(std::memory_order_relaxed);
        const uint32_t audio_hwm = session.audio_ring_depth_high_water.load(std::memory_order_relaxed);
        const uint64_t video_dropped = session.video_payload_dropped.load(std::memory_order_relaxed);
        const uint32_t video_hwm = session.video_payload_depth_high_water.load(std::memory_order_relaxed);

        if (underrun_callbacks == last_stats_underrun_callbacks_ &&
            underrun_frames == last_stats_underrun_frames_ &&
            overflow_frames == last_stats_overflow_frames_ &&
            resync_applied == last_stats_resync_applied_ &&
            audio_hwm == last_stats_audio_hwm_ &&
            video_dropped == last_stats_video_dropped_ &&
            video_hwm == last_stats_video_hwm_) {
            return;
        }

        last_stats_underrun_callbacks_ = underrun_callbacks;
        last_stats_underrun_frames_ = underrun_frames;
        last_stats_overflow_frames_ = overflow_frames;
        last_stats_resync_applied_ = resync_applied;
        last_stats_audio_hwm_ = audio_hwm;
        last_stats_video_dropped_ = video_dropped;
        last_stats_video_hwm_ = video_hwm;

        std::fprintf(stderr,
                     "[movie_audio_out] stats underrun_cb=%llu underrun_frames=%llu overflow_frames=%llu "
                     "resync_apply=%llu audio_hwm=%u video_drop=%llu video_hwm=%u\n",
                     static_cast<unsigned long long>(underrun_callbacks),
                     static_cast<unsigned long long>(underrun_frames),
                     static_cast<unsigned long long>(overflow_frames),
                     static_cast<unsigned long long>(resync_applied),
                     audio_hwm,
                     static_cast<unsigned long long>(video_dropped),
                     video_hwm);
    }

    struct AsyncAudioLoad {
        std::atomic<bool> done{false};
        bool success = false;
        AVFAudioExtractor* extractor = nullptr;
        ~AsyncAudioLoad() { delete extractor; }
    };

    AudioFillThread fill_thread_;
    bool fill_thread_started_ = false;

    std::atomic<AVFAudioExtractor*> extractor_{nullptr};
    AVFAudioExtractor* deferred_delete_ = nullptr;
    std::shared_ptr<AsyncAudioLoad> pending_load_;
    std::string last_path_;
    bool loop_enabled_ = true;
    std::atomic<double> transport_speed_{1.0};
    std::atomic<double> pending_resync_time_{-1.0};
    std::atomic<uint64_t> pending_resync_generation_{0};
    std::atomic<uint8_t> clock_connected_{0};
    std::atomic<uint64_t> clock_generation_{0};
    std::atomic<double> clock_speed_{1.0};
    uint64_t last_seen_clock_generation_ = 0;
    uint64_t callback_counter_ = 0;
    std::atomic<const VividSharedHandleService*> shared_handles_{nullptr};
    std::atomic<uint64_t> pending_stream_handle_{0};
    std::atomic<uint64_t> pending_stream_ptr_{0};
    uint64_t active_stream_handle_ = 0;
    vivid::media::MediaSession* active_session_ = nullptr;
    std::atomic<vivid::media::MediaSession*> active_session_ptr_{nullptr};
    std::shared_ptr<AVFAudioExtractor> session_extractor_;
    std::vector<std::shared_ptr<AVFAudioExtractor>> deferred_session_releases_;
    uint64_t last_stats_underrun_callbacks_ = 0;
    uint64_t last_stats_underrun_frames_ = 0;
    uint64_t last_stats_overflow_frames_ = 0;
    uint64_t last_stats_resync_applied_ = 0;
    uint64_t last_stats_video_dropped_ = 0;
    uint32_t last_stats_audio_hwm_ = 0;
    uint32_t last_stats_video_hwm_ = 0;
};

VIVID_REGISTER(MovieAudioOut)
