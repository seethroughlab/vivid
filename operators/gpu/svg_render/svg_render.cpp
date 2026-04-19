#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

// =============================================================================
// SVG Render WGSL Fragment Shader
// =============================================================================

static const char* kSvgFragment = R"(

struct Uniforms {
    override_r: f32,
    override_g: f32,
    override_b: f32,
    use_override: f32,
    bg_r: f32,
    bg_g: f32,
    bg_b: f32,
    bg_a: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var svg_sampler: sampler;
@group(0) @binding(2) var svg_texture: texture_2d<f32>;

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
    let svg = textureSample(svg_texture, svg_sampler, input.uv);
    let bg = vec4f(uniforms.bg_r, uniforms.bg_g, uniforms.bg_b, uniforms.bg_a);

    var color = svg;
    if (uniforms.use_override > 0.5) {
        let tint = vec3f(uniforms.override_r, uniforms.override_g, uniforms.override_b);
        color = vec4f(tint * svg.a, svg.a);
    }

    // Composite SVG over background
    let out_a = color.a + bg.a * (1.0 - color.a);
    let out_rgb = select(
        bg.rgb,
        (color.rgb * color.a + bg.rgb * bg.a * (1.0 - color.a)) / out_a,
        out_a > 0.001
    );
    return vec4f(out_rgb, out_a);
}
)";

// =============================================================================
// Uniform struct matching the WGSL Uniforms
// =============================================================================

struct SvgUniforms {
    float override_r, override_g, override_b;
    float use_override;
    float bg_r, bg_g, bg_b, bg_a;
};
/**
 * @brief Renders SVG files using the nanosvg rasterizer.
 *
 * Loads and rasterizes .svg files to a texture with configurable scale,
 * position, and optional color override for tinting.
 *
 * @see TextureLoader, Text
 */
