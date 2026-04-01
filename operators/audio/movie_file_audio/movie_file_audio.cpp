#include "operator_api/operator.h"
#include "../../shared/movie_audio/avf_audio_extractor.h"

#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// =============================================================================
// Private audio ring buffer — same algorithm as media_session.h ring but
// self-contained within this operator.  No shared state.
// =============================================================================

struct AudioRing {
    static constexpr uint32_t kCapacity = 240000; // ~5s @ 48kHz

    std::array<float, kCapacity> left{};
    std::array<float, kCapacity> right{};
    std::atomic<uint32_t> write_pos{0};
    std::atomic<uint32_t> read_pos{0};
    std::atomic<uint32_t> epoch{0};        // incremented on clear — detects mid-read clears
    std::atomic<float>    sample_rate{48000.0f};
    std::atomic<float>    speed{1.0f};

    // Monotonic media-time tracking (written by read side, read by frame thread via bridge)
    std::atomic<double>   read_head_time{0.0};
    std::atomic<double>   write_head_time{0.0};

    // Pre-roll gate
    std::atomic<uint8_t>  preroll_ready{0};

    uint32_t available_read() const {
        const uint32_t w = write_pos.load(std::memory_order_acquire);
        const uint32_t r = read_pos.load(std::memory_order_relaxed);
        return (w - r + kCapacity) % kCapacity;
    }

    uint32_t available_write() const {
        const uint32_t w = write_pos.load(std::memory_order_relaxed);
        const uint32_t r = read_pos.load(std::memory_order_acquire);
        return (r - w - 1 + kCapacity) % kCapacity;
    }

    uint32_t write(const float* l, const float* r_in, uint32_t frames) {
        if (!l || !r_in || frames == 0) return 0;
        const uint32_t can = available_write();
        const uint32_t n = std::min(frames, can);
        const uint32_t wp = write_pos.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t idx = (wp + i) % kCapacity;
            left[idx] = l[i];
            right[idx] = r_in[i];
        }
        write_pos.store((wp + n) % kCapacity, std::memory_order_release);
        return n;
    }

    uint32_t read(float* l_out, float* r_out, uint32_t frames) {
        if (!l_out || !r_out || frames == 0) return 0;
        const uint32_t epoch_before = epoch.load(std::memory_order_acquire);
        const uint32_t avail = available_read();
        const uint32_t n = std::min(frames, avail);
        const uint32_t rp = read_pos.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t idx = (rp + i) % kCapacity;
            l_out[i] = left[idx];
            r_out[i] = right[idx];
        }
        // If ring was cleared mid-read, discard
        if (epoch.load(std::memory_order_acquire) != epoch_before) {
            std::memset(l_out, 0, frames * sizeof(float));
            std::memset(r_out, 0, frames * sizeof(float));
            return 0;
        }
        if (n < frames) {
            std::memset(l_out + n, 0, (frames - n) * sizeof(float));
            std::memset(r_out + n, 0, (frames - n) * sizeof(float));
        }
        read_pos.store((rp + n) % kCapacity, std::memory_order_release);
        // Advance time and reconcile with write head to prevent unbounded drift.
        {
            const double sr = std::max(1.0, static_cast<double>(sample_rate.load(std::memory_order_relaxed)));
            const double spd = std::max(0.0, static_cast<double>(speed.load(std::memory_order_relaxed)));
            const double advance = static_cast<double>(frames) * spd / sr;
            const double old_t = read_head_time.load(std::memory_order_relaxed);
            double new_t = old_t + advance;

            // The "true" read position is write_head_time minus buffered duration.
            const double wht = write_head_time.load(std::memory_order_acquire);
            const double buffer_lag = static_cast<double>(available_read()) * spd / sr;
            const double expected_t = wht - buffer_lag;
            const double error = new_t - expected_t;

            constexpr double kSnapThreshold = 0.100; // 100ms: hard snap
            constexpr double kSlewThreshold = 0.002;  // 2ms: begin slewing
            constexpr double kSlewRate      = 0.10;   // correct 10% of error per callback

            if (std::abs(error) > kSnapThreshold) {
                new_t = expected_t + advance;
            } else if (std::abs(error) > kSlewThreshold) {
                new_t -= error * kSlewRate;
            }

            read_head_time.store(new_t, std::memory_order_relaxed);
        }
        return n;
    }

    void clear(double reset_time = 0.0) {
        write_pos.store(0, std::memory_order_relaxed);
        read_pos.store(0, std::memory_order_relaxed);
        read_head_time.store(reset_time, std::memory_order_relaxed);
        write_head_time.store(reset_time, std::memory_order_relaxed);
        epoch.fetch_add(1, std::memory_order_release);
    }
};

