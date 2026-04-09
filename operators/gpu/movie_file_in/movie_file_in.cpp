#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "../../shared/movie_decode/video_decoder.h"
#include "../../shared/movie_decode/decoder_factory.h"
#include "../../shared/movie_decode/texture_upload.h"
#include "../../shared/movie_decode/placeholder_frame.h"
#include "../../shared/movie_decode/load_generation.h"
#include "../../shared/movie_decode/movie_playback_stats.h"
#include "movie_transport.h"
#include "session_registry.h"
#include "decoded_frame_queue.h"
#include "video_decode_worker.h"
#include "../../shared/movie_decode/avf_decoder.h"
#include "../../shared/movie_decode/hap_decoder.h"

#include <chrono>
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
/**
 * @brief Video file playback with frame-accurate timing and audio sync.
 *
 * Plays MP4, MOV, MKV, WebM and other video formats. Supports GPU-compressed
 * textures (BC1/BC3/BC4) and YCoCg color space decoding. Audio sync via
 * optional audio_time input from MovieFileAudio.
 *
 * @see TextureLoader, WebcamIn, MovieFileAudio
 */
struct MovieFileIn : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "MovieFileIn";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::FilePath> file {"file"};
    vivid::Param<int>   play_mode {"play_mode", 0, {"Loop", "Once", "Hold Last"}};
    vivid::Param<float> speed     {"speed", 1.0f, 0.0f, 4.0f};
    vivid::Param<float> video_phase_offset_ms {"video_phase_offset_ms", 0.0f, -250.0f, 250.0f};

    MovieFileIn() {
        vivid::semantic_tag(file, "path_video");
        vivid::semantic_shape(file, "path");
        vivid::description(file, "Path to a video or image file to play");

        vivid::semantic_tag(play_mode, "x_play_mode");
        vivid::semantic_shape(play_mode, "enum");
        vivid::description(play_mode, "What happens at the end: Loop, play Once, or Hold Last frame");

        vivid::semantic_tag(speed, "x_playback_speed");
        vivid::semantic_shape(speed, "scalar");
        vivid::description(speed, "Playback rate multiplier, 1 = normal speed");

        vivid::semantic_tag(video_phase_offset_ms, "time_milliseconds");
        vivid::semantic_shape(video_phase_offset_ms, "scalar");
        vivid::semantic_unit(video_phase_offset_ms, "ms");
        vivid::description(video_phase_offset_ms, "Timing offset for audio sync, in milliseconds");

        start_loader_thread();
    }

    ~MovieFileIn() override {
        stop_loader_thread();
        if (decode_worker_) {
            decode_worker_->stop();
            decode_worker_.reset();
        }
        if (session_) {
            PlaybackSessionRegistry::instance().release(session_->source_path());
            session_.reset();
        }
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
        // audio_time: SIGNAL input from MovieFileAudio/time via cadence bridge
        out.push_back({"audio_time", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
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
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[movie_file_in] lazy_init FAILED\n");
                return;
            }
        }

        // Read file path from param
        std::string effective_path = file.str_value;

        if (effective_path != last_path_) {
            last_path_ = effective_path;
            on_source_changed();
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

        // Track the authoritative local time for output publishing
        double published_local_time = decoder_ ? static_cast<double>(decoder_->current_time()) : 0.0;

        if (decoder_ && decoder_->is_open() && !placeholder_active_) {
            decoder_->set_loop(play_mode.int_value() == 0);
            decoder_->set_speed(speed.value);

            // ==========================================================
            // Clock ownership: AUDIO-MASTER vs SELF-CLOCK
            // ==========================================================
            // Two modes, selected by whether audio_time input is connected:
            //
            // SELF-CLOCK (audio_time disconnected):
            //   AVPlayer's internal clock is authoritative.  We read its
            //   position via decoder_->current_time() and publish it.
            //
            // AUDIO-MASTER (audio_time connected and > 0):
            //   Audio pipeline's monotonic time is authoritative.  The
            //   shared MovieTransport evaluates drift and seek policy.
            //
            // Seek policy is centralized in MovieTransport (shared dylib):
            //   threshold = 2 * frame_duration, cooldown = 150ms, budget = 4/sec
            // ==========================================================

            const float audio_time = ctx->input_values ? ctx->input_values[0] : 0.0f;
            const bool audio_master = ctx->input_connected && ctx->input_connected[0]
                                      && audio_time > 0.0f;

            if (session_) {
                session_->transport().set_play_mode(static_cast<PlayMode>(play_mode.int_value()));
                session_->transport().set_speed(speed.value);
            }

            if (audio_master && session_) {
                auto& transport = session_->transport();
                const double phase_s = static_cast<double>(video_phase_offset_ms.value) * 0.001;
                const double desired_local = transport.compute_audio_master_time(audio_time, phase_s);
                const double decoder_time = std::max(0.0, static_cast<double>(decoder_->current_time()));

                video_stats_.drift_ms.update(
                    static_cast<float>(std::abs(transport.drift_seconds(desired_local, decoder_time)) * 1000.0));

                const double desired_mono = static_cast<double>(audio_time) + phase_s;
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
                            std::fprintf(stderr, "[movie_file_in] seek to %.3fs failed\n",
                                         decision.seek_target);
                        }
                        break;
                }
                published_local_time = desired_local;
            } else {
                double video_local = std::max(0.0, static_cast<double>(decoder_->current_time()));
                if (session_) {
                    published_local_time = session_->transport().compute_self_clock_time(video_local);
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
                    if (decode_worker_) decode_worker_->submit_decoded(hap->make_decoded_frame());
                    video_stats_.new_frame_count++;
                } else if (status == DecodeStatus::ReusedFrame) {
                    video_stats_.reused_frame_count++;
                } else {
                    video_stats_.nil_frame_count++;
                }
            } else {
                // AVF: acquire on main thread (fast), copy on worker thread
                auto* avf = static_cast<AVFDecoder*>(decoder_.get());
                auto acquired = avf->acquire_pixel_buffer();
                if (acquired.valid()) {
                    if (decode_worker_) {
                        auto acquired_ptr = std::make_shared<AcquiredPixelBuffer>(std::move(acquired));
                        decode_worker_->submit_work([acquired_ptr]() mutable {
                            return AVFDecoder::copy_pixel_buffer(std::move(*acquired_ptr));
                        });
                    }
                    video_stats_.new_frame_count++;
                } else if (acquired.status == DecodeStatus::ReusedFrame) {
                    video_stats_.reused_frame_count++;
                } else {
                    video_stats_.nil_frame_count++;
                }
            }

            auto t_decode_end = std::chrono::steady_clock::now();
            video_stats_.decode_acquire_us.update(
                std::chrono::duration<float, std::micro>(t_decode_end - t_decode_start).count());

            // Unified consumption: pop latest frame from queue
            bool got_new_decoded = false;
            DecodedFrame ready;
            if (decode_worker_ && decode_worker_->pop_latest(ready)) {
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
                } else {
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
                                 ctx->output_texture_view, "MovieFileIn Blit");
        } else {
            clear_output(ctx);
        }

        // Publish time and telemetry outputs
        if (ctx->output_values) {
            ctx->output_values[1] = static_cast<float>(published_local_time);
            ctx->output_values[2] = decoder_ ? decoder_->duration() : 0.0f;
            ctx->output_values[3] = static_cast<float>(video_stats_.new_frame_count);
            ctx->output_values[4] = static_cast<float>(video_stats_.reused_frame_count);
            ctx->output_values[5] = static_cast<float>(video_stats_.nil_frame_count);
            ctx->output_values[6] = video_stats_.decode_acquire_us.value.load(std::memory_order_relaxed);
            ctx->output_values[7] = video_stats_.decode_copy_us.value.load(std::memory_order_relaxed);
            ctx->output_values[8] = video_stats_.gpu_upload_us.value.load(std::memory_order_relaxed);
            ctx->output_values[9] = video_stats_.drift_ms.value.load(std::memory_order_relaxed);
            ctx->output_values[10] = static_cast<float>(video_stats_.seek_correction_count);
            ctx->output_values[11] = static_cast<float>(video_stats_.seek_budget_exhausted_count);
            ctx->output_values[12] = static_cast<float>(video_stats_.drop_repeat_correction_count);
        }
    }

