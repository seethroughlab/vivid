#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "video_decoder.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <thread>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// =============================================================================
// Blit WGSL Fragment Shader — samples staging texture, outputs to render target
// =============================================================================

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

// =============================================================================
// Forward declarations for platform-specific video decoder factory
// =============================================================================

#ifdef __APPLE__
std::unique_ptr<VideoDecoder> create_avf_decoder();
#endif

// =============================================================================
// Helper: check if file extension matches video types
// =============================================================================

static bool is_video_extension(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    // Convert to lowercase
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".mp4" || ext == ".mov" || ext == ".m4v" ||
           ext == ".avi" || ext == ".mkv" || ext == ".webm";
}

// =============================================================================
// Embedded 5×7 bitmap font (A-Z), each glyph stored as 7 rows, MSB = leftmost
// =============================================================================

static const uint8_t kFont5x7[26][7] = {
    {0x70,0x88,0x88,0xF8,0x88,0x88,0x88}, // A
    {0xF0,0x88,0x88,0xF0,0x88,0x88,0xF0}, // B
    {0x70,0x88,0x80,0x80,0x80,0x88,0x70}, // C
    {0xF0,0x88,0x88,0x88,0x88,0x88,0xF0}, // D
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0xF8}, // E
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0x80}, // F
    {0x70,0x88,0x80,0xB8,0x88,0x88,0x70}, // G
    {0x88,0x88,0x88,0xF8,0x88,0x88,0x88}, // H
    {0x70,0x20,0x20,0x20,0x20,0x20,0x70}, // I
    {0x38,0x10,0x10,0x10,0x10,0x90,0x60}, // J
    {0x88,0x90,0xA0,0xC0,0xA0,0x90,0x88}, // K
    {0x80,0x80,0x80,0x80,0x80,0x80,0xF8}, // L
    {0x88,0xD8,0xA8,0xA8,0x88,0x88,0x88}, // M
    {0x88,0xC8,0xC8,0xA8,0x98,0x98,0x88}, // N
    {0x70,0x88,0x88,0x88,0x88,0x88,0x70}, // O
    {0xF0,0x88,0x88,0xF0,0x80,0x80,0x80}, // P
    {0x70,0x88,0x88,0x88,0xA8,0x90,0x68}, // Q
    {0xF0,0x88,0x88,0xF0,0xA0,0x90,0x88}, // R
    {0x70,0x88,0x80,0x70,0x08,0x88,0x70}, // S
    {0xF8,0x20,0x20,0x20,0x20,0x20,0x20}, // T
    {0x88,0x88,0x88,0x88,0x88,0x88,0x70}, // U
    {0x88,0x88,0x88,0x88,0x50,0x50,0x20}, // V
    {0x88,0x88,0x88,0xA8,0xA8,0xD8,0x88}, // W
    {0x88,0x88,0x50,0x20,0x50,0x88,0x88}, // X
    {0x88,0x88,0x50,0x20,0x20,0x20,0x20}, // Y
    {0xF8,0x08,0x10,0x20,0x40,0x80,0xF8}, // Z
};

// =============================================================================
// MovieFileIn Operator
// =============================================================================

struct MovieFileIn : vivid::OperatorBase {
    static constexpr const char* kName   = "MovieFileIn";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::FilePath> file {"file"};
    vivid::Param<int>   play_mode {"play_mode", 0, {"Loop", "Once", "Hold Last"}};
    vivid::Param<float> speed     {"speed", 1.0f, 0.0f, 4.0f};

