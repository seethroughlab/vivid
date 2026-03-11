#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// =============================================================================
// TimeMachine WGSL Shaders
// =============================================================================

// Blit shader: passthrough copy of source into a single cache layer
static const char* kBlitFragment = R"(

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var texSampler: sampler;
@group(0) @binding(1) var inputTex: texture_2d<f32>;

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
    return textureSample(inputTex, texSampler, input.uv);
}
)";

// Slit-scan shader: per-pixel temporal displacement via a grayscale map
static const char* kSlitScanFragment = R"(

struct Uniforms {
    depth: f32,
    offset: f32,
    frame_count: u32,
    write_index: u32,
    filled: u32,
    _pad0: u32,
    _pad1: u32,
    _pad2: u32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var cacheTex: texture_2d_array<f32>;
@group(0) @binding(3) var mapTex: texture_2d<f32>;

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
    let map_color = textureSample(mapTex, texSampler, input.uv);
    let disp = dot(map_color.rgb, vec3f(0.2126, 0.7152, 0.0722));

    let max_age = f32(uniforms.filled - 1u);
    let age = clamp((uniforms.offset + uniforms.depth * (1.0 - disp)) * max_age, 0.0, max_age);

    let age_lo = floor(age);
    let age_hi = min(age_lo + 1.0, max_age);
    let t = age - age_lo;

    let idx_a = (uniforms.write_index + uniforms.frame_count - u32(age_lo)) % uniforms.frame_count;
    let idx_b = (uniforms.write_index + uniforms.frame_count - u32(age_hi)) % uniforms.frame_count;

    let sample_a = textureSample(cacheTex, texSampler, input.uv, i32(idx_a));
    let sample_b = textureSample(cacheTex, texSampler, input.uv, i32(idx_b));

    return mix(sample_a, sample_b, t);
}
)";

// =============================================================================
// Uniform struct matching WGSL (32 bytes)
// =============================================================================

struct TimeMachineUniforms {
    float    depth;
    float    offset;
    uint32_t frame_count;
    uint32_t write_index;
    uint32_t filled;
    uint32_t _pad0, _pad1, _pad2;
};

// =============================================================================
// TimeMachine Operator
// =============================================================================