private:

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
                session_->transport().set_source(static_cast<double>(decoder_->duration()));
                session_->transport().set_frame_rate(decoder_->frame_rate());
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

    void on_source_changed() {
        if (decode_worker_) decode_worker_->flush();
        last_decoded_frame_.clear();
        if (session_) {
            PlaybackSessionRegistry::instance().release(session_->source_path());
            session_.reset();
        }
        if (!last_path_.empty()) {
            session_ = PlaybackSessionRegistry::instance().acquire(last_path_);
        }
        video_stats_.reset();
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
            std::fprintf(stderr, "[movie_file_in] Failed to load image: %s\n", last_path_.c_str());
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
        rp_desc.label = vivid_sv("MovieFileIn Clear");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(gpu->command_encoder, &rp_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    // ---- GPU pipeline init --------------------------------------------------

    bool lazy_init(const VividGpuContext* gpu) {
        shader_ = vivid::gpu::create_shader(gpu->device, kBlitFragment, "MovieFileIn Shader");
        shader_ycocg_ = vivid::gpu::create_shader(gpu->device, kBlitFragmentYCoCg, "MovieFileIn YCoCg Shader");
        if (!shader_ || !shader_ycocg_) return false;

        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "MovieFileIn Sampler");

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
        bgl_desc.label = vivid_sv("MovieFileIn BGL");
        bgl_desc.entryCount = 2;
        bgl_desc.entries = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("MovieFileIn Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                gpu->output_format, "MovieFileIn Pipeline");
        pipeline_ycocg_ = vivid::gpu::create_pipeline(gpu->device, shader_ycocg_, pipe_layout_,
                                                       gpu->output_format, "MovieFileIn YCoCg Pipeline");
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
    std::shared_ptr<PlaybackSession> session_;
    std::unique_ptr<VideoDecodeWorker> decode_worker_;
    DecodedFrame last_decoded_frame_;

    std::mutex loader_mu_;
    std::condition_variable loader_cv_;
    bool loader_stop_ = false;
    bool loader_has_request_ = false;
    LoadRequest loader_request_{};
    std::optional<LoadResult> loader_ready_result_;
    MovieLoadCoordinator load_coordinator_{};
    std::thread loader_thread_;
    std::atomic<bool> loader_finished_{false};
};

VIVID_REGISTER(MovieFileIn)
