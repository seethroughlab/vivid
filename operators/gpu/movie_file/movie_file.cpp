#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "../../shared/movie_decode/video_decoder.h"
#include "../../shared/movie_decode/decoder_factory.h"
#include "../../shared/movie_decode/texture_upload.h"
#include "../../shared/movie_decode/metal_frame_upload.h"
#include "../../shared/movie_decode/placeholder_frame.h"
#include "../../shared/movie_decode/load_generation.h"
#include "../../shared/movie_decode/movie_playback_stats.h"
#include "../../shared/movie_audio/avf_audio_extractor.h"
#include "movie_transport.h"
#include "decoded_frame_queue.h"
#include "video_decode_worker.h"
#include "../../shared/movie_decode/avf_decoder.h"
#include "../../shared/movie_decode/hap_decoder.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <memory>
#include <thread>
#include <cctype>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cmath>
#include <optional>
#include <utility>
#include <unordered_map>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

static const char* kBlitFragment = R"(

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var texSampler: sampler;
@group(0) @binding(1) var tex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(tex, texSampler, input.uv);
}
)";

static const char* kBlitFragmentYCoCg = R"(

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var texSampler: sampler;
@group(0) @binding(1) var tex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

fn decode_hapq_ycocg(sampled: vec4f) -> vec3f {
    let scale = sampled.b * (255.0 / 8.0) + 1.0;
    let co = (sampled.r - 0.5) / scale;
    let cg = (sampled.g - 0.5) / scale;
    let y = sampled.a;
    return vec3f(
        y + co - cg,
        y + cg,
        y - co - cg
    );
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let sampled = textureSample(tex, texSampler, input.uv);
    let rgb = clamp(decode_hapq_ycocg(sampled), vec3f(0.0), vec3f(1.0));
    return vec4f(rgb, 1.0);
}
)";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool is_video_extension(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".mp4" || ext == ".mov" || ext == ".m4v" ||
           ext == ".avi" || ext == ".mkv" || ext == ".webm";
}

static WGPUTextureFormat compressed_format_to_texture(VideoCompressedFormat fmt) {
    switch (fmt) {
        case VideoCompressedFormat::BC1:
            return WGPUTextureFormat_BC1RGBAUnorm;
        case VideoCompressedFormat::BC3:
            return WGPUTextureFormat_BC3RGBAUnorm;
        case VideoCompressedFormat::BC4:
            return WGPUTextureFormat_BC4RUnorm;
        default:
            return WGPUTextureFormat_Undefined;
    }
}

struct MovieFileRing {
    static constexpr uint32_t kCapacity = 240000;

    std::array<float, kCapacity> left{};
    std::array<float, kCapacity> right{};
    std::atomic<uint32_t> write_pos{0};
    std::atomic<uint32_t> read_pos{0};
    std::atomic<uint32_t> epoch{0};
    std::atomic<float> sample_rate{48000.0f};
    std::atomic<float> speed{1.0f};
    std::atomic<double> read_head_time{0.0};
    std::atomic<double> write_head_time{0.0};
    std::atomic<uint8_t> preroll_ready{0};

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
        const uint32_t n = std::min(frames, available_write());
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
        const uint32_t n = std::min(frames, available_read());
        const uint32_t rp = read_pos.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t idx = (rp + i) % kCapacity;
            l_out[i] = left[idx];
            r_out[i] = right[idx];
        }
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

        const double sr = std::max(1.0, static_cast<double>(sample_rate.load(std::memory_order_relaxed)));
        const double spd = std::max(0.0, static_cast<double>(speed.load(std::memory_order_relaxed)));
        const double advance = static_cast<double>(frames) * spd / sr;
        double new_t = read_head_time.load(std::memory_order_relaxed) + advance;
        const double wht = write_head_time.load(std::memory_order_acquire);
        const double buffer_lag = static_cast<double>(available_read()) * spd / sr;
        const double expected_t = wht - buffer_lag;
        const double error = new_t - expected_t;
        if (std::abs(error) > 0.100) {
            new_t = expected_t + advance;
        } else if (std::abs(error) > 0.002) {
            new_t -= error * 0.10;
        }
        read_head_time.store(new_t, std::memory_order_relaxed);
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

class MovieFileFillThread {
public:
    void start(MovieFileRing* ring, AVFAudioExtractor* extractor) {
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

    void update_ring(MovieFileRing* ring) { ring_.store(ring, std::memory_order_release); }
    void update_extractor(AVFAudioExtractor* ext) { extractor_.store(ext, std::memory_order_release); }
    void notify() {
        std::lock_guard<std::mutex> lock(mu_);
        wake_.notify_one();
    }
    void quiesce() { std::lock_guard<std::mutex> lock(pump_mu_); }
    ~MovieFileFillThread() { stop(); }

private:
    static constexpr uint32_t kFillChunk = 2048;
    static constexpr uint32_t kTargetAhead = 96000;
    static constexpr uint32_t kPrerollFrames = 24000;

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
        while (ring->available_read() < kTargetAhead) {
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
        if (ring->preroll_ready.load(std::memory_order_relaxed) == 0 &&
            ring->available_read() >= kPrerollFrames) {
            ring->preroll_ready.store(1, std::memory_order_release);
        }
    }

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<MovieFileRing*> ring_{nullptr};
    std::atomic<AVFAudioExtractor*> extractor_{nullptr};
    std::mutex mu_;
    std::mutex pump_mu_;
    std::condition_variable wake_;
    std::array<float, kFillChunk> fill_left_{};
    std::array<float, kFillChunk> fill_right_{};
};

class MovieFileSession {
public:
    explicit MovieFileSession(std::string node_id) : node_id_(std::move(node_id)) {}
    ~MovieFileSession() {
        fill_thread.stop();
        delete deferred_delete;
        auto* ext = extractor.load(std::memory_order_relaxed);
        delete ext;
    }

