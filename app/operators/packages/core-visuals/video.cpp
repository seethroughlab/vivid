// Core visual package operator: Video — a SELF-DECODING movie source. Owns its own file, decoder
// and texture (like Webcam owns its capture), so each Video node plays its own clip chosen through
// the param view — no shared global source, no host injection. Routes standard codecs through
// AVFoundation and HAP through the direct BC-texture path (deps/hap + snappy), decoding on the
// render thread and uploading each frame to a wgpu texture it blits into the graph.
//
// A generator (no inputs). Video-only for now; the decoder abstraction keeps current_time/seek/
// duration so A/V-synced audio can be layered on later (see movie/video_decoder.h).
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include "movie/decoder_factory.h"   // load_video_decoder_for_path (probe -> AVF | HAP)
#include "movie/hap_codec.h"         // VideoCompressedFormat + bytes_per_block

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

// Blit (+ optional HapQ YCoCg reconstruction). The fullscreen vertex preamble is prepended by
// create_shader_checked. u.ycocg selects the scaled-YCoCg decode HapQ (HapY) frames need.
const char* kVideoWGSL = R"(
@group(0) @binding(0) var tex: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;
struct U { ycocg: f32, _p0: f32, _p1: f32, _p2: f32 };
@group(0) @binding(2) var<uniform> u: U;
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    var c = textureSample(tex, samp, inp.uv);
    if (u.ycocg > 0.5) {
        let offset = 0.5019607843137255;           // 128/255
        let Y  = c.a;
        let scale = (c.b * 31.875) + 1.0;          // 255/8 = 31.875
        let Co = (c.r - offset) / scale;
        let Cg = (c.g - offset) / scale;
        c = vec4f(Y + Co - Cg, Y + Cg, Y - Co - Cg, 1.0);
    }
    return c;
}
)";

WGPUTextureFormat wgpu_bc_format(VideoCompressedFormat f) {
    switch (f) {
        case VideoCompressedFormat::BC1: return WGPUTextureFormat_BC1RGBAUnorm;
        case VideoCompressedFormat::BC3: return WGPUTextureFormat_BC3RGBAUnorm;
        case VideoCompressedFormat::BC4: return WGPUTextureFormat_BC4RUnorm;
        default:                         return WGPUTextureFormat_Undefined;
    }
}
}  // namespace

struct VideoOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Video";
    static constexpr const char* kDisplayName = "Video";
    static constexpr const char* kSummary = "Play a movie file (AVFoundation + HAP) as a source in the chain.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "video", "movie"};

    vivid::Param<vivid::FilePath> file{"file", ""};
    vivid::Param<int>   play_mode{"play_mode", 0, {"Loop", "Once"}};   // Once holds the last frame
    vivid::Param<float> speed{"speed", 1.0f, 0.0f, 4.0f};

    VideoOp() {
        vivid::description(file, "Movie file to play (mp4/mov/… and HAP-encoded .mov)");
        vivid::asset_kind(file, "video");
        vivid::semantic_tag(file, "path_video");
        vivid::description(speed, "Playback rate multiplier (1 = normal, 0 = pause)");
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&file); o.push_back(&play_mode); o.push_back(&speed);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }

    ~VideoOp() override { if (dec_) dec_->close(); release_gpu(); }

    void process_gpu(const VividGpuContext* c) override {
        if (init_failed_) { vivid_report_gpu_error(c, err_.c_str()); return; }
        if (!pipe_ && !lazy_init(c)) { init_failed_ = true; vivid_report_gpu_error(c, err_.c_str()); return; }

        const float* p = c->param_values; auto pv = [&](int i, float d){ return p ? p[i] : d; };
        const int   want_mode  = static_cast<int>(pv(1, static_cast<float>(play_mode.value)) + 0.5f);
        const float want_speed = pv(2, speed.value);

        if (file.str_value != loaded_path_) { loaded_path_ = file.str_value; open_decoder(c, want_mode, want_speed); }
        if (dec_ && want_mode  != applied_mode_)  { applied_mode_  = want_mode;  dec_->set_loop(want_mode == 0); }
        if (dec_ && want_speed != applied_speed_) { applied_speed_ = want_speed; dec_->set_speed(want_speed); }

        if (dec_ && dec_->decode_frame() == DecodeStatus::NewFrame) upload_frame(c);
        blit(c);
    }