// =============================================================================
// FillThread — dedicated thread that decodes audio into the ring buffer
// =============================================================================

class FillThread {
public:
    void start(AudioRing* ring, AVFAudioExtractor* extractor) {
        stop();
        ring_.store(ring, std::memory_order_release);
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

    void update_ring(AudioRing* ring) {
        ring_.store(ring, std::memory_order_release);
    }

    void update_extractor(AVFAudioExtractor* ext) {
        extractor_.store(ext, std::memory_order_release);
    }

    void quiesce() {
        std::lock_guard<std::mutex> lock(pump_mu_);
    }

    void notify() {
        std::lock_guard<std::mutex> lock(mu_);
        wake_.notify_one();
    }

    ~FillThread() { stop(); }

private:
    static constexpr uint32_t kFillChunk = 2048;
    static constexpr uint32_t kTargetAhead = 96000;   // ~2s @ 48kHz
    static constexpr uint32_t kPrerollFrames = 24000;  // ~0.5s @ 48kHz

    void run() {
        while (running_.load(std::memory_order_acquire)) {
            pump();
            std::unique_lock<std::mutex> lock(mu_);
            wake_.wait_for(lock, std::chrono::milliseconds(5));
        }
    }

    void pump() {
        std::lock_guard<std::mutex> pump_lock(pump_mu_);
        auto* ring = ring_.load(std::memory_order_acquire);
        auto* ext = extractor_.load(std::memory_order_acquire);
        if (!ring || !ext || !ext->is_open() || !ext->has_audio()) return;

        bool any_written = false;
        while (true) {
            if (ring->available_read() >= kTargetAhead) break;
            const uint32_t writable = ring->available_write();
            if (writable == 0) break;
            const uint32_t chunk = std::min(writable, kFillChunk);
            const uint32_t got = ext->decode_samples(fill_left_.data(), fill_right_.data(), chunk);
            if (got == 0) break;
            ring->write(fill_left_.data(), fill_right_.data(), got);
            any_written = true;
        }

        if (any_written) {
            ring->write_head_time.store(ext->write_head_pts(), std::memory_order_release);
        }

        // Signal pre-roll ready once enough is buffered
        if (ring->preroll_ready.load(std::memory_order_relaxed) == 0) {
            if (ring->available_read() >= kPrerollFrames) {
                ring->preroll_ready.store(1, std::memory_order_release);
            }
        }
    }

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<AudioRing*> ring_{nullptr};
    std::atomic<AVFAudioExtractor*> extractor_{nullptr};
    std::mutex mu_;
    std::mutex pump_mu_;
    std::condition_variable wake_;
    std::array<float, kFillChunk> fill_left_{};
    std::array<float, kFillChunk> fill_right_{};
};

/**
 * @brief Audio playback from video and audio files with speed control.
 *
 * Decodes and plays the audio track from media files with adjustable
 * playback speed. Optional pitch preservation prevents chipmunk effects
 * at non-unity speeds.
 *
 * @param pitch_preserve Maintain original pitch when changing speed.
 * @param play_mode Loop, Once, or Hold Last frame.
 * @see MovieFileIn, Sampler
 */
struct MovieFileAudio : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "MovieFileAudio";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::FilePath> file {"file"};
    vivid::Param<float> speed          {"speed", 1.0f, 0.0f, 4.0f};
    vivid::Param<float> volume         {"volume", 1.0f, 0.0f, 2.0f};
    vivid::Param<int>   pitch_preserve {"pitch_preserve", 1, {"Off", "On"}};
    vivid::Param<int>   play_mode      {"play_mode", 0, {"Loop", "Once", "Hold Last"}};