    void acquire() { ref_count_.fetch_add(1, std::memory_order_relaxed); }
    int release() { return ref_count_.fetch_sub(1, std::memory_order_relaxed) - 1; }

    void set_source_path(const std::string& path, bool pitch_preserve) {
        if (path == source_path_) return;
        source_path_ = path;
        audio_stats.reset();
        transport.clear_source();
        pending_load.reset();
        fill_thread.update_ring(nullptr);
        fill_thread.update_extractor(nullptr);
        fill_thread.quiesce();

        auto* old = extractor.load(std::memory_order_relaxed);
        extractor.store(nullptr, std::memory_order_release);
        delete deferred_delete;
        deferred_delete = old;
        ring.clear();
        ring.preroll_ready.store(0, std::memory_order_release);
        has_audio.store(false, std::memory_order_release);

        if (source_path_.empty()) return;

        auto result = std::make_shared<AsyncAudioLoad>();
        pending_load = result;
        const std::string load_path = source_path_;
        std::thread([result, load_path, pitch_preserve]() {
            auto* ext = new AVFAudioExtractor();
            ext->set_pitch_preserve(pitch_preserve);
            if (ext->open(load_path)) {
                result->extractor = ext;
                result->success = true;
            } else {
                delete ext;
            }
            result->done.store(true, std::memory_order_release);
        }).detach();
    }

    void update_audio_main(float speed, bool loop, bool pitch_preserve) {
        if (pending_load && pending_load->done.load(std::memory_order_acquire)) {
            if (pending_load->success) {
                auto* old = extractor.load(std::memory_order_relaxed);
                auto* fresh = pending_load->extractor;
                pending_load->extractor = nullptr;
                extractor.store(fresh, std::memory_order_release);
                delete deferred_delete;
                deferred_delete = old;

                ring.clear();
                ring.preroll_ready.store(fresh->has_audio() ? 0 : 1, std::memory_order_release);
                fill_thread.update_extractor(fresh);
                fill_thread.update_ring(&ring);
                if (!fill_thread_started) {
                    fill_thread.start(&ring, fresh);
                    fill_thread_started = true;
                }
                fill_thread.notify();
                has_audio.store(fresh->has_audio(), std::memory_order_release);
                transport.set_source(static_cast<double>(fresh->duration()));
            }
            pending_load.reset();
        }

        auto* ext = extractor.load(std::memory_order_acquire);
        if (!ext || !ext->is_open()) return;
        ext->set_speed(speed);
        ext->set_loop(loop);
        ext->set_pitch_preserve(pitch_preserve);
        ring.speed.store(speed, std::memory_order_release);
        fill_thread.notify();
    }

    double audio_local_time(PlayMode play_mode) const {
        auto* ext = extractor.load(std::memory_order_acquire);
        double mono_time = ring.read_head_time.load(std::memory_order_relaxed);
        const double dur = ext && ext->is_open() ? static_cast<double>(ext->duration()) : transport.duration();
        if (play_mode == PlayMode::HoldLast && dur > 0.0 && mono_time > dur) return dur;
        double out = dur > 0.0 ? wrap_time(mono_time, dur) : mono_time;
        if (out < 1e-6 && mono_time > 0.0) out = 1e-6;
        return out;
    }

    MovieTransport transport;
    MovieFileRing ring;
    MovieFileFillThread fill_thread;
    MovieAudioStats audio_stats{};
    std::atomic<bool> has_audio{false};

private:
    struct AsyncAudioLoad {
        std::atomic<bool> done{false};
        bool success = false;
        AVFAudioExtractor* extractor = nullptr;
        ~AsyncAudioLoad() { delete extractor; }
    };

    std::string node_id_;
    std::string source_path_;
    std::atomic<int> ref_count_{0};
    std::atomic<AVFAudioExtractor*> extractor{nullptr};
    AVFAudioExtractor* deferred_delete = nullptr;
    std::shared_ptr<AsyncAudioLoad> pending_load;
    bool fill_thread_started = false;
};

class MovieFileSessionRegistry {
public:
    static MovieFileSessionRegistry& instance() {
        static MovieFileSessionRegistry registry;
        return registry;
    }

    std::shared_ptr<MovieFileSession> acquire(const std::string& node_id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto& slot = sessions_[node_id];
        if (!slot) slot = std::make_shared<MovieFileSession>(node_id);
        slot->acquire();
        return slot;
    }

    void release(const std::string& node_id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = sessions_.find(node_id);
        if (it == sessions_.end()) return;
        if (it->second->release() <= 0) sessions_.erase(it);
    }

private:
    std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<MovieFileSession>> sessions_;
};
/**
 * @brief Movie file playback with one shared audio/video transport.
 *
 * Plays MP4, MOV, MKV, WebM and other video formats with synchronized texture
 * and audio outputs from one graph node.
 *
 * @see TextureLoader, WebcamIn
 */
struct MovieFile : vivid::OperatorBase, vivid::GpuProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "MovieFile";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::FilePath> file {"file"};
    vivid::Param<int>   play_mode {"play_mode", 0, {"Loop", "Once", "Hold Last"}};
    vivid::Param<float> speed     {"speed", 1.0f, 0.0f, 4.0f};
    vivid::Param<float> volume    {"volume", 1.0f, 0.0f, 2.0f};
    vivid::Param<int>   pitch_preserve {"pitch_preserve", 1, {"Off", "On"}};
    vivid::Param<float> video_phase_offset_ms {"video_phase_offset_ms", 0.0f, -250.0f, 250.0f};

