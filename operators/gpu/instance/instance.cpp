#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

// =============================================================================
// Instance WGSL Shader (custom vertex + fragment, NOT fullscreen triangle)
// =============================================================================

static const char* kInstanceShader = R"(

struct Uniforms {
    resolution: vec2f,
    instance_count: f32,
    size: f32,
    hue_spread: f32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
};

struct Instance {
    pos: vec2f,
    value: f32,
    _pad: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) value: f32,
    @location(2) instance_id: f32,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var tex_sampler: sampler;
@group(0) @binding(2) var input_tex: texture_2d<f32>;
@group(1) @binding(0) var<storage, read> instances: array<Instance>;

fn hsv2rgb(h: f32, s: f32, v: f32) -> vec3f {
    let c = v * s;
    let x = c * (1.0 - abs(((h * 6.0) % 2.0) - 1.0));
    let m = v - c;
    var rgb: vec3f;
    let hh = h * 6.0;
    if (hh < 1.0)      { rgb = vec3f(c, x, 0.0); }
    else if (hh < 2.0) { rgb = vec3f(x, c, 0.0); }
    else if (hh < 3.0) { rgb = vec3f(0.0, c, x); }
    else if (hh < 4.0) { rgb = vec3f(0.0, x, c); }
    else if (hh < 5.0) { rgb = vec3f(x, 0.0, c); }
    else                { rgb = vec3f(c, 0.0, x); }
    return rgb + vec3f(m);
}

@vertex
fn vs_main(@builtin(vertex_index) vi: u32,
           @builtin(instance_index) ii: u32) -> VertexOutput {
    // Generate quad from vertex index (6 vertices = 2 triangles)
    //   0--1    Tri 0: 0,1,2   Tri 1: 2,1,3
    //   |\ |
    //   2--3
    var corner: vec2f;
    switch vi {
        case 0u: { corner = vec2f(0.0, 0.0); }
        case 1u: { corner = vec2f(1.0, 0.0); }
        case 2u: { corner = vec2f(0.0, 1.0); }
        case 3u: { corner = vec2f(0.0, 1.0); }
        case 4u: { corner = vec2f(1.0, 0.0); }
        default: { corner = vec2f(1.0, 1.0); }
    }

    let inst = instances[ii];

    // Aspect correction: fit source texture aspect into quad
    let tex_dims = textureDimensions(input_tex);
    let tex_aspect = f32(tex_dims.x) / f32(tex_dims.y);
    let screen_aspect = uniforms.resolution.x / uniforms.resolution.y;

    // Quad size in NDC — size param is fraction of screen height
    var qw = uniforms.size;
    var qh = uniforms.size;

    // Adjust quad to match source texture aspect ratio
    if (tex_aspect > 1.0) {
        qh = qw / tex_aspect;
    } else {
        qw = qh * tex_aspect;
    }

    // Correct for screen aspect ratio (NDC is -1..1 on both axes)
    qw = qw / screen_aspect;

    // Instance center in NDC: map UV (0-1) to NDC (-1..1)
    let cx = inst.pos.x * 2.0 - 1.0;
    let cy = 1.0 - inst.pos.y * 2.0;  // flip Y

    // Position the corner
    let px = cx + (corner.x - 0.5) * qw * 2.0;
    let py = cy + (0.5 - corner.y) * qh * 2.0;

    var out: VertexOutput;
    out.position = vec4f(px, py, 0.0, 1.0);
    out.uv = corner;
    out.value = inst.value;
    out.instance_id = f32(ii);
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let tex_color = textureSample(input_tex, tex_sampler, input.uv);

    // Modulate by instance value (brightness)
    var rgb = tex_color.rgb * input.value;

    // Optional hue shift per instance
    if (uniforms.hue_spread > 0.001) {
        let count = max(uniforms.instance_count, 1.0);
        let hue_offset = (input.instance_id / count) * uniforms.hue_spread;
        // Convert to HSV-ish tint: rotate hue by applying a color shift
        let tint = hsv2rgb(fract(hue_offset), 1.0, 1.0);
        rgb = rgb * tint;
    }

    let a = tex_color.a * input.value;
    // Premultiplied alpha output
    return vec4f(rgb * (a / max(input.value, 0.001)), a);
}
)";

