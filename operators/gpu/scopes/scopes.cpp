#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <string>

// =============================================================================
// Scopes WGSL Shaders
// =============================================================================

// Pass 1: Analysis — renders the scope visualization to an intermediate texture.
// Uses textureLoad (integer coords) to read input without filtering.
static const char* kScopesAnalysisFragment = R"(

struct Uniforms {
    resolution: vec2f,       // output resolution
    input_width: f32,
    input_height: f32,
    scope_type: i32,         // 0=Waveform, 1=Histogram, 2=Vectorscope, 3=Parade
    subsample: i32,
    brightness: f32,
    opacity: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

fn luminance(rgb: vec3f) -> f32 {
    return dot(rgb, vec3f(0.2126, 0.7152, 0.0722));
}

fn waveform(uv: vec2f) -> vec4f {
    let in_w = i32(uniforms.input_width);
    let in_h = i32(uniforms.input_height);
    let sub = max(uniforms.subsample, 1);

    // Map x to input column
    let col = i32(uv.x * f32(in_w));
    // Map y to luma range (bottom=0, top=1)
    let target_luma = 1.0 - uv.y;

    var accum = vec3f(0.0);
    let bin_width = 1.0 / f32(textureDimensions(inputTex).y);

    for (var row = 0; row < in_h; row += sub) {
        let px = textureLoad(inputTex, vec2i(clamp(col, 0, in_w - 1), row), 0);
        let luma = luminance(px.rgb);
        let dist = abs(luma - target_luma);
        if (dist < bin_width * 2.0) {
            let w = 1.0 - dist / (bin_width * 2.0);
            accum += px.rgb * w;
        }
    }

    let scale = uniforms.brightness * f32(sub) / f32(in_h) * 80.0;
    return vec4f(accum * scale, 1.0);
}

fn histogram(uv: vec2f) -> vec4f {
    let in_w = i32(uniforms.input_width);
    let in_h = i32(uniforms.input_height);
    let sub = max(uniforms.subsample, 1);

    // Map x to luma bin (0..255)
    let bin = i32(uv.x * 255.0);

    var count = 0.0;
    for (var y = 0; y < in_h; y += sub) {
        for (var x = 0; x < in_w; x += sub) {
            let px = textureLoad(inputTex, vec2i(x, y), 0);
            let luma = luminance(px.rgb);
            let px_bin = i32(luma * 255.0);
            if (px_bin == bin) {
                count += 1.0;
            }
        }
    }

    // Normalize
    let total = f32(in_w / sub) * f32(in_h / sub);
    let height = count / total * 50.0 * uniforms.brightness;

    // Draw bar from bottom
    let bar_top = 1.0 - uv.y;
    if (bar_top < height) {
        return vec4f(0.7, 0.7, 0.7, 1.0);
    }
    return vec4f(0.0, 0.0, 0.0, 1.0);
}

fn vectorscope(uv: vec2f) -> vec4f {
    let in_w = i32(uniforms.input_width);
    let in_h = i32(uniforms.input_height);
    let sub = max(uniforms.subsample, 1);

    // Map UV to CbCr space: center = (0.5, 0.5) maps to (0,0)
    let target_cb = (uv.x - 0.5) * 2.0;
    let target_cr = (0.5 - uv.y) * 2.0; // Y flipped: top = positive Cr

    var accum = 0.0;
    let radius = 2.0 / f32(min(textureDimensions(inputTex).x, textureDimensions(inputTex).y));

    for (var y = 0; y < in_h; y += sub) {
        for (var x = 0; x < in_w; x += sub) {
            let px = textureLoad(inputTex, vec2i(x, y), 0);
            // BT.709 YCbCr
            let cb = -0.1687 * px.r - 0.3313 * px.g + 0.5 * px.b;
            let cr =  0.5 * px.r - 0.4187 * px.g - 0.0813 * px.b;

            let dist = length(vec2f(cb - target_cb, cr - target_cr));
            if (dist < radius * 4.0) {
                accum += 1.0 - dist / (radius * 4.0);
            }
        }
    }

    let scale = uniforms.brightness * f32(sub * sub) / (f32(in_w) * f32(in_h)) * 800.0;
    let v = accum * scale;

    // Graticule: draw ring at 75% saturation
    let dist_from_center = length(vec2f(target_cb, target_cr));
    let graticule_ring = 0.75;
    let ring = 1.0 - smoothstep(0.0, 0.01, abs(dist_from_center - graticule_ring));
    let ring_color = vec3f(0.3) * ring;

    // Crosshair at center
    let cross_h = 1.0 - smoothstep(0.0, 0.003, abs(target_cb));
    let cross_v = 1.0 - smoothstep(0.0, 0.003, abs(target_cr));
    let cross = max(cross_h, cross_v) * 0.2;
    let guide = max(ring_color, vec3f(cross));

    return vec4f(vec3f(v) + guide, 1.0);
}

fn parade(uv: vec2f) -> vec4f {
    let in_w = i32(uniforms.input_width);
    let in_h = i32(uniforms.input_height);
    let sub = max(uniforms.subsample, 1);

    // Split into 3 columns: R, G, B
    let section = i32(uv.x * 3.0);
    let local_x = fract(uv.x * 3.0);

    let col = i32(local_x * f32(in_w));
    let target_val = 1.0 - uv.y;

    var accum = 0.0;
    let bin_width = 1.0 / f32(textureDimensions(inputTex).y);

    for (var row = 0; row < in_h; row += sub) {
        let px = textureLoad(inputTex, vec2i(clamp(col, 0, in_w - 1), row), 0);
        var channel_val: f32;
        if (section == 0) { channel_val = px.r; }
        else if (section == 1) { channel_val = px.g; }
        else { channel_val = px.b; }

        let dist = abs(channel_val - target_val);
        if (dist < bin_width * 2.0) {
            accum += 1.0 - dist / (bin_width * 2.0);
        }
    }

    let scale = uniforms.brightness * f32(sub) / f32(in_h) * 80.0;
    let v = accum * scale;

    var color = vec3f(v);
    if (section == 0) { color = vec3f(v, v * 0.3, v * 0.3); }
    else if (section == 1) { color = vec3f(v * 0.3, v, v * 0.3); }
    else { color = vec3f(v * 0.3, v * 0.3, v); }

    // Separator lines between sections
    let edge = 1.0 - smoothstep(0.0, 0.005, min(local_x, 1.0 - local_x));
    color += vec3f(0.15) * edge;

    return vec4f(color, 1.0);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let scope = uniforms.scope_type;
    if (scope == 0) { return waveform(input.uv); }
    if (scope == 1) { return histogram(input.uv); }
    if (scope == 2) { return vectorscope(input.uv); }
    return parade(input.uv);
}
)";

