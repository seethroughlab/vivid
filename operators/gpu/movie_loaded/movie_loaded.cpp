#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/media_clock.h"
#include "operator_api/media_stream.h"
#include "../../shared/movie_decode/video_decoder.h"
#include "../../shared/movie_decode/decoder_factory.h"
#include "../../shared/movie_decode/texture_upload.h"
#include "../../shared/movie_decode/placeholder_frame.h"
#include "../../shared/movie_decode/load_generation.h"
#include "../../shared/media_session/media_session.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

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

static void log_load_event(const char* event,
                           const std::string& path,
                           uint64_t generation,
                           const std::string& details) {
    std::fprintf(stderr, "[movie_loaded] %s gen=%llu path='%s' %s\n",
                 event,
                 static_cast<unsigned long long>(generation),
                 path.c_str(),
                 details.c_str());
}

struct MovieLoaded : vivid::OperatorBase {
    static constexpr const char* kName   = "MovieLoaded";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::FilePath> file {"file"};
    vivid::Param<int>   play_mode {"play_mode", 0, {"Loop", "Once", "Hold Last"}};
    vivid::Param<float> speed     {"speed", 1.0f, 0.0f, 4.0f};
    vivid::Param<float> video_phase_offset_ms {"video_phase_offset_ms", 0.0f, -250.0f, 250.0f};

    MovieLoaded() {
        vivid::semantic_tag(file, "path_video");
        vivid::semantic_shape(file, "path");

        vivid::semantic_tag(play_mode, "x_play_mode");
        vivid::semantic_shape(play_mode, "enum");

        vivid::semantic_tag(speed, "x_playback_speed");
        vivid::semantic_shape(speed, "scalar");

        vivid::semantic_tag(video_phase_offset_ms, "time_offset_ms");
        vivid::semantic_shape(video_phase_offset_ms, "scalar");
        vivid::semantic_unit(video_phase_offset_ms, "ms");

        start_loader_thread();
    }

    ~MovieLoaded() override {
        if (shared_handles_ && handle_id_ != 0) {
            shared_handles_->invalidate(handle_id_, media_clock_.source_generation);
            shared_handles_->release(handle_id_);
            handle_id_ = 0;
        }
        stop_loader_thread();
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
        out.push_back(&video_phase_offset_ms);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
        out.push_back({"time", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"speed", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"media_clock", VIVID_PORT_DATA, VIVID_PORT_OUTPUT, "media_stream_v1"});
    }