// =============================================================================
// Per-instance data (matches WGSL struct, 16 bytes, vec4-aligned)
// =============================================================================

struct InstanceData {
    float x, y;     // center position in UV space (0-1)
    float value;    // brightness from Spread
    float _pad;
};

// =============================================================================
// Uniform struct matching WGSL (32 bytes)
// =============================================================================

struct InstanceUniforms {
    float resolution[2];
    float instance_count;
    float size;
    float hue_spread;
    float _pad0;
    float _pad1;
    float _pad2;
};

// =============================================================================
// Instance Operator
// =============================================================================

struct Instance : vivid::OperatorBase {
    static constexpr const char* kName   = "Instance";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   layout     {"layout",     0, {"grid", "circle", "random"}};
    vivid::Param<float> size       {"size",       0.15f, 0.01f, 1.0f};
    vivid::Param<float> hue_spread {"hue_spread", 0.0f,  0.0f,  1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&layout);
        out.push_back(&size);
        out.push_back(&hue_spread);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",     VIVID_PORT_GPU_TEXTURE,    VIVID_PORT_INPUT});
        out.push_back({"values",    VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"positions", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"texture",   VIVID_PORT_GPU_TEXTURE,    VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[instance] lazy_init FAILED\n");
                return;
            }
        }

        // Get input texture
        WGPUTextureView input_tex = nullptr;
        if (gpu->input_texture_views && gpu->input_texture_count >= 1)
            input_tex = gpu->input_texture_views[0];
        if (!input_tex && !fallback_view_) create_fallback(gpu);
        if (!input_tex) input_tex = fallback_view_;

        // Read values Spread (input port index 1: input=0, values=1, positions=2)
        uint32_t count = 0;
        const float* values_data = nullptr;
        if (ctx->input_spreads && ctx->input_spreads[1].length > 0) {
            count = ctx->input_spreads[1].length;
            values_data = ctx->input_spreads[1].data;
        }



        // Read positions Spread (optional, input port index 2)
        uint32_t pos_count = 0;
        const float* pos_data = nullptr;
        if (ctx->input_spreads && ctx->input_spreads[2].length > 0) {
            pos_count = ctx->input_spreads[2].length;
            pos_data = ctx->input_spreads[2].data;
        }

        if (count == 0) count = 1;  // at least 1 instance

        // Build per-instance data
        std::vector<InstanceData> instances(count);
        int layout_mode = layout.int_value();

