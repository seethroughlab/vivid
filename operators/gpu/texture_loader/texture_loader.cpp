#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "../../shared/movie_decode/texture_upload.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <cctype>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ---------------------------------------------------------------------------
// Blit shader — samples a texture and writes to the output
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

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool is_hdr_extension(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".hdr" || ext == ".exr";
}

// Pack float32 RGBA pixels to float16 RGBA in-place (result written to dst).
static void convert_rgba_f32_to_f16(const float* src, uint16_t* dst, int pixel_count) {
    for (int i = 0; i < pixel_count * 4; ++i) {
        float f = src[i];
        uint32_t x;
        std::memcpy(&x, &f, 4);
        uint32_t sign = (x >> 16) & 0x8000u;
        int32_t  exp  = static_cast<int32_t>((x >> 23) & 0xFFu) - 112;
        uint32_t mant = x & 0x7FFFFFu;
        if (exp <= 0)       { dst[i] = static_cast<uint16_t>(sign); continue; }
        if (exp >= 31)      { dst[i] = static_cast<uint16_t>(sign | 0x7C00u); continue; }
        dst[i] = static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
    }
}
/**
 * @brief Loads image files (JPEG, PNG, EXR, HDR) as GPU textures.
 *
 * Supports both LDR (JPEG/PNG via stb_image) and HDR (EXR/Radiance)
 * formats. Hot-reloads on file change.
 *
 * @see MovieFileIn, SvgRender
 */
struct TextureLoader : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "TextureLoader";
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::FilePath> file {"file"};

    TextureLoader() {
        vivid::semantic_tag(file, "path_image");
        vivid::semantic_shape(file, "path");
        vivid::description(file, "Path to an image file (PNG, JPEG, EXR, HDR)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[texture_loader] lazy_init FAILED\n");
                return;
            }
        }

        // Reload only when the file path changes
        const std::string& path = file.str_value;
        if (path != loaded_path_) {
            loaded_path_ = path;
            load_texture(ctx);
        }

        if (texture_.view && texture_.bind_group) {
            if (texture_.width > 0 && texture_.height > 0) {
                vivid_request_output_size(ctx, texture_.width, texture_.height);
            }
            vivid::gpu::run_pass(ctx->command_encoder, pipeline_, texture_.bind_group,
                                 ctx->output_texture_view, "TextureLoader Blit");
        }
    }

    ~TextureLoader() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(sampler_);
        movie_texture_release(texture_);
        if (hdr_texture_)  { wgpuTextureRelease(hdr_texture_);     hdr_texture_  = nullptr; }
        if (hdr_view_)     { wgpuTextureViewRelease(hdr_view_);     hdr_view_     = nullptr; }
        if (hdr_bind_group_) { wgpuBindGroupRelease(hdr_bind_group_); hdr_bind_group_ = nullptr; }
    }