    MovieFile() {
        vivid::semantic_tag(file, "path_video");
        vivid::semantic_shape(file, "path");
        vivid::description(file, "Path to a video, audio, or image file to play");

        vivid::semantic_tag(play_mode, "x_play_mode");
        vivid::semantic_shape(play_mode, "enum");
        vivid::description(play_mode, "What happens at the end: Loop, play Once, or Hold Last frame");

        vivid::semantic_tag(speed, "x_playback_speed");
        vivid::semantic_shape(speed, "scalar");
        vivid::description(speed, "Playback rate multiplier, 1 = normal speed");

        vivid::semantic_tag(volume, "amplitude_linear");
        vivid::semantic_shape(volume, "scalar");
        vivid::description(volume, "Output volume, with 1 = unity gain");

        vivid::semantic_tag(pitch_preserve, "x_pitch_preserve");
        vivid::semantic_shape(pitch_preserve, "enum");
        vivid::description(pitch_preserve, "Keep original pitch when speed is changed");

        vivid::semantic_tag(video_phase_offset_ms, "time_milliseconds");
        vivid::semantic_shape(video_phase_offset_ms, "scalar");
        vivid::semantic_unit(video_phase_offset_ms, "ms");
        vivid::description(video_phase_offset_ms, "Timing offset for audio sync, in milliseconds");

        start_loader_thread();
    }