        // Positions: use custom if provided, otherwise compute from layout
        bool use_custom_positions = (pos_data && pos_count >= count * 2);
        if (use_custom_positions) {
            for (uint32_t i = 0; i < count; i++) {
                instances[i].x = pos_data[i * 2];
                instances[i].y = pos_data[i * 2 + 1];
            }
        } else {
            switch (layout_mode) {
                case 1: { // circle
                    for (uint32_t i = 0; i < count; i++) {
                        float angle = static_cast<float>(i) / static_cast<float>(count) * 6.28318530718f;
                        instances[i].x = 0.5f + 0.35f * std::cos(angle);
                        instances[i].y = 0.5f + 0.35f * std::sin(angle);
                    }
                    break;
                }
                case 2: { // random (deterministic hash)
                    for (uint32_t i = 0; i < count; i++) {
                        float p1 = static_cast<float>(i) * 127.1f;
                        float p2 = static_cast<float>(i) * 311.7f;
                        auto hash = [](float p) -> float {
                            float x = std::fmod(p * 0.1031f, 1.0f);
                            if (x < 0) x += 1.0f;
                            x += x * (x + 33.33f);
                            return std::fmod(x * (x + x), 1.0f);
                        };
                        instances[i].x = hash(p1);
                        instances[i].y = hash(p2);
                    }
                    break;
                }
                default: { // grid
                    uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(count))));
                    uint32_t rows = (count + cols - 1) / cols;
                    for (uint32_t i = 0; i < count; i++) {
                        uint32_t col = i % cols;
                        uint32_t row = i / cols;
                        instances[i].x = (static_cast<float>(col) + 0.5f) / static_cast<float>(cols);
                        instances[i].y = (static_cast<float>(row) + 0.5f) / static_cast<float>(rows);
                    }
                    break;
                }
            }
        }

        // Fill values
        for (uint32_t i = 0; i < count; i++) {
            instances[i].value = (values_data && i < (ctx->input_spreads[1].length))
                                 ? values_data[i] : 1.0f;
            instances[i]._pad = 0.0f;
        }

        // Rebuild storage buffer if instance count changed
        if (count != current_count_) {
            rebuild_storage(gpu, count);
        }

        // Upload instance data
        if (storage_buf_) {
            wgpuQueueWriteBuffer(gpu->queue, storage_buf_, 0,
                                 instances.data(), count * sizeof(InstanceData));
        }

        // Update uniforms
        InstanceUniforms u{};
        u.resolution[0] = static_cast<float>(gpu->output_width);
        u.resolution[1] = static_cast<float>(gpu->output_height);
        u.instance_count = static_cast<float>(count);
        u.size           = size.value;
        u.hue_spread     = hue_spread.value;

        wgpuQueueWriteBuffer(gpu->queue, uniform_buf_, 0, &u, sizeof(u));

        // Recreate bind group 0 when input texture changes
        if (input_tex != cached_input_tex_) {
            vivid::gpu::release(bind_group0_);

            WGPUBindGroupEntry bg0_entries[3]{};
            bg0_entries[0].binding = 0;
            bg0_entries[0].buffer  = uniform_buf_;
            bg0_entries[0].offset  = 0;
            bg0_entries[0].size    = sizeof(InstanceUniforms);
            bg0_entries[1].binding = 1;
            bg0_entries[1].sampler = sampler_;
            bg0_entries[2].binding = 2;
            bg0_entries[2].textureView = input_tex;

            WGPUBindGroupDescriptor bg0_desc{};
            bg0_desc.label = vivid_sv("Instance BG0");
            bg0_desc.layout = bind_layout0_;
            bg0_desc.entryCount = 3;
            bg0_desc.entries = bg0_entries;
            bind_group0_ = wgpuDeviceCreateBindGroup(gpu->device, &bg0_desc);
            cached_input_tex_ = input_tex;
        }

        // Render pass with alpha blending
        WGPURenderPassColorAttachment color_att{};
        color_att.view = gpu->output_texture_view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp  = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = { 0.0, 0.0, 0.0, 0.0 };  // transparent black

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv("Instance Pass");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            gpu->command_encoder, &rp_desc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group0_, 0, nullptr);
        if (storage_bind_group_)
            wgpuRenderPassEncoderSetBindGroup(pass, 1, storage_bind_group_, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 6, count, 0, 0);  // 6 verts per quad, N instances
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    ~Instance() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group0_);
        vivid::gpu::release(storage_bind_group_);
        vivid::gpu::release(bind_layout0_);
        vivid::gpu::release(bind_layout1_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(storage_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    WGPURenderPipeline  pipeline_          = nullptr;
    WGPUBindGroup       bind_group0_       = nullptr;   // uniforms + sampler + texture
    WGPUBindGroup       storage_bind_group_ = nullptr;  // instance data
    WGPUBindGroupLayout bind_layout0_      = nullptr;
    WGPUBindGroupLayout bind_layout1_      = nullptr;
    WGPUBuffer          uniform_buf_       = nullptr;
    WGPUBuffer          storage_buf_       = nullptr;
    WGPUShaderModule    shader_            = nullptr;
    WGPUPipelineLayout  pipe_layout_       = nullptr;
    WGPUSampler         sampler_           = nullptr;
    WGPUTexture         fallback_tex_      = nullptr;
    WGPUTextureView     fallback_view_     = nullptr;
    WGPUTextureView     cached_input_tex_  = nullptr;
    uint32_t            current_count_     = 0;

    void create_fallback(VividGpuState* gpu) {
        WGPUTextureDescriptor td{};
        td.label = vivid_sv("Instance Fallback");
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

        // Initialize to white (so instances are visible even without input)
        const uint8_t white[4] = {255, 255, 255, 255};
        WGPUTexelCopyTextureInfo dest_info{};
        dest_info.texture = fallback_tex_;
        dest_info.mipLevel = 0;
        dest_info.origin = {0, 0, 0};
        dest_info.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &dest_info, white, sizeof(white), &layout, &extent);
    }

    void rebuild_storage(VividGpuState* gpu, uint32_t count) {
        vivid::gpu::release(storage_buf_);
        vivid::gpu::release(storage_bind_group_);
        current_count_ = count;

        if (count == 0) return;

        uint32_t buf_size = count * sizeof(InstanceData);
        if (buf_size < 16) buf_size = 16;  // minimum one InstanceData

        WGPUBufferDescriptor buf_desc{};
        buf_desc.label = vivid_sv("Instance Storage");
        buf_desc.size  = buf_size;
        buf_desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        storage_buf_ = wgpuDeviceCreateBuffer(gpu->device, &buf_desc);

        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer  = storage_buf_;
        bg_entry.offset  = 0;
        bg_entry.size    = buf_size;

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label      = vivid_sv("Instance Storage BG");
        bg_desc.layout     = bind_layout1_;
        bg_desc.entryCount = 1;
        bg_desc.entries    = &bg_entry;
        storage_bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);
    }

    bool lazy_init(VividGpuState* gpu) {
        // Build shader — no fullscreen vertex preamble needed, we have our own vertex shader.
        // But create_shader() always prepends FULLSCREEN_VERTEX_WGSL. We'll build the
        // shader module manually to avoid that.
        std::string wgsl = std::string(vivid::gpu::WGSL_CONSTANTS) + kInstanceShader;

        WGPUShaderSourceWGSL wgsl_src{};
        wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl_src.code = vivid_sv(wgsl.c_str());

        WGPUShaderModuleDescriptor sm_desc{};
        sm_desc.nextInChain = &wgsl_src.chain;
        sm_desc.label = vivid_sv("Instance Shader");
        shader_ = wgpuDeviceCreateShaderModule(gpu->device, &sm_desc);
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(InstanceUniforms), "Instance Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Instance Sampler");

        // --- Bind group layout 0: uniforms + sampler + texture ---
        WGPUBindGroupLayoutEntry bg0_entries[3]{};
        bg0_entries[0].binding = 0;
        bg0_entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bg0_entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        bg0_entries[0].buffer.minBindingSize = sizeof(InstanceUniforms);

        bg0_entries[1].binding = 1;
        bg0_entries[1].visibility = WGPUShaderStage_Fragment;
        bg0_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        bg0_entries[2].binding = 2;
        bg0_entries[2].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bg0_entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        bg0_entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
        bg0_entries[2].texture.multisampled = false;

        WGPUBindGroupLayoutDescriptor bg0_layout_desc{};
        bg0_layout_desc.label = vivid_sv("Instance BGL0");
        bg0_layout_desc.entryCount = 3;
        bg0_layout_desc.entries = bg0_entries;
        bind_layout0_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bg0_layout_desc);

        // --- Bind group layout 1: storage buffer ---
        WGPUBindGroupLayoutEntry bg1_entry{};
        bg1_entry.binding = 0;
        bg1_entry.visibility = WGPUShaderStage_Vertex;
        bg1_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        bg1_entry.buffer.minBindingSize = 0;

        WGPUBindGroupLayoutDescriptor bg1_layout_desc{};
        bg1_layout_desc.label = vivid_sv("Instance BGL1");
        bg1_layout_desc.entryCount = 1;
        bg1_layout_desc.entries = &bg1_entry;
        bind_layout1_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bg1_layout_desc);

        // --- Pipeline layout ---
        WGPUBindGroupLayout layouts[2] = { bind_layout0_, bind_layout1_ };
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Instance Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 2;
        pl_desc.bindGroupLayouts = layouts;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // --- Render pipeline with alpha blending ---
        WGPUBlendState blend{};
        blend.color.srcFactor = WGPUBlendFactor_One;
        blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.alpha.operation = WGPUBlendOperation_Add;

        WGPUColorTargetState color_target{};
        color_target.format = gpu->output_format;
        color_target.blend = &blend;
        color_target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = shader_;
        fragment.entryPoint = vivid_sv("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &color_target;

        WGPURenderPipelineDescriptor rp_desc{};
        rp_desc.label = vivid_sv("Instance Pipeline");
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

        // Initial storage buffer (1 instance)
        rebuild_storage(gpu, 1);

        return true;
    }
};

VIVID_REGISTER(Instance)