    MovieFileIn() {
        vivid::semantic_tag(file, "path_video");
        vivid::semantic_shape(file, "path");

        vivid::semantic_tag(play_mode, "x_play_mode");
        vivid::semantic_shape(play_mode, "enum");

        vivid::semantic_tag(speed, "x_playback_speed");
        vivid::semantic_shape(speed, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
        out.push_back(&play_mode);
        out.push_back(&speed);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
        out.push_back({"time", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"speed", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[movie_file_in] lazy_init FAILED\n");
                return;
            }
        }

        // Check if file path changed
        if (file.str_value != last_path_) {
            last_path_ = file.str_value;
            pending_load_.reset();
            load_media(gpu);
        }

        // Check if async video load completed
        if (pending_load_ && pending_load_->done.load(std::memory_order_acquire)) {
            if (pending_load_->success) {
                decoder_ = std::move(pending_load_->decoder);
                decoder_->set_loop(play_mode.int_value() == 0);
                decoder_->set_speed(speed.value);
                std::fprintf(stderr, "[movie_file_in] Async video load complete: %s (%ux%u, %.1fs)\n",
                             last_path_.c_str(), decoder_->width(), decoder_->height(),
                             decoder_->duration());
            } else {
                std::fprintf(stderr, "[movie_file_in] Async video load failed: %s\n",
                             last_path_.c_str());
                show_placeholder(gpu);
            }
            pending_load_.reset();
        }

        // For video sources, decode the next frame
        if (decoder_ && decoder_->is_open() && !placeholder_active_) {
            // Update playback params
            decoder_->set_loop(play_mode.int_value() == 0);
            decoder_->set_speed(speed.value);

            if (decoder_->decode_frame()) {
                const uint8_t* pixels = decoder_->pixel_data();
                uint32_t w = decoder_->width();
                uint32_t h = decoder_->height();
                if (pixels && w > 0 && h > 0) {
                    // Recreate staging texture if dimensions changed
                    if (w != staging_width_ || h != staging_height_) {
                        recreate_staging(gpu, w, h);
                    }
                    upload_pixels(gpu, pixels, w, h);
                }
            }
        }

        // Request output texture match media dimensions
        if (staging_width_ > 0 && staging_height_ > 0) {
            auto* mutable_ctx = const_cast<VividProcessContext*>(ctx);
            mutable_ctx->preferred_tex_width  = staging_width_;
            mutable_ctx->preferred_tex_height = staging_height_;
        }

        // If we have a staging texture and valid bind group, blit it to output
        if (staging_view_ && bind_group_) {
            blit(gpu);
        } else {
            // Clear to black if nothing loaded
            clear_output(gpu);
        }

        // Write current playback time and speed to control output ports
        if (ctx->output_values) {
            ctx->output_values[1] = decoder_ ? decoder_->current_time() : 0.0f;
            ctx->output_values[2] = speed.value;
        }
    }

    ~MovieFileIn() override {
        decoder_.reset();
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(staging_tex_);
        vivid::gpu::release(staging_view_);
        vivid::gpu::release(bind_group_);
    }

private:
    // GPU resources
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUSampler         sampler_     = nullptr;
    WGPUTexture         staging_tex_ = nullptr;
    WGPUTextureView     staging_view_= nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    uint32_t            staging_width_  = 0;
    uint32_t            staging_height_ = 0;

    // Async video loading
    struct AsyncVideoLoad {
        std::atomic<bool> done{false};
        bool success = false;
        std::unique_ptr<VideoDecoder> decoder;
    };
    std::shared_ptr<AsyncVideoLoad> pending_load_;

    // Media state
    std::string last_path_;
    std::unique_ptr<VideoDecoder> decoder_;
    bool placeholder_active_ = false;
    WGPUDevice cached_device_ = nullptr;  // for staging texture recreation

    void load_media(VividGpuState* gpu) {
        // Close any existing decoder
        if (decoder_) {
            decoder_->close();
            decoder_.reset();
        }
        placeholder_active_ = false;

        if (last_path_.empty()) {
            show_placeholder(gpu);
            return;
        }

        if (is_video_extension(last_path_)) {
#ifdef __APPLE__
            // Launch async load — process() will pick up the result
            auto result = std::make_shared<AsyncVideoLoad>();
            pending_load_ = result;
            std::string path = last_path_;
            std::thread([result, path]{
                auto decoder = create_avf_decoder();
                if (decoder && decoder->open(path)) {
                    decoder->play();
                    result->decoder = std::move(decoder);
                    result->success = true;
                }
                result->done.store(true, std::memory_order_release);
            }).detach();
#else
            std::fprintf(stderr, "[movie_file_in] Video playback not supported on this platform\n");
            show_placeholder(gpu);
#endif
        } else {
            // Try loading as image
            load_image(gpu);
        }
    }

    void load_image(VividGpuState* gpu) {
        int w, h, channels;
        uint8_t* data = stbi_load(last_path_.c_str(), &w, &h, &channels, 4);  // force RGBA
        if (!data) {
            std::fprintf(stderr, "[movie_file_in] Failed to load image: %s\n",
                         last_path_.c_str());
            show_placeholder(gpu);
            return;
        }

        std::fprintf(stderr, "[movie_file_in] Loaded image: %s (%dx%d)\n",
                     last_path_.c_str(), w, h);

        // Convert RGBA -> BGRA (Dawn uses BGRA8Unorm)
        for (int i = 0; i < w * h; ++i) {
            std::swap(data[i * 4 + 0], data[i * 4 + 2]);
        }

        recreate_staging(gpu, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        upload_pixels(gpu, data, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        stbi_image_free(data);
    }

    void recreate_staging(VividGpuState* gpu, uint32_t w, uint32_t h) {
        vivid::gpu::release(staging_tex_);
        vivid::gpu::release(staging_view_);
        vivid::gpu::release(bind_group_);

        staging_width_  = w;
        staging_height_ = h;
        cached_device_  = gpu->device;

        // Use BGRA8Unorm for the staging texture — matches our CPU pixel data.
        // The blit shader samples as float regardless of source format.
        static constexpr WGPUTextureFormat kStagingFormat = WGPUTextureFormat_BGRA8Unorm;

        WGPUTextureDescriptor td{};
        td.label = vivid_sv("MovieFileIn Staging");
        td.size = { w, h, 1 };
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = kStagingFormat;
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        staging_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format = kStagingFormat;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        staging_view_ = wgpuTextureCreateView(staging_tex_, &vd);

        // Recreate bind group with new texture view
        WGPUBindGroupEntry entries[2]{};
        entries[0].binding = 0;
        entries[0].sampler = sampler_;
        entries[1].binding = 1;
        entries[1].textureView = staging_view_;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("MovieFileIn BG");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 2;
        bg_desc.entries = entries;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);
    }

    void upload_pixels(VividGpuState* gpu, const uint8_t* pixels, uint32_t w, uint32_t h) {
        if (!staging_tex_) return;

        uint32_t src_row_bytes = w * 4;
        // WebGPU requires bytesPerRow to be a multiple of 256
        uint32_t aligned_bpr = (src_row_bytes + 255) & ~255u;

        WGPUTexelCopyTextureInfo dest{};
        dest.texture = staging_tex_;
        dest.mipLevel = 0;
        dest.origin = {0, 0, 0};
        dest.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = aligned_bpr;
        layout.rowsPerImage = h;

        WGPUExtent3D extent = { w, h, 1 };

        if (aligned_bpr == src_row_bytes) {
            // No padding needed — upload directly
            wgpuQueueWriteTexture(gpu->queue, &dest, pixels,
                                  static_cast<size_t>(src_row_bytes) * h, &layout, &extent);
        } else {
            // Pad each row to meet alignment
            std::vector<uint8_t> padded(static_cast<size_t>(aligned_bpr) * h, 0);
            for (uint32_t row = 0; row < h; ++row) {
                std::memcpy(padded.data() + row * aligned_bpr,
                           pixels + row * src_row_bytes,
                           src_row_bytes);
            }
            wgpuQueueWriteTexture(gpu->queue, &dest, padded.data(),
                                  padded.size(), &layout, &extent);
        }
    }

    void show_placeholder(VividGpuState* gpu) {
        static constexpr uint32_t kW = 320;
        static constexpr uint32_t kH = 180;
        static constexpr int kScale  = 2;
        static constexpr int kGlyphW = 5;
        static constexpr int kGlyphH = 7;
        static constexpr int kCharW  = kGlyphW * kScale + 2; // 12px per char
        static constexpr int kCharH  = kGlyphH * kScale;     // 14px tall

        std::vector<uint8_t> pixels(kW * kH * 4);

        // Background: dark charcoal with subtle diagonal stripes + 1px border
        for (uint32_t y = 0; y < kH; ++y) {
            for (uint32_t x = 0; x < kW; ++x) {
                uint8_t v = ((x + y) % 8 < 2) ? 0x22 : 0x1A;
                if (x == 0 || y == 0 || x == kW - 1 || y == kH - 1) v = 0x33;
                auto* p = &pixels[(y * kW + x) * 4];
                p[0] = v; p[1] = v; p[2] = v; p[3] = 0xFF; // BGRA gray
            }
        }

        // Render "MEDIA MISSING" centered
        static const char kText[] = "MEDIA MISSING";
        static constexpr int kLen = sizeof(kText) - 1; // 13
        int text_w = kLen * kCharW - 2;                // 154px (no trailing gap)
        int ox = static_cast<int>(kW - text_w) / 2;
        int oy = static_cast<int>(kH - kCharH) / 2;

        for (int c = 0; c < kLen; ++c) {
            char ch = kText[c];
            if (ch == ' ') continue;
            int gi = ch - 'A';
            if (gi < 0 || gi >= 26) continue;

            for (int gy = 0; gy < kGlyphH; ++gy) {
                uint8_t row = kFont5x7[gi][gy];
                for (int gx = 0; gx < kGlyphW; ++gx) {
                    if (!(row & (0x80 >> gx))) continue;
                    for (int sy = 0; sy < kScale; ++sy) {
                        for (int sx = 0; sx < kScale; ++sx) {
                            int px = ox + c * kCharW + gx * kScale + sx;
                            int py = oy + gy * kScale + sy;
                            if (px < 0 || px >= (int)kW) continue;
                            if (py < 0 || py >= (int)kH) continue;
                            auto* p = &pixels[(py * kW + px) * 4];
                            p[0] = 0x88; p[1] = 0x88; p[2] = 0x88; p[3] = 0xFF;
                        }
                    }
                }
            }
        }

        recreate_staging(gpu, kW, kH);
        upload_pixels(gpu, pixels.data(), kW, kH);
        placeholder_active_ = true;
    }

    void blit(VividGpuState* gpu) {
        vivid::gpu::run_pass(gpu->command_encoder, pipeline_, bind_group_,
                             gpu->output_texture_view, "MovieFileIn Blit");
    }

    void clear_output(VividGpuState* gpu) {
        if (!gpu->output_texture_view) return;
        WGPURenderPassColorAttachment color_att{};
        color_att.view = gpu->output_texture_view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp  = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = { 0.0, 0.0, 0.0, 1.0 };

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv("MovieFileIn Clear");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            gpu->command_encoder, &rp_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    bool lazy_init(VividGpuState* gpu) {
        shader_ = vivid::gpu::create_shader(gpu->device, kBlitFragment, "MovieFileIn Shader");
        if (!shader_) return false;

        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "MovieFileIn Sampler");

        // Bind group layout: sampler(0) + texture(1)
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

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("MovieFileIn Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_, gpu->output_format, "MovieFileIn Pipeline");
        if (!pipeline_) return false;

        cached_device_ = gpu->device;
        return true;
    }
};

VIVID_REGISTER(MovieFileIn)