    void process(VividProcessContext* ctx) override {
        if (ctx && ctx->shared_handles) {
            shared_handles_ = ctx->shared_handles;
        }
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[movie_loaded] lazy_init FAILED\n");
                return;
            }
        }

        // Read file path from param (scheduler updates it if wired)
        std::string effective_path = file.str_value;

        if (effective_path != last_path_) {
            last_path_ = effective_path;
            on_source_changed();
            if (is_video_extension(last_path_)) {
                request_video_load(last_path_, wgpuDeviceHasFeature(gpu->device,
                    WGPUFeatureName_TextureCompressionBC));
            } else {
                cancel_pending_video_load();
                decoder_.reset();
                if (last_path_.empty()) {
                    show_placeholder(gpu);
                } else {
                    load_image(gpu);
                }
            }
        }

        apply_ready_load_result(gpu);
        publish_transport_state_if_changed();

        if (decoder_ && decoder_->is_open() && !placeholder_active_) {
            decoder_->set_loop(play_mode.int_value() == 0);
            decoder_->set_speed(speed.value);

            // Check whether audio preroll is complete before decoding/syncing.
            const bool preroll_ready = !session_ ||
                session_->audio_preroll_ready.load(std::memory_order_acquire) != 0;

            if (session_ && preroll_ready) {
                const uint64_t audio_frames = session_->audio_frames_read.load(std::memory_order_acquire);
                const uint64_t generation = media_clock_.source_generation;
                if (audio_frames > 0 &&
                    session_->source_generation.load(std::memory_order_acquire) == generation) {
                    const double duration_s = std::max(0.0, static_cast<double>(decoder_->duration()));
                    const double desired_mono = session_->audio_read_head_media_time.load(std::memory_order_acquire);
                    const double phase_offset_s = static_cast<double>(video_phase_offset_ms.value) * 0.001;
                    const double desired_video_mono = desired_mono + phase_offset_s;
                    const double desired_local = wrap_time(desired_video_mono, duration_s);
                    const double video_local = std::max(0.0, static_cast<double>(decoder_->current_time()));
                    const double err = shortest_circular_diff(desired_local, video_local, duration_s);
                    const double frame_dur = 1.0 / std::max(1.0, static_cast<double>(decoder_->frame_rate()));
                    const double kVideoSeekErrSec = frame_dur * 1.5;
                    constexpr double kVideoSeekCooldownSec = 0.030;
                    const bool generation_changed = (last_video_sync_seek_generation_ != generation);
                    const bool cooldown_ok = std::abs(desired_video_mono - last_video_sync_seek_mono_s_) > kVideoSeekCooldownSec;
                    if (session_->video_frame_counter % 60 == 0) {
                        std::fprintf(stderr,
                            "[movie_loaded] SYNC  err=%.4f  desired_local=%.4f  video_local=%.4f  "
                            "gen_changed=%d  cooldown_ok=%d  desired_mono=%.4f  last_seek=%.4f  thresh=%.4f\n",
                            err, desired_local, video_local,
                            generation_changed ? 1 : 0, cooldown_ok ? 1 : 0,
                            desired_video_mono, last_video_sync_seek_mono_s_, kVideoSeekErrSec);
                    }
                    if (generation_changed || (std::abs(err) > kVideoSeekErrSec && cooldown_ok)) {
                        if (decoder_->seek(desired_local)) {
                            last_video_sync_seek_mono_s_ = desired_video_mono;
                            last_video_sync_seek_generation_ = generation;
                        }
                    }
                }
            }

            if (preroll_ready && decoder_->decode_frame()) {
                uint32_t w = decoder_->width();
                uint32_t h = decoder_->height();
                if (w > 0 && h > 0) {
                    if (decoder_->compression_mode() == VideoFrameCompressionMode::CompressedBC) {
                        WGPUTextureFormat fmt = compressed_format_to_texture(decoder_->compressed_format());
                        const uint8_t* data = decoder_->compressed_data();
                        size_t data_size = decoder_->compressed_size();
                        if (fmt != WGPUTextureFormat_Undefined && data && data_size > 0) {
                            enqueue_video_payload(data, data_size, w, h,
                                                  static_cast<uint32_t>(fmt),
                                                  true,
                                                  decoder_->requires_ycocg_decode());
                            ensure_texture(gpu, w, h, fmt, true);
                            movie_upload_compressed(gpu->queue, texture_, data, data_size, w, h, fmt);
                        }
                    } else {
                        const uint8_t* pixels = decoder_->pixel_data();
                        if (pixels) {
                            const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
                            enqueue_video_payload(pixels, bytes, w, h,
                                                  static_cast<uint32_t>(WGPUTextureFormat_BGRA8Unorm),
                                                  false, false);
                            ensure_texture(gpu, w, h, WGPUTextureFormat_BGRA8Unorm, false);
                            movie_upload_bgra(gpu->queue, texture_, pixels, w, h);
                        }
                    }
                }
            }
        }

        if (texture_.width > 0 && texture_.height > 0) {
            ctx->preferred_tex_width = texture_.width;
            ctx->preferred_tex_height = texture_.height;
        }

        if (texture_.view && texture_.bind_group) {
            WGPURenderPipeline active = pipeline_;
            if (decoder_ && decoder_->compression_mode() == VideoFrameCompressionMode::CompressedBC &&
                decoder_->requires_ycocg_decode() && pipeline_ycocg_) {
                active = pipeline_ycocg_;
            }
            vivid::gpu::run_pass(gpu->command_encoder, active, texture_.bind_group,
                                 gpu->output_texture_view, "MovieLoaded Blit");
        } else {
            clear_output(gpu);
        }

        update_and_publish_media_clock(ctx);
        if (ctx->output_values) {
            // Time output mirrors authoritative media clock (audio-master when available).
            ctx->output_values[1] = static_cast<float>(media_clock_.local_time_s);
            ctx->output_values[2] = speed.value;
        }
    }

