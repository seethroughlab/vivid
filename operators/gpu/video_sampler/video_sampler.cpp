#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "../../shared/movie_decode/texture_upload.h"
#include "../../shared/movie_decode/decoder_factory.h"
#include "../../shared/movie_decode/video_decoder.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Blit shader — samples the active clip's frame texture into the output.
// (Same fullscreen-triangle blit the TextureLoader/MovieFile use.)
// ---------------------------------------------------------------------------
static const char* kVideoSamplerBlit = R"(
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

namespace fs = std::filesystem;

static bool is_video_ext(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".mp4" || ext == ".mov" || ext == ".m4v" || ext == ".mkv" ||
           ext == ".webm" || ext == ".avi";
}

/**
 * @brief A bank of video clips, switched instantly by index — a "video sampler".
 *
 * Loads every video file in a directory into its own decoder, keeps them warm
 * (inactive clips paused), and outputs the frame of the clip selected by
 * `index`. Switching is instant — no per-switch file reload — so a session
 * clip, sequencer, or LFO can cut between many sources in time with the music.
 * The visual analog of the audio Sampler. Video only (no audio output).
 *
 * @tip Point `directory` at a folder of short clips; drive `index` from the
 *   session (one clip per scene) or a control signal for montage.
 * @pitfall Each clip holds an open decoder; keep the bank to a few dozen short
 *   clips. Large/long videos belong in MovieFile instead.
 * @param directory Folder of video files to bank (sorted by name → index order).
 * @param index Which clip to show (0-based, clamped to the bank size).
 * @param play_mode Loop or play Once when a clip reaches its end.
 * @param speed Playback rate of the active clip (1 = normal; 0 = freeze).
 * @output texture The active clip's current frame.
 * @see MovieFile, Sampler, TextureLoader
 */
