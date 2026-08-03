// Core visual package operator: Webcam — live camera capture into a GPU texture.
// The one native node: capture runs on a background AVFoundation dispatch queue
// (avf_capture.mm) behind a mutex + atomic; process_gpu polls the latest frame on
// the render thread, uploads it to a staging texture, and blits it. Ported from
// vivid-classic:operators/gpu/webcam_in, re-authored against the current loadable-
// operator ABI (VIVID_REGISTER + static metadata). Off-macOS it degrades to a
// black "No camera" source (no avf_capture.mm in the build).
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "capture_source.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// Defined in avf_capture.mm (global scope — must match, so it resolves at dlopen).
#ifdef __APPLE__
std::unique_ptr<CaptureSource> create_avf_capture();
#endif

namespace {
// Blit shader: sample the staging texture (Y-flipped) into the output.
const char* kWebcamWGSL = R"(
struct VertexOutput { @builtin(position) position: vec4f, @location(0) uv: vec2f }
@group(0) @binding(0) var texSampler: sampler;
@group(0) @binding(1) var tex: texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, false);   // upright: matches this app's other ops
    var out: VertexOutput; out.position = fs.position; out.uv = fs.uv; return out;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(tex, texSampler, input.uv);
}
)";

void resolution_for_preset(int preset, int& w, int& h) {
    switch (preset) {
        case 0: w = 640;  h = 480;  break;   // 480p
        case 1: w = 1280; h = 720;  break;   // 720p
        default:
        case 2: w = 1920; h = 1080; break;   // 1080p
    }
}
float fps_for_preset(int preset) {
    switch (preset) {
        case 0:  return 15.0f;
        case 1:  return 24.0f;
        default:
        case 2:  return 30.0f;
        case 3:  return 60.0f;
    }
}
}  // namespace

struct WebcamOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Webcam";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
    static constexpr const char* kDisplayName = "Webcam";
    static constexpr const char* kSummary = "Live camera capture (device / resolution / fps) into the chain.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "webcam", "camera"};

    vivid::Param<bool> active    {"active", true};
    vivid::Param<int>  device    {"device", 0, 0, 0};
    vivid::Param<int>  resolution{"resolution", 1, {"480p", "720p", "1080p"}};
    vivid::Param<int>  fps        {"fps", 2, {"15", "24", "30", "60"}};

    std::vector<std::string> device_names_;
    std::vector<const char*> device_name_ptrs_;

    WebcamOp() {
        vivid::description(active, "Enable or disable camera capture");
        vivid::description(device, "Which camera to capture from");
        vivid::description(resolution, "Capture resolution preset: 480p, 720p, or 1080p");
        vivid::description(fps, "Target capture frame rate");

#ifdef __APPLE__
        auto cameras = enumerate_cameras();
#else
        std::vector<CameraInfo> cameras;
#endif
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
        for (auto& n : device_names_) device_name_ptrs_.push_back(n.c_str());
        device.choice_labels = device_name_ptrs_.data();
        device.choice_count  = static_cast<uint32_t>(device_name_ptrs_.size());
        device.max_value     = static_cast<float>(device_name_ptrs_.size() - 1);
    }

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&active); o.push_back(&device); o.push_back(&resolution); o.push_back(&fps);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        VividPortDescriptor p{}; p.name = "texture"; p.type = VIVID_PORT_TEXTURE; p.direction = VIVID_PORT_OUTPUT;
        p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
        o.push_back(p);
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) { if (!lazy_init(ctx)) { std::fprintf(stderr, "[Webcam] lazy_init FAILED\n"); return; } }

        const bool is_active = active.bool_value();
        if (!is_active && was_active_) {
            if (capture_ && capture_->is_open()) capture_->stop();
            was_active_ = false;
        } else if (is_active && !was_active_) {
            const int cd = device.int_value(), cr = resolution.int_value(), cf = fps.int_value();
            if (capture_ && (cd != last_device_ || cr != last_resolution_ || cf != last_fps_)) {
                capture_->close(); capture_.reset();
                last_device_ = cd; last_resolution_ = cr; last_fps_ = cf;
            } else if (capture_ && capture_->is_open()) {
                capture_->start();
            }
            was_active_ = true;
        }

        if (is_active) {
            const int cd = device.int_value(), cr = resolution.int_value(), cf = fps.int_value();
            if (cd != last_device_ || cr != last_resolution_ || cf != last_fps_) {
                last_device_ = cd; last_resolution_ = cr; last_fps_ = cf;
                if (capture_) capture_->close();
                capture_.reset();
            }
            if (!capture_ || !capture_->is_open()) open_capture();
            if (capture_ && capture_->is_open() && capture_->update()) {
                const uint8_t* px = capture_->pixel_data();
                const uint32_t w = capture_->width(), h = capture_->height();
                if (px && w > 0 && h > 0) {
                    if (w != staging_width_ || h != staging_height_) recreate_staging(ctx, w, h);
                    upload_pixels(ctx, px, w, h);
                    has_frame_ = true;
                }
            }
        }

        if (staging_width_ > 0 && staging_height_ > 0)
            vivid_request_output_size(ctx, staging_width_, staging_height_);

        if (has_frame_ && staging_view_ && bind_group_)
            vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_, ctx->output_texture_view, "Webcam Blit");
        else
            clear_output(ctx);
    }

    ~WebcamOp() override {
        capture_.reset();
        vivid::gpu::release(pipeline_); vivid::gpu::release(bind_layout_); vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(shader_); vivid::gpu::release(sampler_);
        vivid::gpu::release(staging_tex_); vivid::gpu::release(staging_view_); vivid::gpu::release(bind_group_);
    }

