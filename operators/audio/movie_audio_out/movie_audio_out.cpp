#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/media_clock.h"
#include "operator_api/media_stream.h"
#include "../../shared/media_session/media_session.h"
#include "../../shared/movie_audio/avf_audio_extractor.h"
#include "../../shared/movie_audio/sync_policy.h"

#include <atomic>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct MovieAudioOut : vivid::OperatorBase {
    static constexpr const char* kName   = "MovieAudioOut";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> volume {"volume", 1.0f, 0.0f, 2.0f};

    MovieAudioOut() {
        vivid::semantic_tag(volume, "amplitude_linear");
        vivid::semantic_shape(volume, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&volume);
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
    static constexpr uint64_t kStartupGateCallbacks = 24;
    static constexpr uint64_t kLoopGateCallbacks = 16;

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
        // (at least one full audio buffer has elapsed since the previous frame).
        delete deferred_delete_;
        deferred_delete_ = nullptr;
        deferred_session_releases_.clear();

        // Attach/detach shared media-stream handle (from MovieLoaded output data).
        const uint64_t pending_session_ptr = pending_stream_ptr_.load(std::memory_order_acquire);
        if (pending_session_ptr != 0) {
            active_session_ = reinterpret_cast<vivid::media::MediaSession*>(pending_session_ptr);
            active_session_ptr_.store(active_session_, std::memory_order_release);
            active_stream_handle_ = 0;
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
        } else if (!active_session && active_session_ != nullptr) {
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

            // Cancel any in-flight async load
            pending_load_.reset();

            // Clear current extractor immediately (audio thread gets silence).
            // Important ownership split:
            // - session-backed path uses shared_ptr ownership (never hand to raw deferred delete)
            // - legacy non-session path keeps raw pointer handoff semantics
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
                // Launch async load on background thread
                auto result = std::make_shared<AsyncAudioLoad>();
                pending_load_ = result;
                std::string path = last_path_;
                std::thread([result, path]{
                    auto* ext = new AVFAudioExtractor();
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
                vivid::media::media_session_audio_ring_clear(*active_session_);
            }
            return;  // Skip sync check on file change
        }

        // Check if async load completed
        if (pending_load_ && pending_load_->done.load(std::memory_order_acquire)) {
            if (pending_load_->success) {
                auto* old = extractor_.load(std::memory_order_relaxed);
                // Take ownership — null it so AsyncAudioLoad destructor won't delete
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
                    vivid::media::media_session_audio_ring_clear(*active_session_);
                    if (old_shared) {
                        deferred_session_releases_.push_back(std::move(old_shared));
                    }
                    deferred_delete_ = nullptr;
                } else {
                    extractor_.store(fresh, std::memory_order_release);
                    deferred_delete_ = old;
                }
                request_startup_gate_.store(true, std::memory_order_release);
            }
            pending_load_.reset();
        }

        auto* ext = extractor_.load(std::memory_order_acquire);
        if (!ext || !ext->is_open() || !ext->has_audio()) return;

        const bool has_clock = clock_connected_.load(std::memory_order_acquire) != 0;
        if (!has_clock) return;

        // Update playback speed for pitch-preserving time stretch.
        const float applied_speed =
            static_cast<float>(clock_speed_.load(std::memory_order_acquire));
        ext->set_speed(applied_speed);
        ext->set_loop(loop_enabled_);
        if (active_session_) {
            active_session_->audio_ring_speed.store(applied_speed, std::memory_order_release);
        }

        // Audio thread may request a hard resync on sustained large drift.
        double requested = pending_resync_time_.exchange(-1.0, std::memory_order_acq_rel);
        uint64_t requested_gen = pending_resync_generation_.exchange(0, std::memory_order_acq_rel);
        if (requested >= 0.0) {
            if (requested_gen != 0 && has_clock) {
                uint64_t live_gen = clock_generation_.load(std::memory_order_acquire);
                if (requested_gen != live_gen) {
                    // Stale request from a superseded source generation.
                    requested = -1.0;
                }
            }
        }
        if (requested >= 0.0) {
            ext->resync(requested);
            if (active_session_) {
                vivid::media::media_session_audio_ring_clear(*active_session_);
                active_session_->audio_read_head_media_time.store(requested, std::memory_order_release);
                active_session_->sync_resync_applied.fetch_add(1, std::memory_order_relaxed);
            }
            log_sync("resync", 0.0, 0.0, requested_gen, clock_loop_epoch_.load(std::memory_order_acquire));
            request_startup_gate_.store(true, std::memory_order_release);
            sync_mode_ = AVSyncCorrectionMode::Locked;
            hard_error_streak_ = 0;
        }

        // Pre-fill ring buffer from AVAssetReader
        ext->fill_buffer();
        if (active_session_) {
            pump_session_audio_ring(*active_session_, *ext);
        }
    }

    // Audio thread
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
            clock_loop_epoch_.store(clock.clock.loop_epoch, std::memory_order_release);
            clock_local_time_s_.store(static_cast<double>(clock.clock.local_time_s), std::memory_order_release);
            clock_monotonic_time_s_.store(clock.clock.monotonic_time_s, std::memory_order_release);
            clock_speed_.store(static_cast<double>(clock.clock.speed), std::memory_order_release);
            clock_duration_s_.store(static_cast<double>(clock.clock.duration_s), std::memory_order_release);
            if (last_seen_clock_generation_ != clock.clock.source_generation) {
                last_seen_clock_generation_ = clock.clock.source_generation;
                hard_error_streak_ = 0;
                resync_cooldown_until_cb_ = callback_counter_ + 48;
                pending_resync_generation_.store(clock.clock.source_generation, std::memory_order_release);
                pending_resync_time_.store(static_cast<double>(clock.clock.local_time_s), std::memory_order_release);
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
        const uint64_t cb = callback_counter_;

        if (request_startup_gate_.exchange(false, std::memory_order_acq_rel)) {
            startup_gate_pending_ = true;
        }

        constexpr AVSyncThresholds kSync{};
        auto* ext = extractor_.load(std::memory_order_acquire);
        const bool sync_ready =
            (active_session && ext && ext->is_open() && ext->has_audio() && clock.valid);
        if (sync_ready && !last_sync_ready_) {
            startup_gate_pending_ = true;
        }
        last_sync_ready_ = sync_ready;

        if (clock.valid) {
            const uint64_t generation = clock.clock.source_generation;
            const uint32_t loop_epoch = clock.clock.loop_epoch;
            if (!last_clock_seen_) {
                last_clock_seen_ = true;
                last_clock_generation_seen_ = generation;
                last_clock_loop_epoch_seen_ = loop_epoch;
            } else if (generation != last_clock_generation_seen_) {
                last_clock_generation_seen_ = generation;
                last_clock_loop_epoch_seen_ = loop_epoch;
                startup_gate_pending_ = true;
            } else if (loop_epoch != last_clock_loop_epoch_seen_) {
                last_clock_loop_epoch_seen_ = loop_epoch;
                loop_gate_pending_ = true;
            }
        }

        if (sync_ready) {
            if (startup_gate_pending_) {
                startup_gate_until_cb_ = cb + kStartupGateCallbacks;
                startup_gate_pending_ = false;
            }
            if (loop_gate_pending_) {
                loop_gate_until_cb_ = cb + kLoopGateCallbacks;
                loop_gate_pending_ = false;
            }
        }

        const uint64_t gate_generation =
            clock.valid ? clock.clock.source_generation : clock_generation_.load(std::memory_order_acquire);
        const uint32_t gate_loop_epoch =
            clock.valid ? clock.clock.loop_epoch : clock_loop_epoch_.load(std::memory_order_acquire);
        const bool prev_gate_active = last_startup_gate_active_ || last_loop_gate_active_;
        const bool startup_gate_active = sync_gate_active(cb, startup_gate_until_cb_);
        const bool loop_gate_active = sync_gate_active(cb, loop_gate_until_cb_);
        const bool gate_active = startup_gate_active || loop_gate_active;
        if (startup_gate_active != last_startup_gate_active_) {
            log_gate_transition(startup_gate_active ? "startup_gate_begin" : "startup_gate_end",
                                gate_generation, gate_loop_epoch);
        }
        if (loop_gate_active != last_loop_gate_active_) {
            log_gate_transition(loop_gate_active ? "loop_gate_begin" : "loop_gate_end",
                                gate_generation, gate_loop_epoch);
        }
        last_startup_gate_active_ = startup_gate_active;
        last_loop_gate_active_ = loop_gate_active;
        if (gate_active || prev_gate_active != gate_active) {
            sync_mode_ = AVSyncCorrectionMode::Locked;
            hard_error_streak_ = 0;
        }

        if (sync_ready) {
            double target_time = 0.0;
            double audio_time = active_session
                ? active_session->audio_read_head_media_time.load(std::memory_order_acquire)
                : ext->read_head_pts();
            double speed_for_skip =
                std::max(0.01, clock_speed_.load(std::memory_order_acquire));
            uint64_t generation = 0;
            uint32_t loop_epoch = 0;
            target_time = clock.clock.monotonic_time_s;
            speed_for_skip = std::max(0.01, static_cast<double>(clock.clock.speed));
            generation = clock.clock.source_generation;
            loop_epoch = clock.clock.loop_epoch;

            const double error = target_time - audio_time;
            AVSyncDecision decision =
                decide_av_sync_stateful_gated(error, kSync, sync_mode_, gate_active);
            switch (decision.action) {
                case AVSyncAction::None:
                    hard_error_streak_ = 0;
                    log_sync("locked", error, 0.0, generation, loop_epoch);
                    break;
                case AVSyncAction::Skip: {
                    hard_error_streak_ = 0;
                    uint32_t frames_to_drop = static_cast<uint32_t>(std::ceil(
                        decision.skip_media_s * static_cast<double>(audio->sample_rate) / speed_for_skip));
                    if (active_session) {
                        vivid::media::media_session_audio_discard(*active_session, frames_to_drop);
                        active_session->sync_skip_actions.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        ext->discard_samples(frames_to_drop);
                    }
                    log_sync("skip", error, decision.skip_media_s, generation, loop_epoch);
                    break;
                }
                case AVSyncAction::Silence:
                    hard_error_streak_ = 0;
                    {
                        const double ahead_s = std::max(0.0, -error);
                        if (ahead_s >= kSync.critical_s &&
                            callback_counter_ >= resync_cooldown_until_cb_ &&
                            pending_resync_time_.load(std::memory_order_acquire) < 0.0 &&
                            !gate_active) {
                            const double request_local =
                                static_cast<double>(clock.clock.local_time_s);
                            if (auto* session = active_session_ptr_.load(std::memory_order_acquire)) {
                                vivid::media::TransportCommand cmd;
                                cmd.type = vivid::media::TransportCommandType::Seek;
                                cmd.generation = generation;
                                cmd.seek_time_s = request_local;
                                vivid::media::media_session_enqueue_command(*session, std::move(cmd));
                                session->sync_resync_requests.fetch_add(1, std::memory_order_relaxed);
                            } else {
                                pending_resync_time_.store(request_local, std::memory_order_release);
                                pending_resync_generation_.store(generation, std::memory_order_release);
                            }
                            resync_cooldown_until_cb_ = callback_counter_ + 48;
                            log_sync("resync_req_ahead", error, 0.0, generation, loop_epoch);
                        } else {
                            log_sync("hold_suppressed", error, 0.0, generation, loop_epoch);
                        }
                    }
                    break;
                case AVSyncAction::Resync:
                    hard_error_streak_++;
                    if (callback_counter_ >= resync_cooldown_until_cb_ &&
                        hard_error_streak_ >= 3 &&
                        pending_resync_time_.load(std::memory_order_acquire) < 0.0 &&
                        !gate_active) {
                        const double request_local =
                            static_cast<double>(clock.clock.local_time_s);
                        if (auto* session = active_session_ptr_.load(std::memory_order_acquire)) {
                            vivid::media::TransportCommand cmd;
                            cmd.type = vivid::media::TransportCommandType::Seek;
                            cmd.generation = generation;
                            cmd.seek_time_s = request_local;
                            vivid::media::media_session_enqueue_command(*session, std::move(cmd));
                            session->sync_resync_requests.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            pending_resync_time_.store(request_local, std::memory_order_release);
                            pending_resync_generation_.store(generation, std::memory_order_release);
                        }
                        resync_cooldown_until_cb_ = callback_counter_ + 48;
                        hard_error_streak_ = 0;
                        log_sync("resync_req", error, 0.0, generation, loop_epoch);
                    } else {
                        log_sync("resync_hold", error, 0.0, generation, loop_epoch);
                    }
                    std::memset(L, 0, n * sizeof(float));
                    std::memset(R, 0, n * sizeof(float));
                    return;
            }

            if (active_session) {
                vivid::media::media_session_audio_read(*active_session, L, R, n);
            } else {
                ext->read_samples(L, R, n);
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
        if (active_session && (cb % 240u) == 0u) {
            log_session_stats(*active_session);
        }
    }

    ~MovieAudioOut() override {
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
    static void copy_small(char* dst, size_t cap, const char* src) {
        if (!dst || cap == 0) return;
        if (!src) {
            dst[0] = '\0';
            return;
        }
        std::strncpy(dst, src, cap - 1);
        dst[cap - 1] = '\0';
    }

    void log_sync(const char* action, double error_s, double aux_s,
                  uint64_t generation, uint32_t loop_epoch) {
        const uint64_t frame = sync_log_frame_.fetch_add(1, std::memory_order_relaxed);
        const uint64_t cooldown_until = sync_log_cooldown_until_.load(std::memory_order_relaxed);
        if (frame < cooldown_until && std::strcmp(action, last_sync_action_) == 0) return;
        copy_small(last_sync_action_, sizeof(last_sync_action_), action);
        sync_log_cooldown_until_.store(frame + 120, std::memory_order_relaxed);
        std::fprintf(stderr,
                     "[movie_audio_out] sync_state=%s gen=%llu epoch=%u err=%.1fms aux=%.1fms\n",
                     action,
                     static_cast<unsigned long long>(generation),
                     loop_epoch,
                     error_s * 1000.0,
                     aux_s * 1000.0);
    }

    void log_gate_transition(const char* action, uint64_t generation, uint32_t loop_epoch) {
        std::fprintf(stderr,
                     "[movie_audio_out] sync_state=%s gen=%llu epoch=%u err=0.0ms aux=0.0ms\n",
                     action,
                     static_cast<unsigned long long>(generation),
                     loop_epoch);
    }

    void log_session_stats(const vivid::media::MediaSession& session) {
        const uint64_t underrun_callbacks = session.audio_underrun_callbacks.load(std::memory_order_relaxed);
        const uint64_t underrun_frames = session.audio_underrun_frames.load(std::memory_order_relaxed);
        const uint64_t overflow_frames = session.audio_write_overflow_frames.load(std::memory_order_relaxed);
        const uint64_t resync_req = session.sync_resync_requests.load(std::memory_order_relaxed);
        const uint64_t resync_applied = session.sync_resync_applied.load(std::memory_order_relaxed);
        const uint64_t skip_actions = session.sync_skip_actions.load(std::memory_order_relaxed);
        const uint64_t silence_actions = session.sync_silence_actions.load(std::memory_order_relaxed);
        const uint32_t audio_hwm = session.audio_ring_depth_high_water.load(std::memory_order_relaxed);
        const uint64_t video_dropped = session.video_payload_dropped.load(std::memory_order_relaxed);
        const uint32_t video_hwm = session.video_payload_depth_high_water.load(std::memory_order_relaxed);

        if (underrun_callbacks == last_stats_underrun_callbacks_ &&
            underrun_frames == last_stats_underrun_frames_ &&
            overflow_frames == last_stats_overflow_frames_ &&
            resync_req == last_stats_resync_req_ &&
            resync_applied == last_stats_resync_applied_ &&
            skip_actions == last_stats_skip_actions_ &&
            silence_actions == last_stats_silence_actions_ &&
            audio_hwm == last_stats_audio_hwm_ &&
            video_dropped == last_stats_video_dropped_ &&
            video_hwm == last_stats_video_hwm_) {
            return;
        }

        last_stats_underrun_callbacks_ = underrun_callbacks;
        last_stats_underrun_frames_ = underrun_frames;
        last_stats_overflow_frames_ = overflow_frames;
        last_stats_resync_req_ = resync_req;
        last_stats_resync_applied_ = resync_applied;
        last_stats_skip_actions_ = skip_actions;
        last_stats_silence_actions_ = silence_actions;
        last_stats_audio_hwm_ = audio_hwm;
        last_stats_video_dropped_ = video_dropped;
        last_stats_video_hwm_ = video_hwm;

        std::fprintf(stderr,
                     "[movie_audio_out] stats underrun_cb=%llu underrun_frames=%llu overflow_frames=%llu "
                     "resync_req=%llu resync_apply=%llu skip=%llu silence=%llu audio_hwm=%u video_drop=%llu video_hwm=%u\n",
                     static_cast<unsigned long long>(underrun_callbacks),
                     static_cast<unsigned long long>(underrun_frames),
                     static_cast<unsigned long long>(overflow_frames),
                     static_cast<unsigned long long>(resync_req),
                     static_cast<unsigned long long>(resync_applied),
                     static_cast<unsigned long long>(skip_actions),
                     static_cast<unsigned long long>(silence_actions),
                     audio_hwm,
                     static_cast<unsigned long long>(video_dropped),
                     video_hwm);
    }

    static constexpr uint32_t kSessionPumpChunk = 1024;
    void pump_session_audio_ring(vivid::media::MediaSession& session,
                                 AVFAudioExtractor& ext) {
        while (true) {
            const uint32_t writable = vivid::media::media_session_audio_available_write(session);
            if (writable == 0) break;
            const uint32_t chunk = std::min<uint32_t>(writable, kSessionPumpChunk);
            const uint32_t got = ext.read_samples(session_pump_left_.data(),
                                                  session_pump_right_.data(),
                                                  chunk);
            if (got == 0) break;
            vivid::media::media_session_audio_write(session,
                                                    session_pump_left_.data(),
                                                    session_pump_right_.data(),
                                                    got);
        }
    }

    struct AsyncAudioLoad {
        std::atomic<bool> done{false};
        bool success = false;
        AVFAudioExtractor* extractor = nullptr;
        ~AsyncAudioLoad() { delete extractor; }
    };

    std::atomic<AVFAudioExtractor*> extractor_{nullptr};
    AVFAudioExtractor* deferred_delete_ = nullptr;  // held for one frame before deletion
    std::shared_ptr<AsyncAudioLoad> pending_load_;
    std::string last_path_;
    bool loop_enabled_ = true;
    std::atomic<double> transport_speed_{1.0};
    std::atomic<double> pending_resync_time_{-1.0};
    std::atomic<uint64_t> pending_resync_generation_{0};
    std::atomic<uint8_t> clock_connected_{0};
    std::atomic<uint64_t> clock_generation_{0};
    std::atomic<uint32_t> clock_loop_epoch_{0};
    std::atomic<double> clock_local_time_s_{0.0};
    std::atomic<double> clock_monotonic_time_s_{0.0};
    std::atomic<double> clock_speed_{1.0};
    std::atomic<double> clock_duration_s_{0.0};
    uint64_t last_seen_clock_generation_ = 0;
    uint64_t startup_gate_until_cb_ = 0;
    uint64_t loop_gate_until_cb_ = 0;
    bool startup_gate_pending_ = false;
    bool loop_gate_pending_ = false;
    bool last_startup_gate_active_ = false;
    bool last_loop_gate_active_ = false;
    bool last_sync_ready_ = false;
    bool last_clock_seen_ = false;
    uint64_t last_clock_generation_seen_ = 0;
    uint32_t last_clock_loop_epoch_seen_ = 0;
    uint64_t callback_counter_ = 0;
    uint64_t resync_cooldown_until_cb_ = 0;
    uint32_t hard_error_streak_ = 0;
    AVSyncCorrectionMode sync_mode_ = AVSyncCorrectionMode::Locked;
    std::atomic<bool> request_startup_gate_{false};
    std::atomic<uint64_t> sync_log_frame_{0};
    std::atomic<uint64_t> sync_log_cooldown_until_{0};
    std::atomic<const VividSharedHandleService*> shared_handles_{nullptr};
    std::atomic<uint64_t> pending_stream_handle_{0};
    std::atomic<uint64_t> pending_stream_ptr_{0};
    uint64_t active_stream_handle_ = 0;
    vivid::media::MediaSession* active_session_ = nullptr;
    std::atomic<vivid::media::MediaSession*> active_session_ptr_{nullptr};
    std::shared_ptr<AVFAudioExtractor> session_extractor_;
    std::vector<std::shared_ptr<AVFAudioExtractor>> deferred_session_releases_;
    std::array<float, 4096> scratch_left_{};
    std::array<float, 4096> scratch_right_{};
    std::array<float, kSessionPumpChunk> session_pump_left_{};
    std::array<float, kSessionPumpChunk> session_pump_right_{};
    char last_sync_action_[32] = {};
    uint64_t last_stats_underrun_callbacks_ = 0;
    uint64_t last_stats_underrun_frames_ = 0;
    uint64_t last_stats_overflow_frames_ = 0;
    uint64_t last_stats_resync_req_ = 0;
    uint64_t last_stats_resync_applied_ = 0;
    uint64_t last_stats_skip_actions_ = 0;
    uint64_t last_stats_silence_actions_ = 0;
    uint64_t last_stats_video_dropped_ = 0;
    uint32_t last_stats_audio_hwm_ = 0;
    uint32_t last_stats_video_hwm_ = 0;
};

VIVID_REGISTER(MovieAudioOut)