private:
    static double wrap_time(double t, double duration) {
        if (duration <= 0.0) return std::max(0.0, t);
        double out = std::fmod(t, duration);
        if (out < 0.0) out += duration;
        return out;
    }

    static double shortest_circular_diff(double target, double current, double duration) {
        if (duration <= 0.0) return target - current;
        double d = target - current;
        const double half = duration * 0.5;
        while (d > half) d -= duration;
        while (d < -half) d += duration;
        return d;
    }

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
    vivid::MediaClockV1 media_clock_{};
    vivid::MediaStreamV1 media_stream_{};
    double last_local_time_s_ = 0.0;
    uint64_t handle_id_ = 0;
    const VividSharedHandleService* shared_handles_ = nullptr;
    std::unique_ptr<vivid::media::MediaSession> session_;

    std::mutex loader_mu_;
    std::condition_variable loader_cv_;
    bool loader_stop_ = false;
    bool loader_has_request_ = false;
    LoadRequest loader_request_{};
    std::optional<LoadResult> loader_ready_result_;
    MovieLoadCoordinator load_coordinator_{};
    std::thread loader_thread_;
    float last_cmd_speed_ = 1.0f;
    int last_cmd_play_mode_ = 0;
    double last_video_sync_seek_mono_s_ = -1000.0;
    uint64_t last_video_sync_seek_generation_ = 0;

    void ensure_texture(VividGpuState* gpu,
                        uint32_t w,
                        uint32_t h,
                        WGPUTextureFormat format,
                        bool compressed) {
        if (texture_.width == w && texture_.height == h &&
            texture_.format == format && texture_.compressed == compressed &&
            texture_.texture && texture_.bind_group && texture_.view) {
            return;
        }
        movie_texture_recreate(gpu->device, sampler_, bind_layout_, texture_,
                               w, h, format, compressed);
    }

    void show_placeholder(VividGpuState* gpu) {
        auto frame = make_movie_missing_placeholder();
        ensure_texture(gpu, frame.width, frame.height, WGPUTextureFormat_BGRA8Unorm, false);
        movie_upload_bgra(gpu->queue, texture_, frame.bgra.data(), frame.width, frame.height);
        placeholder_active_ = true;
    }

    void load_image(VividGpuState* gpu) {
        int w = 0, h = 0, channels = 0;
        uint8_t* data = stbi_load(last_path_.c_str(), &w, &h, &channels, 4);
        if (!data) {
            std::fprintf(stderr, "[movie_loaded] Failed to load image: %s\n", last_path_.c_str());
            show_placeholder(gpu);
            return;
        }

        for (int i = 0; i < w * h; ++i) {
            std::swap(data[i * 4 + 0], data[i * 4 + 2]);
        }

        ensure_texture(gpu, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                       WGPUTextureFormat_BGRA8Unorm, false);
        movie_upload_bgra(gpu->queue, texture_, data,
                          static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        stbi_image_free(data);
        placeholder_active_ = false;
        std::fprintf(stderr, "[movie_loaded] Loaded image: %s (%dx%d)\n", last_path_.c_str(), w, h);
    }

    void request_video_load(const std::string& path, bool bc_supported) {
        std::lock_guard<std::mutex> lock(loader_mu_);
        uint64_t generation = load_coordinator_.request_next();
        loader_request_ = LoadRequest{generation, path, bc_supported};
        loader_has_request_ = true;
        loader_cv_.notify_one();
        log_load_event("load_request", path, generation,
                       bc_supported ? "bc=yes" : "bc=no");
    }

    void cancel_pending_video_load() {
        std::lock_guard<std::mutex> lock(loader_mu_);
        load_coordinator_.cancel_pending();
        loader_has_request_ = false;
    }

    void apply_ready_load_result(VividGpuState* gpu) {
        std::optional<LoadResult> ready;
        {
            std::lock_guard<std::mutex> lock(loader_mu_);
            if (loader_ready_result_) {
                ready = std::move(loader_ready_result_);
                loader_ready_result_.reset();
            }
        }
        if (!ready) return;

        if (!load_coordinator_.should_apply(ready->generation)) {
            log_load_event("load_drop_stale", ready->path, ready->generation, ready->diagnostics);
            return;
        }

        load_coordinator_.mark_applied(ready->generation);
        if (ready->success && ready->decoder) {
            decoder_ = std::move(ready->decoder);
            movie_texture_release(texture_);
            decoder_->set_loop(play_mode.int_value() == 0);
            decoder_->set_speed(speed.value);
            placeholder_active_ = false;
            // New decoder/source instance became authoritative.
            on_source_changed();
            log_load_event("load_success", ready->path, ready->generation, ready->diagnostics);
        } else {
            log_load_event("load_failed", ready->path, ready->generation, ready->diagnostics);
            decoder_.reset();
            show_placeholder(gpu);
        }
    }

    void on_source_changed() {
        media_clock_.source_generation += 1;
        media_clock_.loop_epoch = 0;
        last_local_time_s_ = 0.0;
        last_video_sync_seek_mono_s_ = -1000.0;
        if (!session_) session_ = std::make_unique<vivid::media::MediaSession>();
        vivid::media::media_session_note_generation_transition(*session_);
        {
            std::lock_guard<std::mutex> lock(session_->mu);
            session_->source_path = last_path_;
        }
        if (shared_handles_ && handle_id_ != 0) {
            shared_handles_->invalidate(handle_id_, media_clock_.source_generation);
            shared_handles_->release(handle_id_);
            handle_id_ = 0;
        }
        handle_id_ = shared_handles_
            ? shared_handles_->create("media_stream_v1", session_.get(), media_clock_.source_generation)
            : 0;
        vivid::media::TransportCommand cmd;
        cmd.type = vivid::media::TransportCommandType::SetSource;
        cmd.generation = media_clock_.source_generation;
        cmd.source_path = last_path_;
        cmd.speed = speed.value;
        cmd.playing = speed.value > 0.0f ? 1u : 0u;
        cmd.loop_enabled = (play_mode.int_value() == 0) ? 1u : 0u;
        vivid::media::media_session_enqueue_command(*session_, std::move(cmd));
    }

    void publish_transport_state_if_changed() {
        if (!session_) return;
        const float cur_speed = speed.value;
        const int cur_mode = play_mode.int_value();
        if (cur_speed == last_cmd_speed_ && cur_mode == last_cmd_play_mode_) return;
        last_cmd_speed_ = cur_speed;
        last_cmd_play_mode_ = cur_mode;
        vivid::media::TransportCommand cmd;
        cmd.type = vivid::media::TransportCommandType::SetPlayback;
        cmd.generation = media_clock_.source_generation;
        cmd.speed = cur_speed;
        cmd.playing = cur_speed > 0.0f ? 1u : 0u;
        cmd.loop_enabled = (cur_mode == 0) ? 1u : 0u;
        vivid::media::media_session_enqueue_command(*session_, std::move(cmd));
    }

    void enqueue_video_payload(const uint8_t* data,
                               size_t byte_count,
                               uint32_t width,
                               uint32_t height,
                               uint32_t format,
                               bool compressed,
                               bool ycocg) {
        if (!session_ || !data || byte_count == 0) return;
        vivid::media::VideoFramePayload payload;
        payload.generation = media_clock_.source_generation;
        payload.frame_index = session_->video_frame_counter + 1;
        payload.monotonic_time_s = media_clock_.monotonic_time_s;
        payload.width = width;
        payload.height = height;
        payload.format = format;
        payload.ycocg_encoded = ycocg ? 1u : 0u;
        payload.compression_mode = compressed
            ? vivid::media::VideoFrameCompressionMode::CompressedBC
            : vivid::media::VideoFrameCompressionMode::UncompressedBGRA;
        payload.bytes.assign(data, data + byte_count);
        vivid::media::media_session_enqueue_video_frame(*session_, std::move(payload));
    }

    void update_and_publish_media_clock(const VividProcessContext* ctx) {
        const double duration_s = decoder_ ? std::max(0.0, static_cast<double>(decoder_->duration())) : 0.0;
        const double decoder_local_time_s = decoder_ ? std::max(0.0, static_cast<double>(decoder_->current_time())) : 0.0;
        const bool loop_enabled = (play_mode.int_value() == 0);
        const bool playing = (decoder_ && speed.value > 0.0f && !placeholder_active_);
        double local_time_s = decoder_local_time_s;
        double monotonic_time_s = decoder_local_time_s;
        bool using_audio_master = false;

        // Audio-master authority: when audio is active, publish clock from audio read-head.
        if (session_) {
            const uint64_t read_frames = session_->audio_frames_read.load(std::memory_order_acquire);
            const double audio_mono = session_->audio_read_head_media_time.load(std::memory_order_acquire);
            if (read_frames > 0 && audio_mono >= 0.0) {
                using_audio_master = true;
                monotonic_time_s = audio_mono;
                if (duration_s > 0.0) {
                    const double q = std::floor(audio_mono / duration_s);
                    media_clock_.loop_epoch = static_cast<uint32_t>(std::max(0.0, q));
                    local_time_s = std::fmod(audio_mono, duration_s);
                    if (local_time_s < 0.0) local_time_s += duration_s;
                } else {
                    media_clock_.loop_epoch = 0;
                    local_time_s = audio_mono;
                }
            }
        }

        if (session_ && (session_->video_frame_counter % 120) == 0) {
            const uint64_t rf = session_->audio_frames_read.load(std::memory_order_relaxed);
            const double am = session_->audio_read_head_media_time.load(std::memory_order_relaxed);
            std::fprintf(stderr,
                "[movie_loaded] CLOCK  audio_master=%d  read_frames=%llu  audio_mono=%.4f  "
                "decoder_local=%.4f  local=%.4f  gen=%u\n",
                using_audio_master ? 1 : 0,
                static_cast<unsigned long long>(rf), am,
                decoder_local_time_s, local_time_s,
                media_clock_.source_generation);
        }

        if (!using_audio_master) {
            if (decoder_ && loop_enabled && duration_s > 0.0) {
                // Detect wrap by local-time regression beyond small jitter.
                if (decoder_local_time_s + 0.010 < last_local_time_s_ &&
                    (last_local_time_s_ - decoder_local_time_s) > (duration_s * 0.25)) {
                    media_clock_.loop_epoch += 1;
                }
            }
            local_time_s = decoder_local_time_s;
            monotonic_time_s =
                vivid::media_clock_monotonic(local_time_s, duration_s, media_clock_.loop_epoch);
        }
        last_local_time_s_ = local_time_s;

        media_clock_.local_time_s = local_time_s;
        media_clock_.duration_s = duration_s;
        media_clock_.speed = speed.value;
        media_clock_.playing = playing ? 1u : 0u;
        media_clock_.loop_enabled = loop_enabled ? 1u : 0u;
        media_clock_.monotonic_time_s = monotonic_time_s;
        if (session_) {
            session_->source_generation.store(media_clock_.source_generation, std::memory_order_release);
            session_->loop_epoch.store(media_clock_.loop_epoch, std::memory_order_release);
            session_->monotonic_time_s.store(media_clock_.monotonic_time_s, std::memory_order_release);
            session_->local_time_s.store(media_clock_.local_time_s, std::memory_order_release);
            session_->duration_s.store(media_clock_.duration_s, std::memory_order_release);
            session_->speed.store(media_clock_.speed, std::memory_order_release);
            session_->playing.store(media_clock_.playing, std::memory_order_release);
            session_->loop_enabled.store(media_clock_.loop_enabled, std::memory_order_release);
            std::lock_guard<std::mutex> vlock(session_->video_queue_mu);
            vivid::media::VideoFrameEvent ev;
            ev.generation = media_clock_.source_generation;
            ev.frame_index = ++session_->video_frame_counter;
            ev.monotonic_time_s = media_clock_.monotonic_time_s;
            ev.width = texture_.width;
            ev.height = texture_.height;
            session_->video_queue.push_back(ev);
            while (session_->video_queue.size() > 16) session_->video_queue.pop_front();
        }

        if (ctx) {
            media_stream_.handle_id = handle_id_;
            media_stream_.session_ptr = reinterpret_cast<uint64_t>(session_.get());
            media_stream_.source_generation = media_clock_.source_generation;
            media_stream_.schema_version = 1;
            media_stream_.flags = 0;
            media_stream_.clock = media_clock_;
            if (ctx->output_data && ctx->output_data_count > 0)
                ctx->output_data[0] = &media_stream_;
        }
    }

    void start_loader_thread() {
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
        });
    }

    void stop_loader_thread() {
        {
            std::lock_guard<std::mutex> lock(loader_mu_);
            loader_stop_ = true;
            load_coordinator_.cancel_all();
            loader_cv_.notify_one();
        }
        if (loader_thread_.joinable()) {
            loader_thread_.join();
        }
    }

    void clear_output(VividGpuState* gpu) {
        if (!gpu->output_texture_view) return;
        WGPURenderPassColorAttachment color_att{};
        color_att.view = gpu->output_texture_view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = {0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv("MovieLoaded Clear");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(gpu->command_encoder, &rp_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    bool lazy_init(VividGpuState* gpu) {
        shader_ = vivid::gpu::create_shader(gpu->device, kBlitFragment, "MovieLoaded Shader");
        shader_ycocg_ = vivid::gpu::create_shader(gpu->device, kBlitFragmentYCoCg, "MovieLoaded YCoCg Shader");
        if (!shader_ || !shader_ycocg_) return false;

        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "MovieLoaded Sampler");

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
        bgl_desc.label = vivid_sv("MovieLoaded BGL");
        bgl_desc.entryCount = 2;
        bgl_desc.entries = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("MovieLoaded Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                gpu->output_format, "MovieLoaded Pipeline");
        pipeline_ycocg_ = vivid::gpu::create_pipeline(gpu->device, shader_ycocg_, pipe_layout_,
                                                       gpu->output_format, "MovieLoaded YCoCg Pipeline");
        return pipeline_ != nullptr && pipeline_ycocg_ != nullptr;
    }
};

VIVID_REGISTER(MovieLoaded)
