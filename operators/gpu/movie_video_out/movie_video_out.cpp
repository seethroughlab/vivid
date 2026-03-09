#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/media_stream.h"
#include "../../shared/movie_decode/texture_upload.h"
#include "../../shared/media_session/media_session.h"

#include <cstdio>

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

struct MovieVideoOut : vivid::OperatorBase {
    static constexpr const char* kName = "MovieVideoOut";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"media_stream", VIVID_PORT_DATA, VIVID_PORT_INPUT, "media_stream_v1"});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[movie_video_out] lazy_init failed\n");
                return;
            }
        }

        // Read MediaStreamV1 from data input port
        vivid::media::MediaSession* session = nullptr;
        uint64_t generation = 0;
        if (ctx->input_data && ctx->input_data_count > 0 && ctx->input_data[0]) {
            const auto* stream = static_cast<const vivid::MediaStreamV1*>(ctx->input_data[0]);
            if (stream->session_ptr) {
                session = reinterpret_cast<vivid::media::MediaSession*>(stream->session_ptr);
                generation = stream->source_generation;
            }
        }

        // On source generation change, release texture and reset state
        if (generation != last_seen_generation_) {
            movie_texture_release(texture_);
            use_ycocg_ = false;
            last_seen_generation_ = generation;
        }

        // Drain video_payload_queue, keep only latest frame (drop policy)
        if (session) {
            std::optional<vivid::media::VideoFramePayload> latest;
            while (auto frame = media_session_pop_video_frame(*session)) {
                latest = std::move(frame);
            }

            if (latest) {
                const auto& f = *latest;
                bool compressed = (f.compression_mode == vivid::media::VideoFrameCompressionMode::CompressedBC);
                auto fmt = static_cast<WGPUTextureFormat>(f.format);

                ensure_texture(gpu, f.width, f.height, fmt, compressed);

                if (compressed) {
                    movie_upload_compressed(gpu->queue, texture_,
                                            f.bytes.data(), f.bytes.size(),
                                            f.width, f.height, fmt);
                } else {
                    movie_upload_bgra(gpu->queue, texture_,
                                      f.bytes.data(), f.width, f.height);
                }

                use_ycocg_ = (f.ycocg_encoded != 0);
            }
        }

        // Set preferred texture dimensions
        if (texture_.width > 0 && texture_.height > 0) {
            auto* mutable_ctx = const_cast<VividProcessContext*>(ctx);
            mutable_ctx->preferred_tex_width = texture_.width;
            mutable_ctx->preferred_tex_height = texture_.height;
        }

        // Render: blit owned texture to output, or clear to black
        if (texture_.view && texture_.bind_group) {
            WGPURenderPipeline active = use_ycocg_ ? pipeline_ycocg_ : pipeline_;
            vivid::gpu::run_pass(gpu->command_encoder, active, texture_.bind_group,
                                 gpu->output_texture_view, "MovieVideoOut Blit");
        } else if (gpu->output_texture_view) {
            // Clear to black without drawing (no bind group available).
            // Guard: sinks may have a null output_texture_view.
            WGPURenderPassColorAttachment color_att{};
            color_att.view = gpu->output_texture_view;
            color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            color_att.loadOp = WGPULoadOp_Clear;
            color_att.storeOp = WGPUStoreOp_Store;
            color_att.clearValue = WGPUColor{0, 0, 0, 1};

            WGPURenderPassDescriptor rp_desc{};
            rp_desc.label = vivid_sv("MovieVideoOut Clear");
            rp_desc.colorAttachmentCount = 1;
            rp_desc.colorAttachments = &color_att;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(gpu->command_encoder, &rp_desc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
    }

    ~MovieVideoOut() override {
        movie_texture_release(texture_);
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(pipeline_ycocg_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(shader_ycocg_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sampler_);
    }

private:
    WGPURenderPipeline  pipeline_       = nullptr;
    WGPURenderPipeline  pipeline_ycocg_ = nullptr;
    WGPUBindGroupLayout bind_layout_    = nullptr;
    WGPUShaderModule    shader_         = nullptr;
    WGPUShaderModule    shader_ycocg_   = nullptr;
    WGPUPipelineLayout  pipe_layout_    = nullptr;
    WGPUSampler         sampler_        = nullptr;

    MovieTextureState   texture_{};
    uint64_t            last_seen_generation_ = 0;
    bool                use_ycocg_ = false;

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

    bool lazy_init(VividGpuState* gpu) {
        if (!gpu || !gpu->device) return false;

        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "MovieVideoOut Sampler");

        WGPUBindGroupLayoutEntry bgl[2]{};
        bgl[0].binding = 0;
        bgl[0].visibility = WGPUShaderStage_Fragment;
        bgl[0].sampler.type = WGPUSamplerBindingType_Filtering;

        bgl[1].binding = 1;
        bgl[1].visibility = WGPUShaderStage_Fragment;
        bgl[1].texture.sampleType = WGPUTextureSampleType_Float;
        bgl[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        bgl[1].texture.multisampled = false;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("MovieVideoOut BGL");
        bgl_desc.entryCount = 2;
        bgl_desc.entries = bgl;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("MovieVideoOut PL");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        shader_ = vivid::gpu::create_shader(gpu->device, kBlitFragment, "MovieVideoOut Shader");
        shader_ycocg_ = vivid::gpu::create_shader(gpu->device, kBlitFragmentYCoCg, "MovieVideoOut YCoCg Shader");
        if (!shader_ || !shader_ycocg_) return false;

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                  gpu->output_format, "MovieVideoOut Pipeline");
        pipeline_ycocg_ = vivid::gpu::create_pipeline(gpu->device, shader_ycocg_, pipe_layout_,
                                                        gpu->output_format, "MovieVideoOut YCoCg Pipeline");
        return pipeline_ != nullptr && pipeline_ycocg_ != nullptr;
    }
};

VIVID_REGISTER(MovieVideoOut)