private:
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUSampler         sampler_     = nullptr;
    WGPUTexture         staging_tex_ = nullptr;
    WGPUTextureView     staging_view_= nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    uint32_t            staging_width_ = 0, staging_height_ = 0;

    std::unique_ptr<CaptureSource> capture_;
    int  last_device_ = -1, last_resolution_ = -1, last_fps_ = -1;
    bool has_frame_ = false, was_active_ = true;

    void open_capture() {
#ifdef __APPLE__
        capture_ = create_avf_capture();
#endif
        if (!capture_) return;
        int w, h; resolution_for_preset(last_resolution_, w, h);
        if (!capture_->open(last_device_, w, h, fps_for_preset(last_fps_))) {
            std::fprintf(stderr, "[Webcam] failed to open camera %d\n", last_device_);
            capture_.reset();
        }
    }

    void recreate_staging(const VividGpuContext* gpu, uint32_t w, uint32_t h) {
        vivid::gpu::release(staging_tex_); vivid::gpu::release(staging_view_); vivid::gpu::release(bind_group_);
        staging_width_ = w; staging_height_ = h;
        static constexpr WGPUTextureFormat kFmt = WGPUTextureFormat_BGRA8Unorm;
        WGPUTextureDescriptor td{};
        td.label = vivid_sv("Webcam Staging"); td.size = { w, h, 1 };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D; td.format = kFmt;
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        staging_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);
        WGPUTextureViewDescriptor vd{};
        vd.format = kFmt; vd.dimension = WGPUTextureViewDimension_2D; vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
        staging_view_ = wgpuTextureCreateView(staging_tex_, &vd);
        WGPUBindGroupEntry e[2]{};
        e[0].binding = 0; e[0].sampler = sampler_;
        e[1].binding = 1; e[1].textureView = staging_view_;
        WGPUBindGroupDescriptor bd{}; bd.label = vivid_sv("Webcam BG"); bd.layout = bind_layout_; bd.entryCount = 2; bd.entries = e;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bd);
    }

    void upload_pixels(const VividGpuContext* gpu, const uint8_t* pixels, uint32_t w, uint32_t h) {
        if (!staging_tex_) return;
        const uint32_t src_row = w * 4;
        const uint32_t aligned = (src_row + 255) & ~255u;   // wgpu requires 256-byte-aligned bytesPerRow
        WGPUTexelCopyTextureInfo dest{}; dest.texture = staging_tex_; dest.mipLevel = 0; dest.origin = {0,0,0}; dest.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay{}; lay.bytesPerRow = aligned; lay.rowsPerImage = h;
        WGPUExtent3D ext = { w, h, 1 };
        if (aligned == src_row) {
            wgpuQueueWriteTexture(gpu->queue, &dest, pixels, static_cast<size_t>(src_row) * h, &lay, &ext);
        } else {
            std::vector<uint8_t> padded(static_cast<size_t>(aligned) * h, 0);
            for (uint32_t row = 0; row < h; ++row)
                std::memcpy(padded.data() + row * aligned, pixels + row * src_row, src_row);
            wgpuQueueWriteTexture(gpu->queue, &dest, padded.data(), padded.size(), &lay, &ext);
        }
    }

    void clear_output(const VividGpuContext* gpu) {
        if (!gpu->output_texture_view) return;
        WGPURenderPassColorAttachment c{};
        c.view = gpu->output_texture_view; c.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED; c.resolveTarget = nullptr;
        c.loadOp = WGPULoadOp_Clear; c.storeOp = WGPUStoreOp_Store; c.clearValue = { 0.0, 0.0, 0.0, 1.0 };
        WGPURenderPassDescriptor rp{}; rp.label = vivid_sv("Webcam Clear"); rp.colorAttachmentCount = 1; rp.colorAttachments = &c;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(gpu->command_encoder, &rp);
        wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    }

    bool lazy_init(const VividGpuContext* gpu) {
        shader_ = vivid::gpu::create_shader(gpu->device, kWebcamWGSL, "Webcam Shader");
        if (!shader_) return false;
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Webcam Sampler");
        WGPUBindGroupLayoutEntry e[2]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment; e[0].sampler.type = WGPUSamplerBindingType_Filtering;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D; e[1].texture.multisampled = false;
        WGPUBindGroupLayoutDescriptor ld{}; ld.label = vivid_sv("Webcam BGL"); ld.entryCount = 2; ld.entries = e;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.label = vivid_sv("Webcam PL"); pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pld);
        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_, gpu->output_format, "Webcam Pipeline");
        return pipeline_ != nullptr;
    }
};

VIVID_REGISTER(WebcamOp)