// Pass 2: Composite — blend scope visualization over input (or standalone).
static const char* kScopesCompositeFragment = R"(

struct Uniforms {
    resolution: vec2f,
    input_width: f32,
    input_height: f32,
    scope_type: i32,
    subsample: i32,
    brightness: f32,
    opacity: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var originalTex: texture_2d<f32>;
@group(0) @binding(3) var scopeTex: texture_2d<f32>;

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
    let original = textureSample(originalTex, texSampler, input.uv);
    let scope = textureSample(scopeTex, texSampler, input.uv);

    // Overlay mode: blend scope over dimmed original
    let dimmed = original.rgb * (1.0 - uniforms.opacity * 0.5);
    let blended = dimmed + scope.rgb * uniforms.opacity;
    return vec4f(blended, original.a);
}
)";

// Standalone mode: just output the scope with a dark background
static const char* kScopesStandaloneFragment = R"(

struct Uniforms {
    resolution: vec2f,
    input_width: f32,
    input_height: f32,
    scope_type: i32,
    subsample: i32,
    brightness: f32,
    opacity: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var scopeTex: texture_2d<f32>;

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
    let scope = textureSample(scopeTex, texSampler, input.uv);
    let bg = vec3f(0.05);
    return vec4f(bg + scope.rgb * uniforms.opacity, 1.0);
}
)";

// =============================================================================
// Uniform struct matching WGSL (32 bytes)
// =============================================================================

