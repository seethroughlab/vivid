#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

// =============================================================================
// Mesh Warp WGSL Shaders
// =============================================================================

// Vertex shader: reads control point grid from storage buffer, tessellates
// a subdivided mesh with bicubic-interpolated UVs.
// Fragment shader: samples input texture at interpolated UV.

static const char* kMeshWarpShader = R"(

struct Uniforms {
    resolution: vec2f,
    grid_x: i32,
    grid_y: i32,
    grid_overlay: f32,
    tess_level: i32, // subdivisions between control points
    _pad0: f32,
    _pad1: f32,
};

struct ControlPoint {
    pos: vec2f, // output position (clip-like, in 0..1 range)
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,        // source texture UV
    @location(1) grid_uv: vec2f,   // position in grid space for overlay
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;
@group(0) @binding(3) var<storage, read> control_points: array<ControlPoint>;

// Fetch control point, clamping indices to valid range
fn get_cp(ix: i32, iy: i32) -> vec2f {
    let cx = clamp(ix, 0, uniforms.grid_x);
    let cy = clamp(iy, 0, uniforms.grid_y);
    let idx = cy * (uniforms.grid_x + 1) + cx;
    return control_points[idx].pos;
}

// Catmull-Rom interpolation for smooth curves through control points
fn catmull_rom(p0: vec2f, p1: vec2f, p2: vec2f, p3: vec2f, t: f32) -> vec2f {
    let t2 = t * t;
    let t3 = t2 * t;
    return 0.5 * (
        (2.0 * p1) +
        (-p0 + p2) * t +
        (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
        (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
    );
}

// Bicubic interpolation using Catmull-Rom on control point grid
fn bicubic_sample(fx: f32, fy: f32) -> vec2f {
    let ix = i32(floor(fx));
    let iy = i32(floor(fy));
    let tx = fx - f32(ix);
    let ty = fy - f32(iy);

    // Sample 4 rows, each with 4-point Catmull-Rom along X
    let r0 = catmull_rom(get_cp(ix-1, iy-1), get_cp(ix, iy-1), get_cp(ix+1, iy-1), get_cp(ix+2, iy-1), tx);
    let r1 = catmull_rom(get_cp(ix-1, iy),   get_cp(ix, iy),   get_cp(ix+1, iy),   get_cp(ix+2, iy),   tx);
    let r2 = catmull_rom(get_cp(ix-1, iy+1), get_cp(ix, iy+1), get_cp(ix+1, iy+1), get_cp(ix+2, iy+1), tx);
    let r3 = catmull_rom(get_cp(ix-1, iy+2), get_cp(ix, iy+2), get_cp(ix+1, iy+2), get_cp(ix+2, iy+2), tx);

    // Catmull-Rom along Y
    return catmull_rom(r0, r1, r2, r3, ty);
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VertexOutput {
    let tess = uniforms.tess_level;
    let cells_x = uniforms.grid_x * tess;
    let cells_y = uniforms.grid_y * tess;

    // Each cell is 2 triangles = 6 vertices
    let cell_id = vid / 6u;
    let vert_in_cell = vid % 6u;

    let cell_x = i32(cell_id % u32(cells_x));
    let cell_y = i32(cell_id / u32(cells_x));

    // Local vertex within cell quad (two triangles)
    var lx: i32;
    var ly: i32;
    switch vert_in_cell {
        case 0u { lx = 0; ly = 0; }
        case 1u { lx = 1; ly = 0; }
        case 2u { lx = 0; ly = 1; }
        case 3u { lx = 1; ly = 0; }
        case 4u { lx = 1; ly = 1; }
        default { lx = 0; ly = 1; }
    }

    let vx = cell_x + lx;
    let vy = cell_y + ly;

    // Normalized grid coordinates — this is the source texture UV
    let src_u = f32(vx) / f32(cells_x);
    let src_v = f32(vy) / f32(cells_y);

    // Map to control point grid space and do bicubic interpolation
    let cp_x = src_u * f32(uniforms.grid_x);
    let cp_y = src_v * f32(uniforms.grid_y);
    let warped = bicubic_sample(cp_x, cp_y);

    // Convert from 0..1 to clip space -1..1
    let clip_pos = vec2f(warped.x * 2.0 - 1.0, 1.0 - warped.y * 2.0);

    var out: VertexOutput;
    out.position = vec4f(clip_pos, 0.0, 1.0);
    out.uv = vec2f(src_u, src_v);
    out.grid_uv = vec2f(cp_x, cp_y);
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var color = textureSample(inputTex, texSampler, input.uv);

    // Grid overlay: draw lines at control point boundaries
    if (uniforms.grid_overlay > 0.001) {
        let gx = abs(fract(input.grid_uv.x) - 0.5);
        let gy = abs(fract(input.grid_uv.y) - 0.5);
        let line = 1.0 - smoothstep(0.0, 0.03, min(gx, gy));

        // Control point dots
        let dx = fract(input.grid_uv.x) - 0.5;
        let dy = fract(input.grid_uv.y) - 0.5;
        let dot_dist = sqrt(dx * dx + dy * dy);
        let dot = 1.0 - smoothstep(0.0, 0.06, dot_dist);

        let overlay = max(line * 0.6, dot);
        let grid_color = vec4f(0.0, 1.0, 0.5, 1.0) * overlay * uniforms.grid_overlay;
        color = vec4f(mix(color.rgb, grid_color.rgb, grid_color.a), max(color.a, grid_color.a));
    }

    return color;
}
)";

// =============================================================================
// CPU Uniform struct (matches WGSL Uniforms)
// =============================================================================

struct MeshWarpUniforms {
    float resolution[2];
    int   grid_x;
    int   grid_y;
    float grid_overlay;
    int   tess_level;
    float _pad0;
    float _pad1;
};

struct ControlPointGpu {
    float x, y;
};

// =============================================================================
// Mesh Warp Operator
// =============================================================================

struct MeshWarp : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "MeshWarp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   grid_x       {"grid_x",       4, 2, 16};
    vivid::Param<int>   grid_y       {"grid_y",       4, 2, 16};
    vivid::Param<float> grid_overlay {"grid_overlay",  0.0f, 0.0f, 1.0f};

    static constexpr int kTessLevel = 8; // subdivisions between each pair of control points

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&grid_x);
        out.push_back(&grid_y);
        out.push_back(&grid_overlay);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"source",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (init_failed_) {
            vivid_report_gpu_error(ctx, shader_error_msg_.c_str());
            return;
        }

        int gx = grid_x.int_value();
        int gy = grid_y.int_value();

        // Rebuild pipeline if grid dimensions changed
        if (gx != cached_gx_ || gy != cached_gy_) {
            release_all();
        }

        if (!pipeline_) {
            if (!lazy_init(ctx, gx, gy)) {
                init_failed_ = true;
                return;
            }
            cached_gx_ = gx;
            cached_gy_ = gy;
        }

        // Get input texture
        WGPUTextureView input_tex = nullptr;
        if (ctx->input_texture_views && ctx->input_texture_count >= 1)
            input_tex = ctx->input_texture_views[0];
        if (!input_tex && !fallback_view_) create_fallback(ctx);
        if (!input_tex) input_tex = fallback_view_;

        // Update uniforms
        MeshWarpUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.grid_x        = gx;
        u.grid_y        = gy;
        u.grid_overlay  = grid_overlay.value;
        u.tess_level    = kTessLevel;
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        // Update control points — identity grid (can be animated via parameter automation)
        int cp_count = (gx + 1) * (gy + 1);
        std::vector<ControlPointGpu> cps(cp_count);
        for (int cy = 0; cy <= gy; ++cy) {
            for (int cx = 0; cx <= gx; ++cx) {
                auto& cp = cps[cy * (gx + 1) + cx];
                cp.x = static_cast<float>(cx) / static_cast<float>(gx);
                cp.y = static_cast<float>(cy) / static_cast<float>(gy);
            }
        }
        wgpuQueueWriteBuffer(ctx->queue, storage_buf_, 0, cps.data(),
                             cp_count * sizeof(ControlPointGpu));

        // Recreate bind group when input texture changes
        if (input_tex != cached_input_tex_) {
            if (cached_bind_group_)
                wgpuBindGroupRelease(cached_bind_group_);

            WGPUBindGroupEntry entries[4]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].offset  = 0;
            entries[0].size    = sizeof(MeshWarpUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = input_tex;
            entries[3].binding = 3;
            entries[3].buffer  = storage_buf_;
            entries[3].offset  = 0;
            entries[3].size    = static_cast<uint64_t>(cp_count) * sizeof(ControlPointGpu);

            WGPUBindGroupDescriptor bg_desc{};
            bg_desc.label = vivid_sv("MeshWarp BG");
            bg_desc.layout = bind_layout_;
            bg_desc.entryCount = 4;
            bg_desc.entries = entries;
            cached_bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);
            cached_input_tex_ = input_tex;
        }