private:
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUSampler         sampler_     = nullptr;

    // LDR path uses MovieTextureState (BGRA8Unorm)
    MovieTextureState texture_{};

    // HDR path (RGBA16Float) — separate resources
    WGPUTexture     hdr_texture_    = nullptr;
    WGPUTextureView hdr_view_       = nullptr;
    WGPUBindGroup   hdr_bind_group_ = nullptr;

    std::string loaded_path_;
    bool        loaded_hdr_ = false;

    void release_hdr() {
        if (hdr_bind_group_) { wgpuBindGroupRelease(hdr_bind_group_); hdr_bind_group_ = nullptr; }
        if (hdr_view_)       { wgpuTextureViewRelease(hdr_view_);     hdr_view_       = nullptr; }
        if (hdr_texture_)    { wgpuTextureRelease(hdr_texture_);      hdr_texture_    = nullptr; }
    }

    void load_texture(const VividGpuContext* ctx) {
        // Release previous GPU resources
        movie_texture_release(texture_);
        release_hdr();
        loaded_hdr_ = false;

        if (loaded_path_.empty()) {
            show_magenta_placeholder(ctx);
            return;
        }

        if (is_hdr_extension(loaded_path_)) {
            load_hdr(ctx);
        } else {
            load_ldr(ctx);
        }
    }

    void load_ldr(const VividGpuContext* ctx) {
        int w = 0, h = 0, channels = 0;
        uint8_t* data = stbi_load(loaded_path_.c_str(), &w, &h, &channels, 4);
        if (!data) {
            std::fprintf(stderr, "[texture_loader] Failed to load: %s\n", loaded_path_.c_str());
            show_magenta_placeholder(ctx);
            return;
        }

        // stbi returns RGBA; convert to BGRA for WebGPU
        for (int i = 0; i < w * h; ++i) {
            std::swap(data[i * 4 + 0], data[i * 4 + 2]);
        }

        movie_texture_recreate(ctx->device, sampler_, bind_layout_, texture_,
                               static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                               WGPUTextureFormat_BGRA8Unorm, false);
        movie_upload_bgra(ctx->queue, texture_, data,
                          static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        stbi_image_free(data);
        std::fprintf(stderr, "[texture_loader] Loaded: %s (%dx%d)\n", loaded_path_.c_str(), w, h);
    }

    void load_hdr(const VividGpuContext* ctx) {
        int w = 0, h = 0, channels = 0;
        float* data = stbi_loadf(loaded_path_.c_str(), &w, &h, &channels, 4);
        if (!data) {
            std::fprintf(stderr, "[texture_loader] Failed to load HDR: %s\n", loaded_path_.c_str());
            show_magenta_placeholder(ctx);
            return;
        }

        const int pixel_count = w * h;
        std::vector<uint16_t> f16(static_cast<size_t>(pixel_count) * 4);
        convert_rgba_f32_to_f16(data, f16.data(), pixel_count);
        stbi_image_free(data);

        // Create RGBA16Float texture
        WGPUTextureDescriptor tex_desc{};
        tex_desc.label         = vivid_sv("TextureLoader HDR Texture");
        tex_desc.dimension     = WGPUTextureDimension_2D;
        tex_desc.size          = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        tex_desc.mipLevelCount = 1;
        tex_desc.sampleCount   = 1;
        tex_desc.format        = WGPUTextureFormat_RGBA16Float;
        tex_desc.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        hdr_texture_ = wgpuDeviceCreateTexture(ctx->device, &tex_desc);
        if (!hdr_texture_) {
            std::fprintf(stderr, "[texture_loader] Failed to create HDR texture for: %s\n", loaded_path_.c_str());
            show_magenta_placeholder(ctx);
            return;
        }

        // Upload
        WGPUTexelCopyTextureInfo dst{};
        dst.texture  = hdr_texture_;
        dst.mipLevel = 0;
        dst.origin   = {0, 0, 0};
        dst.aspect   = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.offset       = 0;
        layout.bytesPerRow  = static_cast<uint32_t>(w) * 4u * sizeof(uint16_t);
        layout.rowsPerImage = static_cast<uint32_t>(h);

        WGPUExtent3D extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        wgpuQueueWriteTexture(ctx->queue, &dst,
                              f16.data(), f16.size() * sizeof(uint16_t),
                              &layout, &extent);

        // Texture view
        WGPUTextureViewDescriptor view_desc{};
        view_desc.format          = WGPUTextureFormat_RGBA16Float;
        view_desc.dimension       = WGPUTextureViewDimension_2D;
        view_desc.mipLevelCount   = 1;
        view_desc.arrayLayerCount = 1;
        hdr_view_ = wgpuTextureCreateView(hdr_texture_, &view_desc);

        // Bind group (reuses bind_layout_ from lazy_init)
        WGPUBindGroupEntry bg_entries[2]{};
        bg_entries[0].binding     = 0;
        bg_entries[0].sampler     = sampler_;
        bg_entries[1].binding     = 1;
        bg_entries[1].textureView = hdr_view_;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label      = vivid_sv("TextureLoader HDR BG");
        bg_desc.layout     = bind_layout_;
        bg_desc.entryCount = 2;
        bg_desc.entries    = bg_entries;
        hdr_bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);

        // Store dims in texture_ for vivid_request_output_size (view/bind_group left null)
        texture_.width  = static_cast<uint32_t>(w);
        texture_.height = static_cast<uint32_t>(h);

        // Redirect the blit to use HDR resources
        texture_.view       = hdr_view_;
        texture_.bind_group = hdr_bind_group_;

        loaded_hdr_ = true;
        std::fprintf(stderr, "[texture_loader] Loaded HDR: %s (%dx%d)\n", loaded_path_.c_str(), w, h);
    }

    void show_magenta_placeholder(const VividGpuContext* ctx) {
        // 1×1 BGRA magenta: B=255, G=0, R=255, A=255
        static const uint8_t kMagenta[4] = {255, 0, 255, 255};
        movie_texture_recreate(ctx->device, sampler_, bind_layout_, texture_,
                               1, 1, WGPUTextureFormat_BGRA8Unorm, false);
        movie_upload_bgra(ctx->queue, texture_, kMagenta, 1, 1);
    }

    bool lazy_init(const VividGpuContext* ctx) {
        shader_ = vivid::gpu::create_shader(ctx->device, kBlitFragment, "TextureLoader Shader");
        if (!shader_) return false;

        sampler_ = vivid::gpu::create_linear_sampler(ctx->device, "TextureLoader Sampler");

        WGPUBindGroupLayoutEntry entries[2]{};
        entries[0].binding              = 0;
        entries[0].visibility           = WGPUShaderStage_Fragment;
        entries[0].sampler.type         = WGPUSamplerBindingType_Filtering;

        entries[1].binding                    = 1;
        entries[1].visibility                 = WGPUShaderStage_Fragment;
        entries[1].texture.sampleType         = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension      = WGPUTextureViewDimension_2D;
        entries[1].texture.multisampled       = false;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label      = vivid_sv("TextureLoader BGL");
        bgl_desc.entryCount = 2;
        bgl_desc.entries    = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label                  = vivid_sv("TextureLoader Pipeline Layout");
        pl_desc.bindGroupLayoutCount   = 1;
        pl_desc.bindGroupLayouts       = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(ctx->device, shader_, pipe_layout_,
                                                 ctx->output_format, "TextureLoader Pipeline");
        return pipeline_ != nullptr;
    }
};

static const char* kTextureLoaderImageExts[] = {
    ".png",
    ".jpg",
    ".jpeg",
};

static const VividFileDropHandlerDescriptor kTextureLoaderFileDrops[] = {{
    "Load Image",
    kTextureLoaderImageExts,
    3,
    "file",
    100,
    "Create a TextureLoader node from a dropped image file.",
}};

VIVID_REGISTER(TextureLoader)
VIVID_FILE_DROP(kTextureLoaderFileDrops)