struct ScopesUniforms {
    float resolution[2];   // 8
    float input_width;     // 4
    float input_height;    // 4
    int32_t scope_type;    // 4
    int32_t subsample;     // 4
    float brightness;      // 4
    float opacity;         // 4
};                         // = 32 bytes
/**
 * @brief Real-time waveform, histogram, vectorscope, and parade analysis.
 *
 * Analyzes an input texture and renders one of four scope types:
 * waveform (luminance plot), histogram (level distribution), vectorscope
 * (CbCr color plot), or parade (per-channel waveforms).
 *
 * @see TextureAnalysis, AudioAnalysis
 */
struct Scopes : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Scopes";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   scope_type  {"scope_type", 0, {"Waveform", "Histogram", "Vectorscope", "Parade"}};
    vivid::Param<float> opacity     {"opacity",    0.8f, 0.0f, 1.0f};
    vivid::Param<int>   overlay     {"overlay",    0, {"Standalone", "Overlay"}};
    vivid::Param<float> brightness  {"brightness", 1.5f, 0.5f, 3.0f};
    vivid::Param<int>   subsample   {"subsample",  4, 1, 8};

    Scopes() {
        vivid::description(scope_type, "Analysis type: Waveform, Histogram, Vectorscope, or Parade");
        vivid::description(opacity, "Visibility of the scope visualization");
        vivid::description(overlay, "Standalone renders on black, Overlay composites over the input");
        vivid::description(brightness, "Gain applied to the scope traces");
        vivid::description(subsample, "Pixel skip factor for faster analysis, higher = less accurate");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scope_type);
        out.push_back(&opacity);
        out.push_back(&overlay);
        out.push_back(&brightness);
        out.push_back(&subsample);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!analysis_pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[scopes] lazy_init FAILED\n");
                return;
            }
        }

        // Get input texture
        WGPUTextureView input_tex = nullptr;
        if (ctx->input_texture_views && ctx->input_texture_count >= 1)
            input_tex = ctx->input_texture_views[0];
        if (!input_tex && !fallback_view_) create_fallback(ctx);
        if (!input_tex) input_tex = fallback_view_;

        // Recreate intermediate if resolution changed
        if (ctx->output_width != cached_width_ || ctx->output_height != cached_height_) {
            recreate_intermediate(ctx);
            cached_width_  = ctx->output_width;
            cached_height_ = ctx->output_height;
        }

        // Determine input dimensions
        float in_w = static_cast<float>(ctx->output_width);
        float in_h = static_cast<float>(ctx->output_height);
        if (ctx->input_texture_widths && ctx->input_texture_count >= 1) {
            in_w = static_cast<float>(ctx->input_texture_widths[0]);
            in_h = static_cast<float>(ctx->input_texture_heights[0]);
        }

        // Write uniforms
        ScopesUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.input_width    = in_w;
        u.input_height   = in_h;
        u.scope_type     = scope_type.int_value();
        u.subsample      = subsample.int_value();
        u.brightness     = brightness.value;
        u.opacity        = opacity.value;
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        // Rebuild bind groups if input texture changed or intermediate recreated
        if (input_tex != cached_input_tex_ || bind_groups_dirty_) {
            rebuild_bind_groups(ctx, input_tex);
            cached_input_tex_ = input_tex;
            bind_groups_dirty_ = false;
        }

        static constexpr WGPUColor kClearBlack{0, 0, 0, 1};

        // Pass 1: Analysis — render scope to intermediate
        vivid::gpu::run_pass(ctx->command_encoder, analysis_pipeline_, analysis_bg_,
                             inter_view_, "Scopes Analysis", kClearBlack);

        // Pass 2: Output — composite or standalone
        bool is_overlay = overlay.int_value() == 1;
        if (is_overlay) {
            vivid::gpu::run_pass(ctx->command_encoder, composite_pipeline_, composite_bg_,
                                 ctx->output_texture_view, "Scopes Composite", kClearBlack);
        } else {
            vivid::gpu::run_pass(ctx->command_encoder, standalone_pipeline_, standalone_bg_,
                                 ctx->output_texture_view, "Scopes Standalone", kClearBlack);
        }
    }

    ~Scopes() override {
        vivid::gpu::release(analysis_pipeline_);
        vivid::gpu::release(composite_pipeline_);
        vivid::gpu::release(standalone_pipeline_);
        vivid::gpu::release(single_bind_layout_);
        vivid::gpu::release(dual_bind_layout_);
        vivid::gpu::release(single_pipe_layout_);
        vivid::gpu::release(dual_pipe_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(analysis_shader_);
        vivid::gpu::release(composite_shader_);
        vivid::gpu::release(standalone_shader_);
        vivid::gpu::release(analysis_bg_);
        vivid::gpu::release(composite_bg_);
        vivid::gpu::release(standalone_bg_);
        vivid::gpu::release(inter_tex_);
        vivid::gpu::release(inter_view_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    // Pipelines
    WGPURenderPipeline analysis_pipeline_   = nullptr;
    WGPURenderPipeline composite_pipeline_  = nullptr;
    WGPURenderPipeline standalone_pipeline_ = nullptr;

    // Layouts
    WGPUBindGroupLayout single_bind_layout_ = nullptr; // uniform + sampler + 1 tex
    WGPUBindGroupLayout dual_bind_layout_   = nullptr; // uniform + sampler + 2 tex
    WGPUPipelineLayout  single_pipe_layout_ = nullptr;
    WGPUPipelineLayout  dual_pipe_layout_   = nullptr;

    // Shader modules
    WGPUShaderModule analysis_shader_   = nullptr;
    WGPUShaderModule composite_shader_  = nullptr;
    WGPUShaderModule standalone_shader_ = nullptr;

    // Shared resources
    WGPUBuffer  uniform_buf_ = nullptr;
    WGPUSampler sampler_     = nullptr;

    // Bind groups
    WGPUBindGroup analysis_bg_   = nullptr;
    WGPUBindGroup composite_bg_  = nullptr;
    WGPUBindGroup standalone_bg_ = nullptr;

    // Intermediate scope texture
    WGPUTexture     inter_tex_  = nullptr;
    WGPUTextureView inter_view_ = nullptr;

    // Fallback
    WGPUTexture     fallback_tex_  = nullptr;
    WGPUTextureView fallback_view_ = nullptr;

    // Cache
    WGPUTextureView cached_input_tex_ = nullptr;
    uint32_t cached_width_  = 0;
    uint32_t cached_height_ = 0;
    bool bind_groups_dirty_ = true;

    void create_fallback(const VividGpuContext* gpu) {
        WGPUTextureDescriptor td{};
        td.label = vivid_sv("Scopes Fallback");
        td.size = { 1, 1, 1 };
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = gpu->output_format;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        fallback_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format = gpu->output_format;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        fallback_view_ = wgpuTextureCreateView(fallback_tex_, &vd);

        const uint8_t zero[8] = {};
        WGPUTexelCopyTextureInfo dest_info{};
        dest_info.texture = fallback_tex_;
        dest_info.mipLevel = 0;
        dest_info.origin = {0, 0, 0};
        dest_info.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 8;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &dest_info, zero, sizeof(zero), &layout, &extent);
    }

    void recreate_intermediate(const VividGpuContext* gpu) {
        vivid::gpu::release(inter_tex_);
        vivid::gpu::release(inter_view_);

        WGPUTextureDescriptor td{};
        td.label = vivid_sv("Scopes Intermediate");
        td.size = { gpu->output_width, gpu->output_height, 1 };
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = gpu->output_format;
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        inter_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format = gpu->output_format;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        vd.aspect = WGPUTextureAspect_All;
        inter_view_ = wgpuTextureCreateView(inter_tex_, &vd);

        bind_groups_dirty_ = true;
    }

    void rebuild_bind_groups(const VividGpuContext* gpu, WGPUTextureView input_tex) {
        vivid::gpu::release(analysis_bg_);
        vivid::gpu::release(composite_bg_);
        vivid::gpu::release(standalone_bg_);

        // Analysis: uniform + sampler + input
        {
            WGPUBindGroupEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(ScopesUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = input_tex;

            WGPUBindGroupDescriptor desc{};
            desc.label = vivid_sv("Scopes Analysis BG");
            desc.layout = single_bind_layout_;
            desc.entryCount = 3;
            desc.entries = entries;
            analysis_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Composite: uniform + sampler + original + scope
        {
            WGPUBindGroupEntry entries[4]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(ScopesUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = input_tex;
            entries[3].binding = 3;
            entries[3].textureView = inter_view_;

            WGPUBindGroupDescriptor desc{};
            desc.label = vivid_sv("Scopes Composite BG");
            desc.layout = dual_bind_layout_;
            desc.entryCount = 4;
            desc.entries = entries;
            composite_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Standalone: uniform + sampler + scope
        {
            WGPUBindGroupEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(ScopesUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = inter_view_;

            WGPUBindGroupDescriptor desc{};
            desc.label = vivid_sv("Scopes Standalone BG");
            desc.layout = single_bind_layout_;
            desc.entryCount = 3;
            desc.entries = entries;
            standalone_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }
    }

    bool lazy_init(const VividGpuContext* gpu) {
        // Shader modules
        analysis_shader_   = vivid::gpu::create_shader(gpu->device, kScopesAnalysisFragment,   "Scopes Analysis Shader");
        composite_shader_  = vivid::gpu::create_shader(gpu->device, kScopesCompositeFragment,  "Scopes Composite Shader");
        standalone_shader_ = vivid::gpu::create_shader(gpu->device, kScopesStandaloneFragment, "Scopes Standalone Shader");
        if (!analysis_shader_ || !composite_shader_ || !standalone_shader_)
            return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(ScopesUniforms), "Scopes Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Scopes Sampler");

        // Single-texture bind group layout: uniform(0) + sampler(1) + tex(2)
        {
            WGPUBindGroupLayoutEntry entries[3]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(ScopesUniforms);

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[2].binding    = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2].texture.multisampled  = false;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label = vivid_sv("Scopes Single BGL");
            bgl_desc.entryCount = 3;
            bgl_desc.entries = entries;
            single_bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // Dual-texture bind group layout: uniform(0) + sampler(1) + texA(2) + texB(3)
        {
            WGPUBindGroupLayoutEntry entries[4]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(ScopesUniforms);

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[2].binding    = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2].texture.multisampled  = false;

            entries[3].binding    = 3;
            entries[3].visibility = WGPUShaderStage_Fragment;
            entries[3].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[3].texture.multisampled  = false;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label = vivid_sv("Scopes Dual BGL");
            bgl_desc.entryCount = 4;
            bgl_desc.entries = entries;
            dual_bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // Pipeline layouts
        {
            WGPUPipelineLayoutDescriptor pl_desc{};
            pl_desc.label = vivid_sv("Scopes Single Pipeline Layout");
            pl_desc.bindGroupLayoutCount = 1;
            pl_desc.bindGroupLayouts = &single_bind_layout_;
            single_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);
        }
        {
            WGPUPipelineLayoutDescriptor pl_desc{};
            pl_desc.label = vivid_sv("Scopes Dual Pipeline Layout");
            pl_desc.bindGroupLayoutCount = 1;
            pl_desc.bindGroupLayouts = &dual_bind_layout_;
            dual_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);
        }

        // Render pipelines
        analysis_pipeline_   = vivid::gpu::create_pipeline(gpu->device, analysis_shader_,   single_pipe_layout_, gpu->output_format, "Scopes Analysis Pipeline");
        composite_pipeline_  = vivid::gpu::create_pipeline(gpu->device, composite_shader_,  dual_pipe_layout_,   gpu->output_format, "Scopes Composite Pipeline");
        standalone_pipeline_ = vivid::gpu::create_pipeline(gpu->device, standalone_shader_, single_pipe_layout_, gpu->output_format, "Scopes Standalone Pipeline");

        if (!analysis_pipeline_ || !composite_pipeline_ || !standalone_pipeline_)
            return false;

        // Create initial intermediate
        recreate_intermediate(gpu);

        return true;
    }
};

VIVID_REGISTER(Scopes)