        // Render the tessellated mesh
        int cells_x = gx * kTessLevel;
        int cells_y = gy * kTessLevel;
        uint32_t vertex_count = static_cast<uint32_t>(cells_x * cells_y * 6);

        if (!ctx->output_texture_view) return;
        WGPURenderPassColorAttachment color_att{};
        color_att.view = ctx->output_texture_view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.loadOp  = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = WGPUColor{0, 0, 0, 0};

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv("MeshWarp Pass");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(ctx->command_encoder, &rp_desc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, cached_bind_group_, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, vertex_count, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    ~MeshWarp() override {
        release_all();
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    WGPURenderPipeline  pipeline_      = nullptr;
    WGPUBindGroupLayout bind_layout_   = nullptr;
    WGPUBuffer          uniform_buf_   = nullptr;
    WGPUBuffer          storage_buf_   = nullptr;
    WGPUShaderModule    shader_        = nullptr;
    WGPUPipelineLayout  pipe_layout_   = nullptr;
    WGPUSampler         sampler_       = nullptr;
    WGPUTexture         fallback_tex_  = nullptr;
    WGPUTextureView     fallback_view_ = nullptr;
    WGPUBindGroup       cached_bind_group_ = nullptr;
    WGPUTextureView     cached_input_tex_  = nullptr;
    int                 cached_gx_ = 0;
    int                 cached_gy_ = 0;
    bool                init_failed_       = false;
    std::string         shader_error_msg_;

    void release_all() {
        vivid::gpu::release(cached_bind_group_);
        cached_input_tex_ = nullptr;
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(storage_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sampler_);
    }

    void create_fallback(const VividGpuContext* gpu) {
        WGPUTextureDescriptor td{};
        td.label = vivid_sv("MeshWarp Fallback");
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

    bool lazy_init(const VividGpuContext* gpu, int gx, int gy) {
        // Compile shader with error scope
        wgpuDevicePushErrorScope(gpu->device, WGPUErrorFilter_Validation);
        shader_ = vivid::gpu::create_shader(gpu->device, kMeshWarpShader, "MeshWarp Shader");
        {
            WGPUPopErrorScopeCallbackInfo cb{};
            cb.mode = WGPUCallbackMode_AllowSpontaneous;
            cb.callback = [](WGPUPopErrorScopeStatus, WGPUErrorType type,
                              WGPUStringView msg, void* ud1, void*) {
                if (type != WGPUErrorType_NoError) {
                    auto* self = static_cast<MeshWarp*>(ud1);
                    self->shader_error_msg_ = msg.data
                        ? std::string(msg.data, msg.length) : "unknown WGSL error";
                    std::fprintf(stderr, "[mesh_warp] WGSL error: %s\n",
                                 self->shader_error_msg_.c_str());
                }
            };
            cb.userdata1 = this;
            wgpuDevicePopErrorScope(gpu->device, cb);
        }
        if (!shader_error_msg_.empty() || !shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(MeshWarpUniforms), "MeshWarp Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "MeshWarp Sampler");

        // Storage buffer for control points
        int cp_count = (gx + 1) * (gy + 1);
        {
            WGPUBufferDescriptor desc{};
            desc.label = vivid_sv("MeshWarp Control Points");
            desc.size = static_cast<uint64_t>(cp_count) * sizeof(ControlPointGpu);
            desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
            storage_buf_ = wgpuDeviceCreateBuffer(gpu->device, &desc);
        }

        // Bind group layout: uniform(0) + sampler(1) + texture(2) + storage(3)
        WGPUBindGroupLayoutEntry entries[4]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = sizeof(MeshWarpUniforms);

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[2].texture.multisampled = false;

        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Vertex;
        entries[3].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        entries[3].buffer.minBindingSize = static_cast<uint64_t>(cp_count) * sizeof(ControlPointGpu);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("MeshWarp BGL");
        bgl_desc.entryCount = 4;
        bgl_desc.entries = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("MeshWarp Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // Custom pipeline: we draw a tessellated mesh, not a fullscreen triangle,
        // but still use no vertex buffers (vertex data is generated from vertex_index)
        WGPUColorTargetState color_target{};
        color_target.format = gpu->output_format;
        color_target.writeMask = WGPUColorWriteMask_All;

        // Enable alpha blending for clean edges
        WGPUBlendState blend{};
        blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.alpha.operation = WGPUBlendOperation_Add;
        color_target.blend = &blend;

        WGPUFragmentState fragment{};
        fragment.module = shader_;
        fragment.entryPoint = vivid_sv("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &color_target;

        WGPURenderPipelineDescriptor rp_desc{};
        rp_desc.label = vivid_sv("MeshWarp Pipeline");
        rp_desc.layout = pipe_layout_;
        rp_desc.vertex.module = shader_;
        rp_desc.vertex.entryPoint = vivid_sv("vs_main");
        rp_desc.vertex.bufferCount = 0;
        rp_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        rp_desc.primitive.frontFace = WGPUFrontFace_CCW;
        rp_desc.primitive.cullMode = WGPUCullMode_None;
        rp_desc.multisample.count = 1;
        rp_desc.multisample.mask = 0xFFFFFFFF;
        rp_desc.fragment = &fragment;

        pipeline_ = wgpuDeviceCreateRenderPipeline(gpu->device, &rp_desc);
        if (!pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(MeshWarp)
