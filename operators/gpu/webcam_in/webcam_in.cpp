#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "capture_source.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// =============================================================================
// Blit WGSL — identical to MovieLoaded's (samples staging texture → output)
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
// Platform-specific capture factory
// =============================================================================

#ifdef __APPLE__
std::unique_ptr<CaptureSource> create_avf_capture();
#endif

// =============================================================================
// Resolution presets
// =============================================================================

static void resolution_for_preset(int preset, int& w, int& h) {
    switch (preset) {
        case 0: w = 640;  h = 480;  break;  // 480p
        case 1: w = 1280; h = 720;  break;  // 720p
        default:
        case 2: w = 1920; h = 1080; break;  // 1080p
    }
}

static float fps_for_preset(int preset) {
    switch (preset) {
        case 0:  return 15.0f;
        case 1:  return 24.0f;
        default:
        case 2:  return 30.0f;
        case 3:  return 60.0f;
    }
}
/**
 * @brief Live camera capture with device selection and resolution presets.
 *
 * Captures video from a connected camera device. Dynamically discovers
 * available cameras on startup. Supports 480p, 720p, and 1080p presets.
 *
 * @see MovieFileIn, SyphonIn
 */
struct WebcamIn : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "WebcamIn";
    static constexpr bool kTimeDependent = true;

    vivid::Param<bool> active    {"active", true};
    vivid::Param<int> device     {"device", 0, 0, 0};
    vivid::Param<int> resolution {"resolution", 1, {"480p", "720p", "1080p"}};
    vivid::Param<int> fps        {"fps", 2, {"15", "24", "30", "60"}};

    // Dynamic device name storage
    std::vector<std::string>  device_names_;
    std::vector<const char*>  device_name_ptrs_;

    WebcamIn() {
        vivid::semantic_tag(active, "enabled");
        vivid::semantic_shape(active, "bool");
        vivid::description(active, "Enable or disable camera capture");

        vivid::semantic_tag(device, "index");
        vivid::semantic_shape(device, "int");
        vivid::description(device, "Which camera to capture from");

        vivid::semantic_tag(resolution, "resolution_px");
        vivid::semantic_shape(resolution, "enum");
        vivid::description(resolution, "Capture resolution preset: 480p, 720p, or 1080p");

        vivid::semantic_tag(fps, "frequency_hz");
        vivid::semantic_shape(fps, "enum");
        vivid::description(fps, "Target capture frame rate");

        auto cameras = enumerate_cameras();
        if (cameras.empty()) {
            device_names_.push_back("No camera");
        } else {
            int default_idx = 0;
            for (size_t i = 0; i < cameras.size(); ++i) {
                device_names_.push_back(cameras[i].name);
                if (cameras[i].is_default) default_idx = static_cast<int>(i);
            }
            device.default_value = static_cast<float>(default_idx);
            device.value = device.default_value;
        }
        device_name_ptrs_.reserve(device_names_.size());
        for (auto& n : device_names_)
            device_name_ptrs_.push_back(n.c_str());
        device.choice_labels = device_name_ptrs_.data();
        device.choice_count  = static_cast<uint32_t>(device_name_ptrs_.size());
        device.max_value     = static_cast<float>(device_name_ptrs_.size() - 1);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        param_group(active,     "Capture");
        param_group(device,     "Capture");
        param_group(resolution, "Capture");
        param_group(fps,        "Capture");

        layout_row(resolution, 2, 0);
        layout_row(fps,        2, 1);

        out.push_back(&active);
        out.push_back(&device);
        out.push_back(&resolution);
        out.push_back(&fps);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        // One-time GPU pipeline setup
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[webcam_in] lazy_init FAILED\n");
                return;
            }
        }

        bool is_active = active.bool_value();

        // Handle active toggle
        if (!is_active && was_active_) {
            // Just toggled off — pause capture
            if (capture_ && capture_->is_open())
                capture_->stop();
            was_active_ = false;
        } else if (is_active && !was_active_) {
            // Just toggled on — check if settings changed while inactive
            int cur_device = device.int_value();
            int cur_res    = resolution.int_value();
            int cur_fps    = fps.int_value();
            if (capture_ && (cur_device != last_device_ ||
                             cur_res != last_resolution_ ||
                             cur_fps != last_fps_)) {
                capture_->close();
                capture_.reset();
                last_device_     = cur_device;
                last_resolution_ = cur_res;
                last_fps_        = cur_fps;
            } else if (capture_ && capture_->is_open()) {
                capture_->start();
            }
            was_active_ = true;
        }

        if (is_active) {
            // Reopen capture if device, resolution, or fps changed
            int cur_device = device.int_value();
            int cur_res    = resolution.int_value();
            int cur_fps    = fps.int_value();
            if (cur_device != last_device_ || cur_res != last_resolution_ ||
                cur_fps != last_fps_) {
                last_device_     = cur_device;
                last_resolution_ = cur_res;
                last_fps_        = cur_fps;
                if (capture_) capture_->close();
                capture_.reset();
            }

            // Open capture if needed
            if (!capture_ || !capture_->is_open()) {
                open_capture();
            }

            // Pull a new frame from the capture source
            if (capture_ && capture_->is_open() && capture_->update()) {
                const uint8_t* pixels = capture_->pixel_data();
                uint32_t w = capture_->width();
                uint32_t h = capture_->height();
                if (pixels && w > 0 && h > 0) {
                    if (w != staging_width_ || h != staging_height_) {
                        recreate_staging(ctx, w, h);
                    }
                    upload_pixels(ctx, pixels, w, h);
                    has_frame_ = true;
                }
            }
        }

        // Tell runtime our preferred output size
        if (staging_width_ > 0 && staging_height_ > 0) {
            vivid_request_output_size(ctx, staging_width_, staging_height_);
        }

        // Blit staging → output (or clear to black)
        if (has_frame_ && staging_view_ && bind_group_) {
            blit(ctx);
        } else {
            clear_output(ctx);
        }
    }

    ~WebcamIn() override {
        capture_.reset();
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

    // Capture state
    std::unique_ptr<CaptureSource> capture_;
    int  last_device_     = -1;
    int  last_resolution_ = -1;
    int  last_fps_        = -1;
    bool has_frame_       = false;
    bool was_active_      = true;

    void open_capture() {
#ifdef __APPLE__
        capture_ = create_avf_capture();
#endif
        if (!capture_) return;

        int w, h;
        resolution_for_preset(last_resolution_, w, h);
        float target_fps = fps_for_preset(last_fps_);
        if (!capture_->open(last_device_, w, h, target_fps)) {
            std::fprintf(stderr, "[webcam_in] Failed to open camera %d\n", last_device_);
            capture_.reset();
        }
    }

    // --- GPU helpers (same pattern as MovieLoaded) ---

    void recreate_staging(const VividGpuContext* gpu, uint32_t w, uint32_t h) {
        vivid::gpu::release(staging_tex_);
        vivid::gpu::release(staging_view_);
        vivid::gpu::release(bind_group_);

        staging_width_  = w;
        staging_height_ = h;

        static constexpr WGPUTextureFormat kFmt = WGPUTextureFormat_BGRA8Unorm;

        WGPUTextureDescriptor td{};
        td.label = vivid_sv("WebcamIn Staging");
        td.size = { w, h, 1 };
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = kFmt;
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        staging_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format = kFmt;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        staging_view_ = wgpuTextureCreateView(staging_tex_, &vd);

        WGPUBindGroupEntry entries[2]{};
        entries[0].binding = 0;
        entries[0].sampler = sampler_;
        entries[1].binding = 1;
        entries[1].textureView = staging_view_;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("WebcamIn BG");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 2;
        bg_desc.entries = entries;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);
    }

    void upload_pixels(const VividGpuContext* gpu, const uint8_t* pixels, uint32_t w, uint32_t h) {
        if (!staging_tex_) return;

        uint32_t src_row_bytes = w * 4;
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
            wgpuQueueWriteTexture(gpu->queue, &dest, pixels,
                                  static_cast<size_t>(src_row_bytes) * h, &layout, &extent);
        } else {
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

    void blit(const VividGpuContext* gpu) {
        vivid::gpu::run_pass(gpu->command_encoder, pipeline_, bind_group_,
                             gpu->output_texture_view, "WebcamIn Blit");
    }

    void clear_output(const VividGpuContext* gpu) {
        if (!gpu->output_texture_view) return;
        WGPURenderPassColorAttachment color_att{};
        color_att.view = gpu->output_texture_view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp  = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = { 0.0, 0.0, 0.0, 1.0 };

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv("WebcamIn Clear");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            gpu->command_encoder, &rp_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    bool lazy_init(const VividGpuContext* gpu) {
        shader_ = vivid::gpu::create_shader(gpu->device, kBlitFragment, "WebcamIn Shader");
        if (!shader_) return false;

        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "WebcamIn Sampler");

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
        bgl_desc.label = vivid_sv("WebcamIn BGL");
        bgl_desc.entryCount = 2;
        bgl_desc.entries = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("WebcamIn Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                 gpu->output_format, "WebcamIn Pipeline");
        if (!pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(WebcamIn)