    ~MovieFile() override {
        stop_loader_thread();
        if (decode_worker_) {
            decode_worker_->stop();
            decode_worker_.reset();
        }
        movie_metal_upload_release(metal_upload_);
        release_session();
        decoder_.reset();
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(pipeline_ycocg_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(shader_ycocg_);
        vivid::gpu::release(sampler_);
        movie_texture_release(texture_);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
        out.push_back(&play_mode);
        out.push_back(&speed);
        out.push_back(&volume);
        out.push_back(&pitch_preserve);
        out.push_back(&video_phase_offset_ms);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
        out.push_back({"audio",   VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        out.push_back({"time",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"duration", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        // Playback telemetry (Stage 1 instrumentation)
        out.push_back({"new_frames",      VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"reused_frames",   VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"nil_frames",      VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"decode_time_us",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"copy_time_us",   VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"upload_time_us",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"drift_ms",        VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"seek_corrections", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"seek_budget_exhausted", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"drop_repeat_corrections", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"buffered_ms", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"underruns",   VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"gpu_native_frames", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"cpu_fallback_frames", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"metal_import_failures", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
        out.push_back({"metal_blit_us", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "analysis"});
    }

    void process_audio(const VividAudioContext* ctx) override {
        ensure_session(ctx->node_id);

        float* L = ctx->output_buffers[1];
        float* R = ctx->output_buffers[1] + ctx->buffer_size;
        const uint32_t n = ctx->buffer_size;

        if (!session_) {
            std::memset(L, 0, n * sizeof(float));
            std::memset(R, 0, n * sizeof(float));
            return;
        }

        auto& ring = session_->ring;
        ring.sample_rate.store(static_cast<float>(ctx->sample_rate), std::memory_order_release);

        if (!session_->has_audio.load(std::memory_order_acquire)) {
            std::memset(L, 0, n * sizeof(float));
            std::memset(R, 0, n * sizeof(float));
        } else if (ring.preroll_ready.load(std::memory_order_acquire) != 0) {
            uint32_t got = ring.read(L, R, n);
            if (got < n) {
                session_->audio_stats.underrun_count.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            std::memset(L, 0, n * sizeof(float));
            std::memset(R, 0, n * sizeof(float));
        }

        const float sr = std::max(1.0f, static_cast<float>(ctx->sample_rate));
        const float spd = std::max(0.001f, ring.speed.load(std::memory_order_relaxed));
        const float buffered_ms = static_cast<float>(ring.available_read()) * spd / sr * 1000.0f;
        session_->audio_stats.buffered_duration_ms.store(buffered_ms, std::memory_order_relaxed);

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

        const auto mode = static_cast<PlayMode>(play_mode.int_value());
        const float out_time = static_cast<float>(session_->audio_local_time(mode));
        const float duration = static_cast<float>(session_->transport.duration());
        const float underruns = static_cast<float>(
            session_->audio_stats.underrun_count.load(std::memory_order_relaxed));

        float* time_buf = ctx->output_buffers[2];
        float* duration_buf = ctx->output_buffers[3];
        float* buffered_buf = ctx->output_buffers[14];
        float* underrun_buf = ctx->output_buffers[15];
        for (uint32_t i = 0; i < n; ++i) {
            time_buf[i] = out_time;
            duration_buf[i] = duration;
            buffered_buf[i] = buffered_ms;
            underrun_buf[i] = underruns;
        }
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[movie_file] lazy_init FAILED\n");
                return;
            }
        }

        ensure_session(ctx->node_id);

        // Read file path from param
        std::string effective_path = file.str_value;

        if (effective_path != last_path_) {
            last_path_ = effective_path;
            on_source_changed(ctx->node_id);
            if (session_) {
                session_->set_source_path(last_path_, pitch_preserve.int_value() != 0);
            }
            if (is_video_extension(last_path_)) {
                request_video_load(last_path_, wgpuDeviceHasFeature(ctx->device,
                    WGPUFeatureName_TextureCompressionBC));
            } else {
                cancel_pending_video_load();
                decoder_.reset();
                if (last_path_.empty()) {
                    show_placeholder(ctx);
                } else {
                    load_image(ctx);
                }
            }
        }

        apply_ready_load_result(ctx);

        if (session_) {
            session_->transport.set_play_mode(static_cast<PlayMode>(play_mode.int_value()));
            session_->transport.set_speed(speed.value);
            session_->update_audio_main(speed.value,
                                        play_mode.int_value() == 0,
                                        pitch_preserve.int_value() != 0);
        }

        // Track the authoritative local time for output publishing
        double published_local_time = decoder_ ? static_cast<double>(decoder_->current_time()) : 0.0;
        double audio_master_mono_target = published_local_time;
        bool audio_master_active = false;

        if (decoder_ && decoder_->is_open() && !placeholder_active_) {
            decoder_->set_loop(play_mode.int_value() == 0);
            decoder_->set_speed(speed.value);

            audio_master_active = session_ && session_->has_audio.load(std::memory_order_acquire);
            if (audio_master_active) {
                auto& transport = session_->transport;
                const double phase_s = static_cast<double>(video_phase_offset_ms.value) * 0.001;
                const float audio_time = static_cast<float>(
                    session_->audio_local_time(static_cast<PlayMode>(play_mode.int_value())));
                const double audio_mono_time =
                    session_->ring.read_head_time.load(std::memory_order_acquire);
                const double desired_local = transport.compute_audio_master_time(audio_time, phase_s);
                const double desired_mono = audio_mono_time + phase_s;
                audio_master_mono_target = desired_mono;

                if (decoder_->compression_mode() == VideoFrameCompressionMode::CompressedBC) {
                    const double decoder_time = std::max(0.0, static_cast<double>(decoder_->current_time()));
                    video_stats_.drift_ms.update(
                        static_cast<float>(std::abs(transport.drift_seconds(desired_local, decoder_time)) * 1000.0));

                    auto decision = transport.evaluate_correction(desired_local, decoder_time, desired_mono, ctx->time);
                    switch (decision.type) {
                        case CorrectionType::None:
                            break;
                        case CorrectionType::DropRepeat:
                            video_stats_.drop_repeat_correction_count++;
                            if (decision.budget_exhausted)
                                video_stats_.seek_budget_exhausted_count++;
                            break;
                        case CorrectionType::Seek:
                            if (decoder_->seek(decision.seek_target)) {
                                video_stats_.seek_correction_count++;
                                transport.record_seek_issued(desired_mono);
                                if (decode_worker_) decode_worker_->flush();
                                last_decoded_frame_.clear();
                            } else {
                                std::fprintf(stderr, "[movie_file] seek to %.3fs failed\n",
                                             decision.seek_target);
                            }
                            break;
                    }
                } else {
                    // AVFoundation frame acquisition is explicitly requested at
                    // desired_local below. AVPlayer's own currentTime is not the
                    // presentation clock in audio-master mode, so do not seek
                    // against it during steady-state playback.
                    video_stats_.drift_ms.update(0.0f);
                }
                published_local_time = desired_local;
            } else {
                double video_local = std::max(0.0, static_cast<double>(decoder_->current_time()));
                if (session_) {
                    published_local_time = session_->transport.compute_self_clock_time(video_local);
                } else {
                    published_local_time = video_local;
                }
            }

            // Decode: codec-specific submit to unified queue
            frame_uploaded_ = false;
            auto t_decode_start = std::chrono::steady_clock::now();

            if (decoder_->compression_mode() == VideoFrameCompressionMode::CompressedBC) {
                // HAP: synchronous decode on main thread, direct queue push
                DecodeStatus status = decoder_->decode_frame();
                if (status == DecodeStatus::NewFrame) {
                    auto* hap = static_cast<HAPDecoder*>(decoder_.get());
                    if (decode_worker_) {
                        auto frame = hap->make_decoded_frame();
                        frame.requested_pts = frame.pts;
                        frame.loop_generation = update_video_loop_generation(
                            published_local_time,
                            play_mode.int_value() == 0,
                            static_cast<double>(decoder_->duration()),
                            audio_master_active,
                            audio_master_mono_target);
                        frame.request_sequence = ++video_request_sequence_;
                        decode_worker_->submit_decoded(std::move(frame));
                    }
                    video_stats_.new_frame_count++;
                } else if (status == DecodeStatus::ReusedFrame) {
                    video_stats_.reused_frame_count++;
                } else {
                    video_stats_.nil_frame_count++;
                }
            } else {
                auto* avf = static_cast<AVFDecoder*>(decoder_.get());
                const auto status = schedule_avf_requests(
                    avf,
                    published_local_time,
                    update_video_loop_generation(published_local_time,
                                                 play_mode.int_value() == 0,
                                                 static_cast<double>(decoder_->duration()),
                                                 audio_master_active,
                                                 audio_master_mono_target),
                    audio_master_active);
                if (status == DecodeStatus::NewFrame) {
                    video_stats_.new_frame_count++;
                } else if (status == DecodeStatus::ReusedFrame) {
                    video_stats_.reused_frame_count++;
                } else if (status == DecodeStatus::NilFrame) {
                    video_stats_.nil_frame_count++;
                }
            }

            auto t_decode_end = std::chrono::steady_clock::now();
            video_stats_.decode_acquire_us.update(
                std::chrono::duration<float, std::micro>(t_decode_end - t_decode_start).count());

            bool got_new_decoded = false;
            DecodedFrame ready;
            const bool avf_bounded_presentation =
                decoder_->compression_mode() != VideoFrameCompressionMode::CompressedBC;
            const double frame_duration = frame_duration_seconds();
            const uint64_t target_loop_generation = update_video_loop_generation(
                published_local_time,
                play_mode.int_value() == 0,
                static_cast<double>(decoder_->duration()),
                audio_master_active,
                audio_master_mono_target);
            const bool popped = decode_worker_ &&
                (avf_bounded_presentation
                    ? decode_worker_->pop_best(published_local_time,
                                               target_loop_generation,
                                               frame_duration,
                                               ready)
                    : decode_worker_->pop_latest(ready));
            if (popped) {
                video_stats_.decode_copy_us.update(ready.copy_time_us);
                last_decoded_frame_ = std::move(ready);
                got_new_decoded = true;
            }

            // Unified upload: only when a genuinely new frame was popped
            if (got_new_decoded && !last_decoded_frame_.empty()) {
                auto t_upload_start = std::chrono::steady_clock::now();
                if (last_decoded_frame_.compressed) {
                    WGPUTextureFormat fmt = compressed_format_to_texture(last_decoded_frame_.compressed_format);
                    if (fmt != WGPUTextureFormat_Undefined && !last_decoded_frame_.data.empty()) {
                        ensure_texture(ctx, last_decoded_frame_.width, last_decoded_frame_.height, fmt, true);
                        movie_upload_compressed(ctx->queue, texture_,
                                               last_decoded_frame_.data.data(),
                                               last_decoded_frame_.data.size(),
                                               last_decoded_frame_.width,
                                               last_decoded_frame_.height, fmt);
                        frame_uploaded_ = true;
                    }
                } else if (last_decoded_frame_.has_native_pixel_buffer()) {
                    ensure_texture(ctx, last_decoded_frame_.width, last_decoded_frame_.height,
                                   WGPUTextureFormat_BGRA8Unorm, false);
                    bool import_failed = false;
                    float metal_us = 0.0f;
                    if (movie_upload_cv_pixel_buffer_metal(ctx->device,
                                                           ctx->queue,
                                                           texture_,
                                                           last_decoded_frame_.native_pixel_buffer.get(),
                                                           metal_upload_,
                                                           &metal_us,
                                                           &import_failed)) {
                        frame_uploaded_ = true;
                        video_stats_.gpu_native_frame_count++;
                        video_stats_.metal_blit_us.update(metal_us);
                    } else {
                        if (import_failed) video_stats_.metal_import_failure_count++;
                        auto fallback = AVFDecoder::copy_pixel_buffer_ref(
                            last_decoded_frame_.native_pixel_buffer.get(),
                            last_decoded_frame_.pts);
                        fallback.loop_generation = last_decoded_frame_.loop_generation;
                        fallback.request_sequence = last_decoded_frame_.request_sequence;
                        fallback.request_key = last_decoded_frame_.request_key;
                        fallback.requested_pts = last_decoded_frame_.requested_pts;
                        if (!fallback.empty()) {
                            video_stats_.cpu_fallback_frame_count++;
                            video_stats_.decode_copy_us.update(fallback.copy_time_us);
                            last_decoded_frame_ = std::move(fallback);
                            ensure_texture(ctx, last_decoded_frame_.width, last_decoded_frame_.height,
                                           WGPUTextureFormat_BGRA8Unorm, false);
                            movie_upload_bgra(ctx->queue, texture_,
                                             last_decoded_frame_.data.data(),
                                             last_decoded_frame_.width,
                                             last_decoded_frame_.height);
                            frame_uploaded_ = true;
                        }
                    }
                } else {
                    if (last_decoded_frame_.cpu_fallback) {
                        video_stats_.cpu_fallback_frame_count++;
                    }
                    ensure_texture(ctx, last_decoded_frame_.width, last_decoded_frame_.height,
                                   WGPUTextureFormat_BGRA8Unorm, false);
                    movie_upload_bgra(ctx->queue, texture_,
                                     last_decoded_frame_.data.data(),
                                     last_decoded_frame_.width,
                                     last_decoded_frame_.height);
                    frame_uploaded_ = true;
                }
                auto t_upload_end = std::chrono::steady_clock::now();
                video_stats_.gpu_upload_us.update(
                    std::chrono::duration<float, std::micro>(t_upload_end - t_upload_start).count());
            }
        }

        if (texture_.width > 0 && texture_.height > 0) {
            vivid_request_output_size(ctx, texture_.width, texture_.height);
        }

        if (texture_.view && texture_.bind_group) {
            WGPURenderPipeline active = pipeline_;
            if (!last_decoded_frame_.empty() && last_decoded_frame_.requires_ycocg && pipeline_ycocg_) {
                active = pipeline_ycocg_;
            }
            // Rebuild bind group after texture content upload — wgpu-native
            // bind groups may not observe wgpuQueueWriteTexture updates.
            if (frame_uploaded_)
                movie_texture_rebuild_bind_group(ctx->device, sampler_, bind_layout_, texture_);
            vivid::gpu::run_pass(ctx->command_encoder, active, texture_.bind_group,
                                 ctx->output_texture_view, "MovieFile Blit");
        } else {
            clear_output(ctx);
        }

        // Publish time and telemetry outputs
        if (ctx->output_values) {
            ctx->output_values[2] = static_cast<float>(published_local_time);
            ctx->output_values[3] = decoder_ ? decoder_->duration() : 0.0f;
            ctx->output_values[4] = static_cast<float>(video_stats_.new_frame_count);
            ctx->output_values[5] = static_cast<float>(video_stats_.reused_frame_count);
            ctx->output_values[6] = static_cast<float>(video_stats_.nil_frame_count);
            ctx->output_values[7] = video_stats_.decode_acquire_us.value.load(std::memory_order_relaxed);
            ctx->output_values[8] = video_stats_.decode_copy_us.value.load(std::memory_order_relaxed);
            ctx->output_values[9] = video_stats_.gpu_upload_us.value.load(std::memory_order_relaxed);
            ctx->output_values[10] = video_stats_.drift_ms.value.load(std::memory_order_relaxed);
            ctx->output_values[11] = static_cast<float>(video_stats_.seek_correction_count);
            ctx->output_values[12] = static_cast<float>(video_stats_.seek_budget_exhausted_count);
            ctx->output_values[13] = static_cast<float>(video_stats_.drop_repeat_correction_count);
            if (session_) {
                ctx->output_values[14] = session_->audio_stats.buffered_duration_ms.load(std::memory_order_relaxed);
                ctx->output_values[15] = static_cast<float>(
                    session_->audio_stats.underrun_count.load(std::memory_order_relaxed));
            }
            ctx->output_values[16] = static_cast<float>(video_stats_.gpu_native_frame_count);
            ctx->output_values[17] = static_cast<float>(video_stats_.cpu_fallback_frame_count);
            ctx->output_values[18] = static_cast<float>(video_stats_.metal_import_failure_count);
            ctx->output_values[19] = video_stats_.metal_blit_us.value.load(std::memory_order_relaxed);
        }
    }

private:
    struct AvfFrameRequest {
        double pts = 0.0;
        uint64_t loop_generation = 0;
        bool primary = false;
        bool allow_fallback = false;
    };

    double frame_duration_seconds() const {
        const float fps = decoder_ ? decoder_->frame_rate() : 0.0f;
        return fps > 1.0f ? 1.0 / static_cast<double>(fps) : 1.0 / 30.0;
    }

    uint64_t update_video_loop_generation(double target_pts,
                                          bool loop,
                                          double duration,
                                          bool audio_master,
                                          double monotonic_target) {
        if (!loop || duration <= 0.0) {
            video_loop_generation_ = 0;
            last_video_target_valid_ = false;
            return 0;
        }

        if (audio_master) {
            const double mono = std::max(0.0, monotonic_target);
            video_loop_generation_ = static_cast<uint64_t>(std::floor(mono / duration));
            last_video_target_pts_ = target_pts;
            last_video_target_valid_ = true;
            return video_loop_generation_;
        }

        if (!last_video_target_valid_) {
            last_video_target_pts_ = target_pts;
            last_video_target_valid_ = true;
            return video_loop_generation_;
        }

        if (target_pts + duration * 0.5 < last_video_target_pts_) {
            video_loop_generation_++;
        } else if (target_pts > last_video_target_pts_ + duration * 0.5 &&
                   video_loop_generation_ > 0) {
            video_loop_generation_--;
        }
        last_video_target_pts_ = target_pts;
        return video_loop_generation_;
    }

    static uint64_t avf_request_key(uint64_t loop_generation,
                                    double pts,
                                    double frame_duration) {
        const double fd = std::max(1.0 / 240.0, frame_duration);
        const auto frame_index = static_cast<uint64_t>(
            std::max<int64_t>(0, static_cast<int64_t>(std::llround(pts / fd))));
        return ((loop_generation + 1ULL) << 32) ^ (frame_index & 0xffffffffULL);
    }

    static double clamp_frame_time(double pts, double duration, double frame_duration) {
        if (duration <= 0.0) return std::max(0.0, pts);
        const double last_reasonable = std::max(0.0, duration - std::max(1e-6, frame_duration * 0.25));
        return std::clamp(pts, 0.0, last_reasonable);
    }

    static void add_unique_request(std::vector<AvfFrameRequest>& requests,
                                   double pts,
                                   uint64_t loop_generation,
                                   bool primary,
                                   bool allow_fallback,
                                   double duration,
                                   double frame_duration) {
        pts = clamp_frame_time(pts, duration, frame_duration);
        const uint64_t key = avf_request_key(loop_generation, pts, frame_duration);
        for (const auto& request : requests) {
            if (avf_request_key(request.loop_generation, request.pts, frame_duration) == key) {
                return;
            }
        }
        requests.push_back({pts, loop_generation, primary, allow_fallback});
    }

    DecodeStatus submit_avf_request(AVFDecoder* avf,
                                    const AvfFrameRequest& request,
                                    double frame_duration,
                                    bool target_time_mode) {
        if (!avf || !decode_worker_) return DecodeStatus::NilFrame;

        if (target_time_mode) {
            const double lookahead = request.primary ? frame_duration : 0.0;
            auto acquired_frames = avf->read_pixel_buffers_until(request.pts,
                                                                 lookahead,
                                                                 request.loop_generation);
            bool submitted_any = false;
            for (auto& acquired : acquired_frames) {
                auto frame = AVFDecoder::make_native_frame(std::move(acquired));
                if (frame.empty()) continue;
                frame.requested_pts = request.pts;
                frame.loop_generation = request.loop_generation;
                frame.request_sequence = ++video_request_sequence_;
                frame.request_key = avf_request_key(request.loop_generation,
                                                    frame.pts,
                                                    frame_duration);
                submitted_any = decode_worker_->submit_decoded(std::move(frame)) || submitted_any;
            }
            return submitted_any ? DecodeStatus::NewFrame : DecodeStatus::ReusedFrame;
        }

        AcquiredPixelBuffer acquired;
        acquired = avf->acquire_pixel_buffer();
        if (!acquired.valid()) {
            return acquired.status;
        }

        auto frame = AVFDecoder::make_native_frame(std::move(acquired));
        if (!frame.empty()) {
            frame.requested_pts = request.pts;
            frame.loop_generation = request.loop_generation;
            frame.request_sequence = ++video_request_sequence_;
            frame.request_key = avf_request_key(request.loop_generation,
                                                frame.pts,
                                                frame_duration);
            const bool submitted = decode_worker_->submit_decoded(std::move(frame));
            return submitted ? DecodeStatus::NewFrame : DecodeStatus::ReusedFrame;
        }
        return DecodeStatus::NilFrame;
    }

    DecodeStatus schedule_avf_requests(AVFDecoder* avf,
                                       double target_pts,
                                       uint64_t loop_generation,
                                       bool audio_master) {
        if (!avf) return DecodeStatus::NilFrame;

        const double duration = decoder_ ? static_cast<double>(decoder_->duration()) : 0.0;
        const double frame_duration = frame_duration_seconds();
        const bool loop = play_mode.int_value() == 0 && duration > 0.0;
        std::vector<AvfFrameRequest> requests;
        requests.reserve(10);
        add_unique_request(requests,
                           target_pts,
                           loop_generation,
                           true,
                           audio_master,
                           duration,
                           frame_duration);

        if (loop && audio_master) {
            const double window = std::max(4.0 * frame_duration, 0.120);
            if (target_pts >= duration - window) {
                for (int i = 4; i >= 1; --i) {
                    add_unique_request(requests,
                                       duration - static_cast<double>(i) * frame_duration,
                                       loop_generation,
                                       false,
                                       true,
                                       duration,
                                       frame_duration);
                }
                for (int i = 0; i < 4; ++i) {
                    add_unique_request(requests,
                                       static_cast<double>(i) * frame_duration,
                                       loop_generation + 1,
                                       false,
                                       true,
                                       duration,
                                       frame_duration);
                }
            }

            if (target_pts <= window &&
                decode_worker_ &&
                !decode_worker_->has_ready_generation(loop_generation)) {
                for (int i = 0; i < 4; ++i) {
                    add_unique_request(requests,
                                       static_cast<double>(i) * frame_duration,
                                       loop_generation,
                                       false,
                                       true,
                                       duration,
                                       frame_duration);
                }
            }
        }

        DecodeStatus primary_status = DecodeStatus::NilFrame;
        for (const auto& request : requests) {
            const DecodeStatus status = submit_avf_request(avf, request, frame_duration, audio_master);
            if (request.primary) {
                primary_status = status;
            } else if (status == DecodeStatus::NewFrame) {
                video_stats_.new_frame_count++;
            }
        }
        return primary_status;
    }

    // ---- Loader thread (async video decode) ---------------------------------

    struct LoadRequest {
        uint64_t generation = 0;
        std::string path;
        bool bc_supported = false;
    };

    struct LoadResult {
        uint64_t generation = 0;
        std::string path;
        bool success = false;
        std::unique_ptr<VideoDecoder> decoder;
        std::string diagnostics;
    };

    void start_loader_thread() {
        loader_finished_.store(false, std::memory_order_release);
        loader_thread_ = std::thread([this]() {
            for (;;) {
                LoadRequest req;
                std::shared_ptr<std::atomic<bool>> cancel;
                {
                    std::unique_lock<std::mutex> lock(loader_mu_);
                    loader_cv_.wait(lock, [this]() {
                        return loader_stop_ || loader_has_request_;
                    });
                    if (loader_stop_) break;

                    req = loader_request_;
                    loader_has_request_ = false;
                    cancel = load_coordinator_.begin_active();
                }

                auto loaded = load_video_decoder_for_path(req.path, req.bc_supported, cancel.get());
                if (cancel->load(std::memory_order_acquire)) {
                    continue;
                }

                LoadResult out;
                out.generation = req.generation;
                out.path = req.path;
                out.success = loaded.success;
                out.decoder = std::move(loaded.decoder);
                out.diagnostics = loaded.diagnostics;

                std::lock_guard<std::mutex> lock(loader_mu_);
                if (!loader_stop_) {
                    loader_ready_result_ = std::move(out);
                }
                load_coordinator_.clear_active(cancel);
            }
            loader_finished_.store(true, std::memory_order_release);
        });
    }

    void stop_loader_thread() {
        {
            std::lock_guard<std::mutex> lock(loader_mu_);
            loader_stop_ = true;
            load_coordinator_.cancel_all();
            loader_cv_.notify_one();
        }
        if (!loader_thread_.joinable()) return;

#ifdef __APPLE__
        if (CFRunLoopGetCurrent() == CFRunLoopGetMain()) {
            while (!loader_finished_.load(std::memory_order_acquire)) {
                CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, false);
            }
        }
#endif

        loader_thread_.join();
    }

    void request_video_load(const std::string& path, bool bc_supported) {
        std::lock_guard<std::mutex> lock(loader_mu_);
        uint64_t generation = load_coordinator_.request_next();
        loader_request_ = LoadRequest{generation, path, bc_supported};
        loader_has_request_ = true;
        loader_cv_.notify_one();
    }

    void cancel_pending_video_load() {
        std::lock_guard<std::mutex> lock(loader_mu_);
        load_coordinator_.cancel_pending();
        loader_has_request_ = false;
    }

    void apply_ready_load_result(const VividGpuContext* gpu) {
        std::optional<LoadResult> ready;
        {
            std::lock_guard<std::mutex> lock(loader_mu_);
            if (loader_ready_result_) {
                ready = std::move(loader_ready_result_);
                loader_ready_result_.reset();
            }
        }
        if (!ready) return;

        if (!load_coordinator_.should_apply(ready->generation)) return;

        load_coordinator_.mark_applied(ready->generation);
        if (ready->success && ready->decoder) {
            decoder_ = std::move(ready->decoder);
            movie_texture_release(texture_);
            decoder_->set_loop(play_mode.int_value() == 0);
            decoder_->set_speed(speed.value);
            if (session_) {
                session_->transport.set_source(static_cast<double>(decoder_->duration()));
                session_->transport.set_frame_rate(decoder_->frame_rate());
            }
            // Ensure decode worker is running for unified queue pipeline
            if (!decode_worker_) {
                decode_worker_ = std::make_unique<VideoDecodeWorker>();
                decode_worker_->start();
            } else {
                decode_worker_->flush();
            }
            placeholder_active_ = false;
        } else {
            decoder_.reset();
            show_placeholder(gpu);
        }
    }

    // ---- Source tracking -------------------------------------------------------

    void on_source_changed(const char* node_id) {
        if (decode_worker_) decode_worker_->flush();
        last_decoded_frame_.clear();
        video_loop_generation_ = 0;
        video_request_sequence_ = 0;
        last_video_target_pts_ = 0.0;
        last_video_target_valid_ = false;
        ensure_session(node_id);
        if (session_)
            session_->transport.clear_source();
        video_stats_.reset();
    }

    void ensure_session(const char* node_id) {
        if (!node_id || !*node_id) return;
        if (session_node_id_ == node_id && session_) return;
        release_session();
        session_node_id_ = node_id;
        session_ = MovieFileSessionRegistry::instance().acquire(session_node_id_);
    }

    void release_session() {
        if (!session_) return;
        MovieFileSessionRegistry::instance().release(session_node_id_);
        session_.reset();
        session_node_id_.clear();
    }

    // ---- Texture management -------------------------------------------------

    void ensure_texture(const VividGpuContext* gpu,
                        uint32_t w, uint32_t h,
                        WGPUTextureFormat format, bool compressed) {
        if (texture_.width == w && texture_.height == h &&
            texture_.format == format && texture_.compressed == compressed &&
            texture_.texture && texture_.bind_group && texture_.view) {
            return;
        }
        movie_texture_recreate(gpu->device, sampler_, bind_layout_, texture_,
                               w, h, format, compressed);
    }

    void show_placeholder(const VividGpuContext* gpu) {
        auto frame = make_movie_missing_placeholder();
        ensure_texture(gpu, frame.width, frame.height, WGPUTextureFormat_BGRA8Unorm, false);
        movie_upload_bgra(gpu->queue, texture_, frame.bgra.data(), frame.width, frame.height);
        placeholder_active_ = true;
    }

    void load_image(const VividGpuContext* gpu) {
        int w = 0, h = 0, channels = 0;
        uint8_t* data = stbi_load(last_path_.c_str(), &w, &h, &channels, 4);
        if (!data) {
            std::fprintf(stderr, "[movie_file] Failed to load image: %s\n", last_path_.c_str());
            show_placeholder(gpu);
            return;
        }
        // stb_image returns RGBA; convert to BGRA
        for (int i = 0; i < w * h; ++i) {
            std::swap(data[i * 4 + 0], data[i * 4 + 2]);
        }
        ensure_texture(gpu, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                       WGPUTextureFormat_BGRA8Unorm, false);
        movie_upload_bgra(gpu->queue, texture_, data,
                          static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        stbi_image_free(data);
        placeholder_active_ = false;
    }

    void clear_output(const VividGpuContext* gpu) {
        if (!gpu->output_texture_view) return;
        WGPURenderPassColorAttachment color_att{};
        color_att.view = gpu->output_texture_view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = {0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv("MovieFile Clear");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(gpu->command_encoder, &rp_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    // ---- GPU pipeline init --------------------------------------------------

    bool lazy_init(const VividGpuContext* gpu) {
        shader_ = vivid::gpu::create_shader(gpu->device, kBlitFragment, "MovieFile Shader");
        shader_ycocg_ = vivid::gpu::create_shader(gpu->device, kBlitFragmentYCoCg, "MovieFile YCoCg Shader");
        if (!shader_ || !shader_ycocg_) return false;

        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "MovieFile Sampler");

        WGPUBindGroupLayoutEntry entries[2]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].texture.sampleType = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[1].texture.multisampled = false;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("MovieFile BGL");
        bgl_desc.entryCount = 2;
        bgl_desc.entries = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("MovieFile Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                gpu->output_format, "MovieFile Pipeline");
        pipeline_ycocg_ = vivid::gpu::create_pipeline(gpu->device, shader_ycocg_, pipe_layout_,
                                                       gpu->output_format, "MovieFile YCoCg Pipeline");
        return pipeline_ != nullptr && pipeline_ycocg_ != nullptr;
    }

    // ---- State --------------------------------------------------------------

    WGPURenderPipeline  pipeline_ = nullptr;
    WGPURenderPipeline  pipeline_ycocg_ = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUShaderModule    shader_ = nullptr;
    WGPUShaderModule    shader_ycocg_ = nullptr;
    WGPUSampler         sampler_ = nullptr;

    MovieTextureState texture_{};

    std::string last_path_;
    std::unique_ptr<VideoDecoder> decoder_;
    bool placeholder_active_ = false;
    bool frame_uploaded_ = false;
    MovieVideoStats video_stats_{};
    std::shared_ptr<MovieFileSession> session_;
    std::string session_node_id_;
    std::unique_ptr<VideoDecodeWorker> decode_worker_;
    DecodedFrame last_decoded_frame_;
    MovieMetalUploadState metal_upload_{};
    uint64_t video_loop_generation_ = 0;
    uint64_t video_request_sequence_ = 0;
    double last_video_target_pts_ = 0.0;
    bool last_video_target_valid_ = false;

    std::mutex loader_mu_;
    std::condition_variable loader_cv_;
    bool loader_stop_ = false;
    bool loader_has_request_ = false;
    LoadRequest loader_request_{};
    std::optional<LoadResult> loader_ready_result_;
    MovieLoadCoordinator load_coordinator_{};
    std::thread loader_thread_;
    std::atomic<bool> loader_finished_{false};
    float prev_volume_ = 1.0f;
};

VIVID_DEFINE_OP(MovieFile) {
}

VIVID_REGISTER(MovieFile)