struct SvgRender : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "SvgRender";
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::FilePath> file {"file", ""};
    vivid::Param<float> scale      {"scale",      1.0f, 0.01f, 10.0f};
    vivid::Param<float> x          {"x",          0.0f, -1.0f, 1.0f};
    vivid::Param<float> y          {"y",          0.0f, -1.0f, 1.0f};
    vivid::Param<float> override_r {"override_r", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> override_g {"override_g", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> override_b {"override_b", 1.0f, 0.0f, 1.0f};
    vivid::Param<int> use_color_override {"use_color_override", 0, 0, 1};
    vivid::Param<float> bg_r {"bg_r", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_g {"bg_g", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_b {"bg_b", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_a {"bg_a", 0.0f, 0.0f, 1.0f};

    SvgRender() {
        vivid::description(file, "Path to the SVG file to render");
        vivid::description(scale, "Scale multiplier applied to the SVG natural size");
        vivid::description(x, "Horizontal position offset from center");
        vivid::description(y, "Vertical position offset from center");
        vivid::description(override_r, "Red component of the tint color when override is enabled");
        vivid::description(override_g, "Green component of the tint color when override is enabled");
        vivid::description(override_b, "Blue component of the tint color when override is enabled");
        vivid::description(use_color_override, "Replace SVG colors with the override tint color");
        vivid::description(bg_r, "Red component of the background color");
        vivid::description(bg_g, "Green component of the background color");
        vivid::description(bg_b, "Blue component of the background color");
        vivid::description(bg_a, "Opacity of the background, 0 = transparent");

        vivid::semantic_tag(x, "position_xy");
        vivid::semantic_shape(x, "scalar");
        vivid::semantic_tag(y, "position_xy");
        vivid::semantic_shape(y, "scalar");

        vivid::semantic_tag(override_r, "color_rgba");
        vivid::semantic_shape(override_r, "scalar");
        vivid::semantic_tag(override_g, "color_rgba");
        vivid::semantic_shape(override_g, "scalar");
        vivid::semantic_tag(override_b, "color_rgba");
        vivid::semantic_shape(override_b, "scalar");

        vivid::semantic_tag(bg_r, "color_rgba");
        vivid::semantic_shape(bg_r, "scalar");
        vivid::semantic_tag(bg_g, "color_rgba");
        vivid::semantic_shape(bg_g, "scalar");
        vivid::semantic_tag(bg_b, "color_rgba");
        vivid::semantic_shape(bg_b, "scalar");
        vivid::semantic_tag(bg_a, "color_rgba");
        vivid::semantic_shape(bg_a, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(x, VIVID_DISPLAY_XY_PAD);
        display_hint(y, VIVID_DISPLAY_XY_PAD);
        display_hint(override_r, VIVID_DISPLAY_COLOR);
        display_hint(override_g, VIVID_DISPLAY_COLOR);
        display_hint(override_b, VIVID_DISPLAY_COLOR);
        display_hint(bg_r, VIVID_DISPLAY_COLOR);
        display_hint(bg_g, VIVID_DISPLAY_COLOR);
        display_hint(bg_b, VIVID_DISPLAY_COLOR);

        out.push_back(&file);
        out.push_back(&scale);
        out.push_back(&x);
        out.push_back(&y);
        out.push_back(&override_r);
        out.push_back(&override_g);
        out.push_back(&override_b);
        out.push_back(&use_color_override);
        out.push_back(&bg_r);
        out.push_back(&bg_g);
        out.push_back(&bg_b);
        out.push_back(&bg_a);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[svg_render] lazy_init FAILED\n");
                return;
            }
        }

        std::string current_file = file.str_value;
        uint32_t w = ctx->output_width;
        uint32_t h = ctx->output_height;

        // Re-rasterize when file, scale, or output resolution changes
        if (current_file != last_file_ || scale.value != last_scale_ ||
            w != last_w_ || h != last_h_) {
            rasterize_svg(ctx, w, h, current_file);
            last_file_ = current_file;
            last_scale_ = scale.value;
            last_w_ = w;
            last_h_ = h;
        }

        // Update uniforms
        SvgUniforms u{};
        u.override_r = override_r.value;
        u.override_g = override_g.value;
        u.override_b = override_b.value;
        u.use_override = static_cast<float>(use_color_override.int_value());
        u.bg_r = bg_r.value;
        u.bg_g = bg_g.value;
        u.bg_b = bg_b.value;
        u.bg_a = bg_a.value;

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "SVG Render Pass");
    }

    ~SvgRender() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(svg_tex_);
        vivid::gpu::release(svg_view_);
    }

private:
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUSampler         sampler_     = nullptr;
    WGPUTexture         svg_tex_     = nullptr;
    WGPUTextureView     svg_view_    = nullptr;
    WGPUDevice          device_      = nullptr;
    WGPUQueue           queue_       = nullptr;

    // Change-detection state
    std::string last_file_;
    float       last_scale_ = -1.0f;
    uint32_t    last_w_ = 0;
    uint32_t    last_h_ = 0;

    void rasterize_svg(const VividGpuContext* gpu, uint32_t w, uint32_t h,
                       const std::string& path) {
        if (path.empty()) return;

        NSVGimage* image = nsvgParseFromFile(path.c_str(), "px", 96.0f);
        if (!image) {
            std::fprintf(stderr, "[svg_render] failed to parse SVG: %s\n", path.c_str());
            return;
        }

        // Compute render dimensions from SVG natural size * scale
        float svg_w = image->width * scale.value;
        float svg_h = image->height * scale.value;
        uint32_t rw = static_cast<uint32_t>(svg_w + 0.5f);
        uint32_t rh = static_cast<uint32_t>(svg_h + 0.5f);
        if (rw < 1) rw = 1;
        if (rh < 1) rh = 1;

        // Request output size matching SVG dimensions
        vivid_request_output_size(gpu, rw, rh);

        // Rasterize to RGBA8
        NSVGrasterizer* rast = nsvgCreateRasterizer();
        if (!rast) {
            nsvgDelete(image);
            return;
        }

        // Use the actual output dimensions for the raster buffer
        uint32_t tex_w = rw;
        uint32_t tex_h = rh;

        std::vector<unsigned char> rgba(tex_w * tex_h * 4, 0);
        nsvgRasterize(rast, image, 0, 0, scale.value, rgba.data(), tex_w, tex_h, tex_w * 4);
        nsvgDeleteRasterizer(rast);
        nsvgDelete(image);

        // Upload to GPU texture (RGBA8Unorm)
        if (svg_tex_ && (last_w_ != tex_w || last_h_ != tex_h)) {
            vivid::gpu::release(svg_tex_);
            vivid::gpu::release(svg_view_);
            svg_tex_ = nullptr;
            svg_view_ = nullptr;
        }

        if (!svg_tex_) {
            WGPUTextureDescriptor tex_desc{};
            tex_desc.label = vivid_sv("SVG Texture");
            tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            tex_desc.dimension = WGPUTextureDimension_2D;
            tex_desc.size = {tex_w, tex_h, 1};
            tex_desc.format = WGPUTextureFormat_RGBA8Unorm;
            tex_desc.mipLevelCount = 1;
            tex_desc.sampleCount = 1;
            svg_tex_ = wgpuDeviceCreateTexture(device_, &tex_desc);

            WGPUTextureViewDescriptor view_desc{};
            view_desc.label = vivid_sv("SVG Texture View");
            view_desc.format = WGPUTextureFormat_RGBA8Unorm;
            view_desc.dimension = WGPUTextureViewDimension_2D;
            view_desc.mipLevelCount = 1;
            view_desc.arrayLayerCount = 1;
            svg_view_ = wgpuTextureCreateView(svg_tex_, &view_desc);

            rebuild_bind_group();
        }

        // WebGPU requires bytesPerRow aligned to 256
        uint32_t bytes_per_row = tex_w * 4;
        uint32_t aligned_bpr = (bytes_per_row + 255) & ~255u;

        // Create padded staging buffer if alignment requires it
        std::vector<unsigned char> staging;
        const unsigned char* upload_data = rgba.data();
        size_t upload_size = rgba.size();

        if (aligned_bpr != bytes_per_row) {
            staging.resize(aligned_bpr * tex_h, 0);
            for (uint32_t row = 0; row < tex_h; ++row) {
                std::memcpy(staging.data() + row * aligned_bpr,
                           rgba.data() + row * bytes_per_row, bytes_per_row);
            }
            upload_data = staging.data();
            upload_size = staging.size();
        }

        WGPUTexelCopyBufferLayout layout{};
        layout.offset = 0;
        layout.bytesPerRow = aligned_bpr;
        layout.rowsPerImage = tex_h;

        WGPUTexelCopyTextureInfo dest{};
        dest.texture = svg_tex_;
        dest.mipLevel = 0;
        dest.origin = {0, 0, 0};

        WGPUExtent3D extent = {tex_w, tex_h, 1};
        wgpuQueueWriteTexture(queue_, &dest, upload_data, upload_size, &layout, &extent);
    }

    void rebuild_bind_group() {
        vivid::gpu::release(bind_group_);

        WGPUBindGroupEntry entries[3]{};
        entries[0].binding = 0;
        entries[0].buffer  = uniform_buf_;
        entries[0].offset  = 0;
        entries[0].size    = sizeof(SvgUniforms);

        entries[1].binding = 1;
        entries[1].sampler = sampler_;

        entries[2].binding = 2;
        entries[2].textureView = svg_view_;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("SVG Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 3;
        bg_desc.entries = entries;
        bind_group_ = wgpuDeviceCreateBindGroup(device_, &bg_desc);
    }

    bool lazy_init(const VividGpuContext* gpu) {
        device_ = gpu->device;
        queue_  = gpu->queue;

        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kSvgFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "SVG Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(SvgUniforms), "SVG Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "SVG Sampler");

        // Bind group layout: uniform (0), sampler (1), texture (2)
        WGPUBindGroupLayoutEntry bgl_entries[3]{};

        bgl_entries[0].binding = 0;
        bgl_entries[0].visibility = WGPUShaderStage_Fragment;
        bgl_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entries[0].buffer.minBindingSize = sizeof(SvgUniforms);

        bgl_entries[1].binding = 1;
        bgl_entries[1].visibility = WGPUShaderStage_Fragment;
        bgl_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        bgl_entries[2].binding = 2;
        bgl_entries[2].visibility = WGPUShaderStage_Fragment;
        bgl_entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        bgl_entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("SVG BGL");
        bgl_desc.entryCount = 3;
        bgl_desc.entries = bgl_entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("SVG Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // Create 1x1 placeholder texture so bind group is valid before rasterize
        WGPUTextureDescriptor tex_desc{};
        tex_desc.label = vivid_sv("SVG Placeholder");
        tex_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        tex_desc.dimension = WGPUTextureDimension_2D;
        tex_desc.size = {1, 1, 1};
        tex_desc.format = WGPUTextureFormat_RGBA8Unorm;
        tex_desc.mipLevelCount = 1;
        tex_desc.sampleCount = 1;
        svg_tex_ = wgpuDeviceCreateTexture(gpu->device, &tex_desc);

        WGPUTextureViewDescriptor view_desc{};
        view_desc.label = vivid_sv("SVG Placeholder View");
        view_desc.format = WGPUTextureFormat_RGBA8Unorm;
        view_desc.dimension = WGPUTextureViewDimension_2D;
        view_desc.mipLevelCount = 1;
        view_desc.arrayLayerCount = 1;
        svg_view_ = wgpuTextureCreateView(svg_tex_, &view_desc);

        rebuild_bind_group();

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_,
                                                 gpu->output_format, "SVG Pipeline");
        if (!pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(SvgRender)
