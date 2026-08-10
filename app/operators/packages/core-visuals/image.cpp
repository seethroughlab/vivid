// Core visual package operator: Image — load a still image (PNG/JPG/…) into a GPU
// texture and blit it. Self-contained: owns its own texture (not the shared video
// source). The file path arrives via the loadable ABI's file-param channel — a
// Param<FilePath> whose str_value the host populates from ctx.file_param_values
// (VisualGraph resolves the per-node value against the project dir). Decodes with
// stb_image; reloads only when the path changes.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {
const char* kImageWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
@group(0) @binding(0) var tex: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    return textureSample(tex, samp, inp.uv);
}
)";

// Upload tightly-packed RGBA8 pixels into a fresh sampleable texture.
WGPUTexture make_texture(const VividGpuContext* c, const uint8_t* rgba, uint32_t w, uint32_t h) {
    WGPUTextureDescriptor td{};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size = { w, h, 1 };
    td.format = WGPUTextureFormat_RGBA8Unorm;   // shader reads rgba; wgpu maps to the BGRA output target
    td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture t = wgpuDeviceCreateTexture(c->device, &td);
    if (!t) return nullptr;
    WGPUTexelCopyTextureInfo dst{}; dst.texture = t; dst.mipLevel = 0; dst.origin = {0,0,0}; dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout lay{}; lay.offset = 0; lay.bytesPerRow = w * 4; lay.rowsPerImage = h;
    WGPUExtent3D ext{ w, h, 1 };
    wgpuQueueWriteTexture(c->queue, &dst, rgba, static_cast<size_t>(w) * h * 4, &lay, &ext);
    return t;
}
}  // namespace

struct ImageOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Image";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
    static constexpr const char* kDisplayName = "Image";
    static constexpr const char* kSummary = "Load a still image (PNG/JPG/…) from a file into the chain.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "image", "texture"};

    vivid::Param<vivid::FilePath> path{"file", ""};

    ImageOp() {
        vivid::description(path, "Image file to load (PNG/JPG/BMP/…)");
        vivid::asset_kind(path, "image");   // ADR-0021/P3: filters the file dialog / drop targets
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&path); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(vivid::texture_output());
    }

    ~ImageOp() override {
        release_img();
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }

    void process_gpu(const VividGpuContext* c) override {
        if (init_failed_) { vivid_report_gpu_error(c, err_.c_str()); return; }
        if (!pipe_) { if (!lazy_init(c)) { init_failed_ = true; return; } }
        // Reload only when the (host-resolved) path changes.
        if (path.str_value != loaded_path_) { loaded_path_ = path.str_value; load_image(c); }
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Image");
    }

private:
    WGPUShaderModule    sh_  = nullptr;
    WGPUBindGroupLayout bgl_ = nullptr;
    WGPUPipelineLayout  pl_  = nullptr;
    WGPURenderPipeline  pipe_ = nullptr;
    WGPUSampler         samp_ = nullptr;
    WGPUBindGroup       bg_  = nullptr;
    WGPUTexture         img_tex_ = nullptr;
    WGPUTextureView     img_view_ = nullptr;
    std::string         loaded_path_ = "\x01";   // sentinel != "" so an empty path still triggers the fallback once
    bool                init_failed_ = false;
    std::string         err_;

    void release_img() {
        if (img_view_) { wgpuTextureViewRelease(img_view_); img_view_ = nullptr; }
        if (img_tex_)  { wgpuTextureRelease(img_tex_); img_tex_ = nullptr; }
    }
    // A 2x2 checker so the node always renders something before a file is chosen / on failure.
    void set_fallback(const VividGpuContext* c) {
        static const uint8_t px[16] = { 40,40,48,255,  90,90,100,255,  90,90,100,255,  40,40,48,255 };
        swap_texture(c, make_texture(c, px, 2, 2));
    }
    void load_image(const VividGpuContext* c) {
        if (loaded_path_.empty()) { set_fallback(c); return; }
        int w = 0, h = 0, ch = 0;
        uint8_t* data = stbi_load(loaded_path_.c_str(), &w, &h, &ch, 4);   // force RGBA
        if (!data || w <= 0 || h <= 0) {
            std::fprintf(stderr, "[Image] failed to load: %s\n", loaded_path_.c_str());
            if (data) stbi_image_free(data);
            set_fallback(c);
            return;
        }
        swap_texture(c, make_texture(c, data, static_cast<uint32_t>(w), static_cast<uint32_t>(h)));
        stbi_image_free(data);
    }
    void swap_texture(const VividGpuContext* c, WGPUTexture t) {
        if (!t) return;
        release_img();
        img_tex_ = t;
        img_view_ = wgpuTextureCreateView(img_tex_, nullptr);
        rebuild_bind_group(c);
    }
    void rebuild_bind_group(const VividGpuContext* c) {
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[2]{};
        be[0].binding = 0; be[0].textureView = img_view_;
        be[1].binding = 1; be[1].sampler = samp_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 2; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kImageWGSL, "Image", err);
        if (!sh_ || !err.empty()) { err_ = err.empty() ? "shader module null" : err; return false; }
        WGPUBindGroupLayoutEntry e[2]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment;
        e[0].texture.sampleType = WGPUTextureSampleType_Float; e[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment; e[1].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 2; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Image Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        if (!pipe_) return false;
        set_fallback(c);   // ensures bg_ is valid before the first path load
        return bg_ != nullptr;
    }
};

VIVID_REGISTER(ImageOp)

// ADR-0021/P3: drop a still image onto the graph -> an Image node with its "file" param set.
static const char* const kImageDropExts[] = { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd" };
static const VividFileDropHandlerDescriptor kImageDrop[] = {
    { "Image", kImageDropExts, 7, "file", 10, "Load as an image" }
};
VIVID_FILE_DROP(kImageDrop)