struct VideoSampler : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "VideoSampler";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::FilePath> directory {"directory"};
    vivid::Param<int>   index     {"index",     0, 0, 63};
    vivid::Param<int>   play_mode {"play_mode", 0, {"Loop", "Once"}};
    vivid::Param<float> speed     {"speed",     1.0f, 0.0f, 4.0f};

    VideoSampler() {
        vivid::semantic_shape(directory, "path");
        vivid::description(directory, "Folder of video clips to bank (sorted by name)");
        vivid::semantic_shape(index, "scalar");
        vivid::semantic_intent(index, "clip_select");
        vivid::description(index, "Which banked clip to show (0-based, clamped to bank size)");
        vivid::description(play_mode, "Loop or play Once at a clip's end");
        vivid::semantic_tag(speed, "x_playback_speed");
        vivid::semantic_shape(speed, "scalar");
        vivid::description(speed, "Playback rate of the active clip (1 = normal, 0 = freeze)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&directory);
        out.push_back(&index);
        out.push_back(&play_mode);
        out.push_back(&speed);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Drives `index` when connected (added on top of the param value).
        out.push_back({"index_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"texture",  VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_ && !lazy_init(ctx)) {
            std::fprintf(stderr, "[video_sampler] lazy_init FAILED\n");
            return;
        }

        if (directory.str_value != loaded_dir_) {
            loaded_dir_ = directory.str_value;
            load_bank();
        }

        if (decoders_.empty()) {
            show_placeholder(ctx);
        } else {
            // Resolve active clip: index param + optional index_cv input.
            int sel = index.int_value();
            // index_cv is the only scalar input (port 0); 0 when disconnected.
            if (ctx->input_values)
                sel += static_cast<int>(ctx->input_values[0] + 0.5f);
            const int n = static_cast<int>(decoders_.size());
            sel = ((sel % n) + n) % n;  // wrap into range

            if (sel != active_) {
                if (active_ >= 0 && active_ < n) decoders_[active_]->set_speed(0.0f);
                active_ = sel;
                auto& d = decoders_[active_];
                d->set_loop(play_mode.int_value() == 0);
                d->seek(0.0);
                d->set_speed(std::max(0.01f, speed.value));
            }

            auto& dec = decoders_[active_];
            dec->set_speed(speed.value);
            dec->set_loop(play_mode.int_value() == 0);

            DecodeStatus st = dec->decode_frame();
            if (st == DecodeStatus::NewFrame && dec->pixel_data()) {
                const uint32_t w = dec->width(), h = dec->height();
                if (w && h) {
                    if (texture_.width != w || texture_.height != h || !texture_.view)
                        movie_texture_recreate(ctx->device, sampler_, bind_layout_, texture_,
                                               w, h, WGPUTextureFormat_BGRA8Unorm, false);
                    movie_upload_bgra(ctx->queue, texture_, dec->pixel_data(), w, h);
                    movie_texture_rebuild_bind_group(ctx->device, sampler_, bind_layout_, texture_);
                }
            }
        }

        if (texture_.view && texture_.bind_group) {
            if (texture_.width && texture_.height)
                vivid_request_output_size(ctx, texture_.width, texture_.height);
            vivid::gpu::run_pass(ctx->command_encoder, pipeline_, texture_.bind_group,
                                 ctx->output_texture_view, "VideoSampler Blit");
        }
    }

    ~VideoSampler() override {
        for (auto& d : decoders_) if (d) d->close();
        decoders_.clear();
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(sampler_);
        movie_texture_release(texture_);
    }

private:
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUSampler         sampler_     = nullptr;
    MovieTextureState   texture_{};

    std::vector<std::unique_ptr<VideoDecoder>> decoders_;
    std::string loaded_dir_;
    int active_ = -1;

    void load_bank() {
        for (auto& d : decoders_) if (d) d->close();
        decoders_.clear();
        active_ = -1;
        if (loaded_dir_.empty() || !fs::is_directory(loaded_dir_)) return;

        std::vector<std::string> paths;
        std::error_code ec;
        for (auto& e : fs::directory_iterator(loaded_dir_, ec)) {
            if (e.is_regular_file(ec) && is_video_ext(e.path().string()))
                paths.push_back(e.path().string());
        }
        std::sort(paths.begin(), paths.end());

        for (const auto& p : paths) {
            auto res = load_video_decoder_for_path(p, /*bc_supported=*/false, nullptr);
            if (res.success && res.decoder) {
                res.decoder->set_loop(true);
                res.decoder->set_speed(0.0f);  // paused until selected
                decoders_.push_back(std::move(res.decoder));
            } else {
                std::fprintf(stderr, "[video_sampler] skip (decode failed): %s\n", p.c_str());
            }
        }
        std::fprintf(stderr, "[video_sampler] banked %zu clip(s) from %s\n",
                     decoders_.size(), loaded_dir_.c_str());
    }

    void show_placeholder(const VividGpuContext* ctx) {
        static const uint8_t kGrey[4] = {32, 32, 32, 255};  // BGRA
        if (!texture_.view || texture_.width != 1)
            movie_texture_recreate(ctx->device, sampler_, bind_layout_, texture_,
                                   1, 1, WGPUTextureFormat_BGRA8Unorm, false);
        movie_upload_bgra(ctx->queue, texture_, kGrey, 1, 1);
        movie_texture_rebuild_bind_group(ctx->device, sampler_, bind_layout_, texture_);
    }

    bool lazy_init(const VividGpuContext* ctx) {
        shader_ = vivid::gpu::create_shader(ctx->device, kVideoSamplerBlit, "VideoSampler Shader");
        if (!shader_) return false;
        sampler_ = vivid::gpu::create_linear_sampler(ctx->device, "VideoSampler Sampler");

        WGPUBindGroupLayoutEntry entries[2]{};
        entries[0].binding        = 0;
        entries[0].visibility     = WGPUShaderStage_Fragment;
        entries[0].sampler.type   = WGPUSamplerBindingType_Filtering;
        entries[1].binding                = 1;
        entries[1].visibility             = WGPUShaderStage_Fragment;
        entries[1].texture.sampleType     = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension  = WGPUTextureViewDimension_2D;
        entries[1].texture.multisampled   = false;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label      = vivid_sv("VideoSampler BGL");
        bgl_desc.entryCount = 2;
        bgl_desc.entries    = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label                = vivid_sv("VideoSampler Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts     = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(ctx->device, shader_, pipe_layout_,
                                                ctx->output_format, "VideoSampler Pipeline");
        return pipeline_ != nullptr;
    }
};

VIVID_DEFINE_OP(VideoSampler) {
    display_name = "Video Sampler";
    keywords     = {"video", "clip", "bank", "montage", "footage", "switch"};
    summary      = "A bank of video clips switched instantly by index (visual analog of Sampler)";
}