struct TimeMachine : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "TimeMachine";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> depth  {"depth",  1.0f, 0.0f, 1.0f};
    vivid::Param<int>   frames {"frames", 30,   2,    120};
    vivid::Param<float> offset {"offset", 0.0f, 0.0f, 1.0f};

    TimeMachine() {
        vivid::semantic_tag(depth, "phase_01");
        vivid::semantic_shape(depth, "scalar");

        vivid::semantic_tag(frames, "count");
        vivid::semantic_shape(frames, "int");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&depth);
        out.push_back(&frames);
        out.push_back(&offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"source",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"map",     VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[time_machine] lazy_init FAILED\n");
                return;
            }
        }

        // Resolve input textures with fallbacks
        WGPUTextureView source_tex = nullptr;
        WGPUTextureView map_tex    = nullptr;
        if (ctx->input_texture_views) {
            if (ctx->input_texture_count >= 1) source_tex = ctx->input_texture_views[0];
            if (ctx->input_texture_count >= 2) map_tex    = ctx->input_texture_views[1];
        }

        if (!source_tex) {
            if (!src_fallback_view_) create_src_fallback(ctx);
            source_tex = src_fallback_view_;
        }
        if (!map_tex) {
            if (!map_fallback_view_) create_map_fallback(ctx);
            map_tex = map_fallback_view_;
        }

        int frame_count = frames.int_value();

        // Recreate cache if resolution or frame count changed
        if (ctx->output_width != cached_width_ || ctx->output_height != cached_height_ ||
            frame_count != cached_frames_) {
            recreate_cache(ctx, frame_count);
            cached_width_  = ctx->output_width;
            cached_height_ = ctx->output_height;
            cached_frames_ = frame_count;
        }

        // Rebuild bind groups if inputs changed
        if (source_tex != cached_source_tex_ || map_tex != cached_map_tex_ || bind_groups_dirty_) {
            rebuild_bind_groups(ctx, source_tex, map_tex);
            cached_source_tex_ = source_tex;
            cached_map_tex_    = map_tex;
            bind_groups_dirty_ = false;
        }

        static constexpr WGPUColor kClearTransparent{0, 0, 0, 0};

        // Pass 1: Blit source into cache layer at write_index_
        vivid::gpu::run_pass(ctx->command_encoder, blit_pipeline_, blit_bg_,
                             layer_views_[write_index_], "TimeMachine Blit", kClearTransparent);

        // After blit we have one more valid frame in the buffer
        uint32_t new_filled = std::min(filled_ + 1, static_cast<uint32_t>(frame_count));

        // Write uniforms for the slit-scan pass
        TimeMachineUniforms u{};
        u.depth       = depth.value;
        u.offset      = offset.value;
        u.frame_count = static_cast<uint32_t>(frame_count);
        u.write_index = write_index_;
        u.filled      = new_filled;
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        // Pass 2: Slit-scan — sample cache via displacement map → output
        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, main_bg_,
                             ctx->output_texture_view, "TimeMachine SlitScan", kClearTransparent);

        // Advance ring buffer
        write_index_ = (write_index_ + 1) % static_cast<uint32_t>(frame_count);
        filled_      = new_filled;
    }

    ~TimeMachine() override {
        vivid::gpu::release(blit_pipeline_);
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(blit_bind_layout_);
        vivid::gpu::release(main_bind_layout_);
        vivid::gpu::release(blit_pipe_layout_);
        vivid::gpu::release(main_pipe_layout_);
        vivid::gpu::release(blit_shader_);
        vivid::gpu::release(main_shader_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(blit_bg_);
        vivid::gpu::release(main_bg_);
        release_cache();
        vivid::gpu::release(src_fallback_tex_);
        vivid::gpu::release(src_fallback_view_);
        vivid::gpu::release(map_fallback_tex_);
        vivid::gpu::release(map_fallback_view_);
    }

private:
    // Pipelines
    WGPURenderPipeline blit_pipeline_ = nullptr;
    WGPURenderPipeline pipeline_      = nullptr;

    // Layouts
    WGPUBindGroupLayout blit_bind_layout_ = nullptr;
    WGPUBindGroupLayout main_bind_layout_ = nullptr;
    WGPUPipelineLayout  blit_pipe_layout_ = nullptr;
    WGPUPipelineLayout  main_pipe_layout_ = nullptr;

    // Shader modules
    WGPUShaderModule blit_shader_ = nullptr;
    WGPUShaderModule main_shader_ = nullptr;

    // Shared resources
    WGPUBuffer  uniform_buf_ = nullptr;
    WGPUSampler sampler_     = nullptr;

    // Bind groups
    WGPUBindGroup blit_bg_ = nullptr;
    WGPUBindGroup main_bg_ = nullptr;

    // Cache texture array (ring buffer of N historical frames)
    WGPUTexture                  cache_tex_  = nullptr;
    WGPUTextureView              cache_view_ = nullptr;   // full 2DArray view for sampling
    std::vector<WGPUTextureView> layer_views_;             // per-layer 2D views for blit targets

    // Fallback textures
    WGPUTexture     src_fallback_tex_  = nullptr;
    WGPUTextureView src_fallback_view_ = nullptr;
    WGPUTexture     map_fallback_tex_  = nullptr;
    WGPUTextureView map_fallback_view_ = nullptr;

    // Ring buffer state
    uint32_t write_index_   = 0;
    uint32_t filled_        = 0;
    int      cached_frames_ = 0;

    // Cache tracking
    WGPUTextureView cached_source_tex_ = nullptr;
    WGPUTextureView cached_map_tex_    = nullptr;
    uint32_t cached_width_  = 0;
    uint32_t cached_height_ = 0;
    bool bind_groups_dirty_ = true;

    // -------------------------------------------------------------------------
    // Create 1x1 black fallback texture (source — black when disconnected)
    // -------------------------------------------------------------------------

    void create_src_fallback(const VividGpuContext* gpu) {
        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("TimeMachine Src Fallback");
        td.size          = {1, 1, 1};
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.dimension     = WGPUTextureDimension_2D;
        td.format        = gpu->output_format;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        src_fallback_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format          = gpu->output_format;
        vd.dimension       = WGPUTextureViewDimension_2D;
        vd.mipLevelCount   = 1;
        vd.arrayLayerCount = 1;
        vd.aspect          = WGPUTextureAspect_All;
        src_fallback_view_ = wgpuTextureCreateView(src_fallback_tex_, &vd);

        const uint8_t zero[8] = {};  // 8 bytes of zero for RGBA16Float
        WGPUTexelCopyTextureInfo dest{};
        dest.texture = src_fallback_tex_;
        dest.aspect  = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = 8;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &dest, zero, sizeof(zero), &layout, &extent);
    }

    // -------------------------------------------------------------------------
    // Create 1x1 white fallback texture (map — passthrough when disconnected)
    // -------------------------------------------------------------------------

    void create_map_fallback(const VividGpuContext* gpu) {
        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("TimeMachine Map Fallback");
        td.size          = {1, 1, 1};
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.dimension     = WGPUTextureDimension_2D;
        td.format        = gpu->output_format;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        map_fallback_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format          = gpu->output_format;
        vd.dimension       = WGPUTextureViewDimension_2D;
        vd.mipLevelCount   = 1;
        vd.arrayLayerCount = 1;
        vd.aspect          = WGPUTextureAspect_All;
        map_fallback_view_ = wgpuTextureCreateView(map_fallback_tex_, &vd);

        // RGBA16Float white: half-float 1.0 = 0x3C00
        const uint16_t white[4] = {0x3C00, 0x3C00, 0x3C00, 0x3C00};
        WGPUTexelCopyTextureInfo dest{};
        dest.texture = map_fallback_tex_;
        dest.aspect  = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = 8;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &dest, white, sizeof(white), &layout, &extent);
    }

    // -------------------------------------------------------------------------
    // Release cache texture array and all layer views
    // -------------------------------------------------------------------------

    void release_cache() {
        for (auto& v : layer_views_)
            vivid::gpu::release(v);
        layer_views_.clear();
        vivid::gpu::release(cache_view_);
        vivid::gpu::release(cache_tex_);
    }

    // -------------------------------------------------------------------------
    // Create/recreate the cache texture array and per-layer views
    // -------------------------------------------------------------------------

    void recreate_cache(const VividGpuContext* gpu, int frame_count) {
        release_cache();

        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("TimeMachine Cache");
        td.size          = {gpu->output_width, gpu->output_height, static_cast<uint32_t>(frame_count)};
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.dimension     = WGPUTextureDimension_2D;
        td.format        = gpu->output_format;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
        cache_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        // Full 2DArray view for shader sampling
        {
            WGPUTextureViewDescriptor vd{};
            vd.format          = gpu->output_format;
            vd.dimension       = WGPUTextureViewDimension_2DArray;
            vd.baseMipLevel    = 0;
            vd.mipLevelCount   = 1;
            vd.baseArrayLayer  = 0;
            vd.arrayLayerCount = static_cast<uint32_t>(frame_count);
            vd.aspect          = WGPUTextureAspect_All;
            cache_view_ = wgpuTextureCreateView(cache_tex_, &vd);
        }

        // Per-layer 2D views for blit render targets
        layer_views_.resize(frame_count);
        for (int i = 0; i < frame_count; ++i) {
            WGPUTextureViewDescriptor vd{};
            vd.format          = gpu->output_format;
            vd.dimension       = WGPUTextureViewDimension_2D;
            vd.baseMipLevel    = 0;
            vd.mipLevelCount   = 1;
            vd.baseArrayLayer  = static_cast<uint32_t>(i);
            vd.arrayLayerCount = 1;
            vd.aspect          = WGPUTextureAspect_All;
            layer_views_[i] = wgpuTextureCreateView(cache_tex_, &vd);
        }

        // Reset ring buffer state
        write_index_ = 0;
        filled_      = 0;
        bind_groups_dirty_ = true;
    }

    // -------------------------------------------------------------------------
    // Rebuild bind groups when inputs or cache change
    // -------------------------------------------------------------------------

    void rebuild_bind_groups(const VividGpuContext* gpu, WGPUTextureView source_tex, WGPUTextureView map_tex) {
        vivid::gpu::release(blit_bg_);
        vivid::gpu::release(main_bg_);

        // Blit bind group: sampler + source texture
        {
            WGPUBindGroupEntry entries[2]{};
            entries[0].binding = 0;
            entries[0].sampler = sampler_;
            entries[1].binding = 1;
            entries[1].textureView = source_tex;

            WGPUBindGroupDescriptor desc{};
            desc.label      = vivid_sv("TimeMachine Blit BG");
            desc.layout     = blit_bind_layout_;
            desc.entryCount = 2;
            desc.entries    = entries;
            blit_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Main bind group: uniform + sampler + cache array + map
        {
            WGPUBindGroupEntry entries[4]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(TimeMachineUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = cache_view_;
            entries[3].binding = 3;
            entries[3].textureView = map_tex;

            WGPUBindGroupDescriptor desc{};
            desc.label      = vivid_sv("TimeMachine Main BG");
            desc.layout     = main_bind_layout_;
            desc.entryCount = 4;
            desc.entries    = entries;
            main_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }
    }

    // -------------------------------------------------------------------------
    // One-time GPU resource initialization
    // -------------------------------------------------------------------------

    bool lazy_init(const VividGpuContext* gpu) {
        // Shader modules
        blit_shader_ = vivid::gpu::create_shader(gpu->device, kBlitFragment, "TimeMachine Blit Shader");
        main_shader_ = vivid::gpu::create_shader(gpu->device, kSlitScanFragment, "TimeMachine SlitScan Shader");
        if (!blit_shader_ || !main_shader_)
            return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(TimeMachineUniforms), "TimeMachine Uniforms");
        sampler_     = vivid::gpu::create_linear_sampler(gpu->device, "TimeMachine Sampler");

        // Blit bind group layout: sampler(0) + texture_2d(1)
        {
            WGPUBindGroupLayoutEntry entries[2]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[1].texture.multisampled  = false;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label      = vivid_sv("TimeMachine Blit BGL");
            bgl_desc.entryCount = 2;
            bgl_desc.entries    = entries;
            blit_bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // Main bind group layout: uniform(0) + sampler(1) + texture_2d_array(2) + texture_2d(3)
        {
            WGPUBindGroupLayoutEntry entries[4]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(TimeMachineUniforms);

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[2].binding    = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2DArray;
            entries[2].texture.multisampled  = false;

            entries[3].binding    = 3;
            entries[3].visibility = WGPUShaderStage_Fragment;
            entries[3].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[3].texture.multisampled  = false;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label      = vivid_sv("TimeMachine Main BGL");
            bgl_desc.entryCount = 4;
            bgl_desc.entries    = entries;
            main_bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // Pipeline layouts
        {
            WGPUPipelineLayoutDescriptor pl_desc{};
            pl_desc.label               = vivid_sv("TimeMachine Blit Pipeline Layout");
            pl_desc.bindGroupLayoutCount = 1;
            pl_desc.bindGroupLayouts     = &blit_bind_layout_;
            blit_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);
        }
        {
            WGPUPipelineLayoutDescriptor pl_desc{};
            pl_desc.label               = vivid_sv("TimeMachine Main Pipeline Layout");
            pl_desc.bindGroupLayoutCount = 1;
            pl_desc.bindGroupLayouts     = &main_bind_layout_;
            main_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);
        }

        // Render pipelines
        blit_pipeline_ = vivid::gpu::create_pipeline(gpu->device, blit_shader_, blit_pipe_layout_,
                                                     gpu->output_format, "TimeMachine Blit Pipeline");
        pipeline_      = vivid::gpu::create_pipeline(gpu->device, main_shader_, main_pipe_layout_,
                                                     gpu->output_format, "TimeMachine SlitScan Pipeline");

        if (!blit_pipeline_ || !pipeline_)
            return false;

        // Create initial cache
        recreate_cache(gpu, frames.int_value());

        return true;
    }
};

VIVID_REGISTER(TimeMachine)