    MovieFileAudio() {
        vivid::semantic_tag(file, "path_video");
        vivid::semantic_shape(file, "path");
        vivid::description(file, "Path to a video or audio file to play");

        vivid::semantic_tag(speed, "x_playback_speed");
        vivid::semantic_shape(speed, "scalar");
        vivid::description(speed, "Playback speed multiplier (1 = normal, 0 = paused)");

        vivid::semantic_tag(volume, "amplitude_linear");
        vivid::semantic_shape(volume, "scalar");
        vivid::description(volume, "Output volume (0\u20132, with 1 = unity gain)");

        vivid::semantic_tag(pitch_preserve, "x_pitch_preserve");
        vivid::semantic_shape(pitch_preserve, "enum");
        vivid::description(pitch_preserve, "Keep original pitch when speed is changed");

        vivid::semantic_tag(play_mode, "x_play_mode");
        vivid::semantic_shape(play_mode, "enum");
        vivid::description(play_mode, "Loop continuously, play Once, or Hold Last frame at end");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
        out.push_back(&speed);
        out.push_back(&volume);
        out.push_back(&pitch_preserve);
        out.push_back(&play_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output",   VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        out.push_back({"time",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"duration", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    // ---- Main thread (called each frame) ------------------------------------

    void main_thread_update(double /*time*/) override {
        // Deferred delete: by now the audio thread has observed the new pointer
        delete deferred_delete_;
        deferred_delete_ = nullptr;

        // Detect file path change
        const std::string current_path = file.str_value;
        if (current_path != last_path_) {
            last_path_ = current_path;
            on_source_changed();
            return;
        }

        // Check if async load completed
        if (pending_load_ && pending_load_->done.load(std::memory_order_acquire)) {
            if (pending_load_->success) {
                auto* old = extractor_.load(std::memory_order_relaxed);
                auto* fresh = pending_load_->extractor;
                pending_load_->extractor = nullptr; // take ownership

                extractor_.store(fresh, std::memory_order_release);
                deferred_delete_ = old;

                ring_.clear();
                ring_.preroll_ready.store(0, std::memory_order_release);

                fill_thread_.update_extractor(fresh);
                fill_thread_.update_ring(&ring_);
                if (!fill_thread_started_) {
                    fill_thread_.start(&ring_, fresh);
                    fill_thread_started_ = true;
                }
                fill_thread_.notify();

                // Video-only files: no audio to preroll
                if (!fresh->has_audio()) {
                    ring_.preroll_ready.store(1, std::memory_order_release);
                }

                // Fresh load starts at t=0; drain any stale resync
                pending_resync_time_.store(-1.0, std::memory_order_release);
            }
            pending_load_.reset();
        }

        auto* ext = extractor_.load(std::memory_order_acquire);
        if (!ext || !ext->is_open() || !ext->has_audio()) return;

        // Update playback parameters
        ext->set_speed(speed.value);
        ext->set_loop(play_mode.int_value() == 0);
        ext->set_pitch_preserve(pitch_preserve.int_value() != 0);
        ring_.speed.store(speed.value, std::memory_order_release);

        // Handle seek/resync
        double requested = pending_resync_time_.exchange(-1.0, std::memory_order_acq_rel);
        if (requested >= 0.0) {
            ext->resync(requested);
            ring_.clear(requested);
            ring_.preroll_ready.store(0, std::memory_order_release);
            fill_thread_.notify();
        }

        // Wake fill thread each frame
        fill_thread_.notify();
    }

    // ---- Audio thread (real-time callback) ----------------------------------

    void process_audio(const VividAudioContext* ctx) override {
        float* L = ctx->output_buffers[0];
        float* R = ctx->output_buffers[0] + ctx->buffer_size; // planar channel 1
        const uint32_t n = ctx->buffer_size;

        ring_.sample_rate.store(static_cast<float>(ctx->sample_rate), std::memory_order_release);

        const uint8_t preroll = ring_.preroll_ready.load(std::memory_order_acquire);
        if (preroll != 0) {
            ring_.read(L, R, n);
        } else {
            std::memset(L, 0, n * sizeof(float));
            std::memset(R, 0, n * sizeof(float));
        }

        // Apply volume with 64-sample ramp to avoid clicks
        const float vol = volume.value;
        const float prev = prev_volume_;
        if (vol != prev) {
            const uint32_t ramp_len = std::min(n, uint32_t{64});
            for (uint32_t i = 0; i < ramp_len; ++i) {
                const float t = static_cast<float>(i + 1) / static_cast<float>(ramp_len);
                const float v = prev + (vol - prev) * t;
                L[i] *= v;
                R[i] *= v;
            }
            for (uint32_t i = ramp_len; i < n; ++i) {
                L[i] *= vol;
                R[i] *= vol;
            }
            prev_volume_ = vol;
        } else {
            for (uint32_t i = 0; i < n; ++i) {
                L[i] *= vol;
                R[i] *= vol;
            }
        }

        // Publish SIGNAL outputs: time and duration
        // These cross the cadence bridge to frame-rate operators (e.g. MovieFileIn)
        auto* ext = extractor_.load(std::memory_order_acquire);
        double mono_time = ring_.read_head_time.load(std::memory_order_relaxed);
        const float dur = (ext && ext->is_open()) ? ext->duration() : 0.0f;

        // "Hold Last" (play_mode 2): clamp time at duration so the last frame
        // stays on screen and the time signal doesn't drift past the end.
        if (play_mode.int_value() == 2 && dur > 0.0f && mono_time > static_cast<double>(dur)) {
            mono_time = static_cast<double>(dur);
        }

        // Wrap by duration before float cast to preserve precision for long playback.
        // MovieFileIn already applies wrap_time() so this is compatible.
        double out_time = mono_time;
        if (dur > 0.0f) {
            out_time = std::fmod(mono_time, static_cast<double>(dur));
            if (out_time < 0.0) out_time += static_cast<double>(dur);
        }
        // Floor to epsilon so the video operator's audio_master gate (> 0.0f) stays active.
        if (out_time < 1e-6 && mono_time > 0.0) out_time = 1e-6;
    }

    ~MovieFileAudio() override {
        fill_thread_.stop();
        delete deferred_delete_;
        auto* ext = extractor_.load(std::memory_order_relaxed);
        delete ext;
    }

private:
    struct AsyncAudioLoad {
        std::atomic<bool> done{false};
        bool success = false;
        AVFAudioExtractor* extractor = nullptr;
        ~AsyncAudioLoad() { delete extractor; }
    };

    void on_source_changed() {
        // Stop fill thread while we swap extractors
        fill_thread_.update_ring(nullptr);
        fill_thread_.update_extractor(nullptr);
        fill_thread_.quiesce();

        auto* old = extractor_.load(std::memory_order_relaxed);
        extractor_.store(nullptr, std::memory_order_release);
        deferred_delete_ = old;

        pending_load_.reset();

        ring_.clear();
        ring_.preroll_ready.store(0, std::memory_order_release);

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
    }

    AudioRing ring_{};
    FillThread fill_thread_;
    bool fill_thread_started_ = false;

    std::atomic<AVFAudioExtractor*> extractor_{nullptr};
    AVFAudioExtractor* deferred_delete_ = nullptr;
    std::shared_ptr<AsyncAudioLoad> pending_load_;
    std::string last_path_;
    float prev_volume_ = 1.0f;
    std::atomic<double> pending_resync_time_{-1.0};
};

VIVID_REGISTER(MovieFileAudio)