private:
    std::unique_ptr<VideoDecoder> dec_;
    std::string loaded_path_ = "\x01";   // sentinel != any path so the first open fires (even empty)
    int   applied_mode_  = -1;
    float applied_speed_ = -1.f;
    bool  bc_supported_  = false;
    bool  init_failed_   = false; std::string err_;

    // Blit pipeline (tex + sampler + a 1-float ycocg uniform).
    WGPUShaderModule    sh_  = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline  pipe_ = nullptr; WGPUSampler samp_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    // Staging texture the decoded frame uploads into.
    WGPUTexture     tex_  = nullptr; WGPUTextureView view_ = nullptr;
    WGPUTextureFormat tex_fmt_ = WGPUTextureFormat_Undefined; uint32_t tex_w_ = 0, tex_h_ = 0;
    bool ycocg_ = false; bool have_frame_ = false;

    void open_decoder(const VividGpuContext* /*c*/, int mode, float spd) {
        if (dec_) { dec_->close(); dec_.reset(); }
        have_frame_ = false;
        tex_fmt_ = WGPUTextureFormat_Undefined; tex_w_ = tex_h_ = 0;   // force a texture rebuild on next frame
        if (loaded_path_.empty()) return;
        DecoderLoadResult r = load_video_decoder_for_path(loaded_path_, bc_supported_, nullptr);
        if (!r.success || !r.decoder) {
            std::fprintf(stderr, "[Video] open failed (%s): %s\n", loaded_path_.c_str(), r.diagnostics.c_str());
            return;
        }
        dec_ = std::move(r.decoder);
        dec_->set_loop(mode == 0);
        dec_->set_speed(spd);
        applied_mode_ = mode; applied_speed_ = spd;
        std::fprintf(stderr, "[Video] %s (%s)\n", loaded_path_.c_str(), r.diagnostics.c_str());
    }

    void ensure_texture(const VividGpuContext* c, WGPUTextureFormat fmt, uint32_t w, uint32_t h) {
        if (tex_ && tex_fmt_ == fmt && tex_w_ == w && tex_h_ == h) return;
        if (view_) { wgpuTextureViewRelease(view_); view_ = nullptr; }
        if (tex_)  { wgpuTextureRelease(tex_); tex_ = nullptr; }
        WGPUTextureDescriptor td{}; td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = { w, h, 1 }; td.format = fmt;
        td.mipLevelCount = 1; td.sampleCount = 1;
        tex_ = wgpuDeviceCreateTexture(c->device, &td);
        view_ = wgpuTextureCreateView(tex_, nullptr);
        tex_fmt_ = fmt; tex_w_ = w; tex_h_ = h;
        rebuild_bind_group(c);
    }

    void upload_frame(const VividGpuContext* c) {
        const uint32_t w = dec_->width(), h = dec_->height();
        if (!w || !h) return;
        if (dec_->compression_mode() == VideoFrameCompressionMode::CompressedBC) {
            const VideoCompressedFormat cf = dec_->compressed_format();
            const WGPUTextureFormat fmt = wgpu_bc_format(cf);
            const size_t bpb = vivid_compressed_bytes_per_block(cf);
            if (fmt == WGPUTextureFormat_Undefined || bpb == 0 || !dec_->compressed_data()) return;
            ensure_texture(c, fmt, w, h);
            ycocg_ = dec_->requires_ycocg_decode();
            const uint32_t blocks_w = (w + 3) / 4, blocks_h = (h + 3) / 4;
            WGPUTexelCopyTextureInfo dst{}; dst.texture = tex_; dst.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferLayout lay{}; lay.bytesPerRow = blocks_w * (uint32_t)bpb; lay.rowsPerImage = blocks_h;
            WGPUExtent3D ext{ w, h, 1 };
            wgpuQueueWriteTexture(c->queue, &dst, dec_->compressed_data(), dec_->compressed_size(), &lay, &ext);
        } else {
            const uint8_t* px = dec_->pixel_data();
            if (!px) return;
            ensure_texture(c, WGPUTextureFormat_BGRA8Unorm, w, h);
            ycocg_ = false;
            WGPUTexelCopyTextureInfo dst{}; dst.texture = tex_; dst.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferLayout lay{}; lay.bytesPerRow = w * 4; lay.rowsPerImage = h;
            WGPUExtent3D ext{ w, h, 1 };
            wgpuQueueWriteTexture(c->queue, &dst, px, (size_t)w * h * 4, &lay, &ext);
        }
        have_frame_ = true;
    }

    void blit(const VividGpuContext* c) {
        const float u[4] = { ycocg_ ? 1.f : 0.f, 0.f, 0.f, 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        WGPURenderPassColorAttachment cat{};
        cat.view = c->output_texture_view; cat.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        cat.loadOp = WGPULoadOp_Clear; cat.storeOp = WGPUStoreOp_Store; cat.clearValue = { 0, 0, 0, 1 };
        WGPURenderPassDescriptor rpd{}; rpd.colorAttachmentCount = 1; rpd.colorAttachments = &cat;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(c->command_encoder, &rpd);
        if (have_frame_ && bg_) {
            wgpuRenderPassEncoderSetPipeline(pass, pipe_);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg_, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    void rebuild_bind_group(const VividGpuContext* c) {
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        if (!view_) return;
        WGPUBindGroupEntry be[3]{};
        be[0].binding = 0; be[0].textureView = view_;
        be[1].binding = 1; be[1].sampler = samp_;
        be[2].binding = 2; be[2].buffer = ubo_; be[2].size = 16;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 3; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
    }

    bool lazy_init(const VividGpuContext* c) {
        bc_supported_ = wgpuDeviceHasFeature(c->device, WGPUFeatureName_TextureCompressionBC);
        sh_ = vivid::gpu::create_shader_checked(c->device, kVideoWGSL, "Video", err_);
        if (!sh_ || !err_.empty()) { if (err_.empty()) err_ = "shader null"; return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 16, "Video U");
        WGPUBindGroupLayoutEntry e[3]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment;
        e[0].texture.sampleType = WGPUTextureSampleType_Float; e[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment; e[1].sampler.type = WGPUSamplerBindingType_Filtering;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment; e[2].buffer.type = WGPUBufferBindingType_Uniform; e[2].buffer.minBindingSize = 16;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 3; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Video");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        if (!pipe_) { err_ = "pipeline null"; return false; }
        return true;
    }

    void release_gpu() {
        if (bg_) wgpuBindGroupRelease(bg_); if (view_) wgpuTextureViewRelease(view_); if (tex_) wgpuTextureRelease(tex_);
        if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
};

VIVID_REGISTER(VideoOp)

// ADR-0021/P3: drop a movie file onto the graph -> a Video node with its "file" param set.
static const char* const kVideoDropExts[] = { ".mp4", ".mov", ".m4v", ".avi" };
static const VividFileDropHandlerDescriptor kVideoDrop[] = {
    { "Movie", kVideoDropExts, 4, "file", 10, "Play as a video" }
};
VIVID_FILE_DROP(kVideoDrop)
