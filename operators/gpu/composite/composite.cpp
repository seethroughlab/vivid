#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <string>

// =============================================================================
// Composite WGSL Fragment Shader
// =============================================================================

static const char* kCompositeFragment = R"(

struct Uniforms {
    blend_mode: i32,
    opacity: f32,
    _pad0: f32,
    _pad1: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var texA: texture_2d<f32>;
@group(0) @binding(3) var texB: texture_2d<f32>;

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
    let A = textureSample(texA, texSampler, input.uv);
    let B = textureSample(texB, texSampler, input.uv);
    let op = uniforms.opacity;

    var result: vec4f;

    switch uniforms.blend_mode {
        case 1: {
            // Add
            result = vec4f(B.rgb + A.rgb * op, max(A.a, B.a));
        }
        case 2: {
            // Multiply
            result = vec4f(B.rgb * mix(vec3f(1.0), A.rgb, op), max(A.a, B.a));
        }
        case 3: {
            // Screen
            let one = vec3f(1.0);
            result = vec4f(one - (one - B.rgb) * (one - A.rgb * op), max(A.a, B.a));
        }
        case 4: {
            // Overlay
            let lo = 2.0 * A.rgb * B.rgb;
            let hi = vec3f(1.0) - 2.0 * (vec3f(1.0) - A.rgb) * (vec3f(1.0) - B.rgb);
            let ov = select(hi, lo, B.rgb < vec3f(0.5));
            result = vec4f(mix(B.rgb, ov, op), max(A.a, B.a));
        }
        default: {
            // Normal (0)
            result = vec4f(mix(B.rgb, A.rgb, A.a * op), mix(B.a, 1.0, A.a * op));
        }
    }

    return result;
}
)";

// =============================================================================
// Uniform struct matching WGSL
// =============================================================================

struct CompositeUniforms {
    int   blend_mode;
    float opacity;
    float _pad0;
    float _pad1;
};

// =============================================================================
// Composite Operator
// =============================================================================

struct Composite : vivid::OperatorBase {
    static constexpr const char* kName   = "Composite";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   blend_mode {"blend_mode", 0, {"Normal", "Add", "Multiply", "Screen", "Overlay"}};
    vivid::Param<float> opacity    {"opacity", 1.0f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&blend_mode);
        out.push_back(&opacity);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"b", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[composite] lazy_init FAILED\n");
                return;
            }
        }

        // Get input texture views
        WGPUTextureView tex_a = nullptr;
        WGPUTextureView tex_b = nullptr;
        if (gpu->input_texture_views && gpu->input_texture_count >= 1)
            tex_a = gpu->input_texture_views[0];
        if (gpu->input_texture_views && gpu->input_texture_count >= 2)
            tex_b = gpu->input_texture_views[1];

        // Create fallback 1x1 transparent texture if needed
        if (!tex_a && !fallback_view_) create_fallback(gpu);
        if (!tex_b && !fallback_view_) create_fallback(gpu);
        if (!tex_a) tex_a = fallback_view_;
        if (!tex_b) tex_b = fallback_view_;

        // Update uniforms
        CompositeUniforms u{};
        u.blend_mode = blend_mode.int_value();
        u.opacity    = opacity.value;
        wgpuQueueWriteBuffer(gpu->queue, uniform_buf_, 0, &u, sizeof(u));

        // Recreate bind group only when texture inputs change
        if (tex_a != cached_tex_a_ || tex_b != cached_tex_b_) {
            if (cached_bind_group_)
                wgpuBindGroupRelease(cached_bind_group_);

            WGPUBindGroupEntry bg_entries[4]{};
            bg_entries[0].binding = 0;
            bg_entries[0].buffer  = uniform_buf_;
            bg_entries[0].offset  = 0;
            bg_entries[0].size    = sizeof(CompositeUniforms);
            bg_entries[1].binding = 1;
            bg_entries[1].sampler = sampler_;
            bg_entries[2].binding = 2;
            bg_entries[2].textureView = tex_a;
            bg_entries[3].binding = 3;
            bg_entries[3].textureView = tex_b;

            WGPUBindGroupDescriptor bg_desc{};
            bg_desc.label = vivid_sv("Composite BG");
            bg_desc.layout = bind_layout_;
            bg_desc.entryCount = 4;
            bg_desc.entries = bg_entries;
            cached_bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);
            cached_tex_a_ = tex_a;
            cached_tex_b_ = tex_b;
        }

        // Render pass
        WGPURenderPassColorAttachment color_att{};
        color_att.view = gpu->output_texture_view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp  = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = { 0.0, 0.0, 0.0, 0.0 };

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.label = vivid_sv("Composite Pass");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            gpu->command_encoder, &rp_desc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, cached_bind_group_, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    ~Composite() override {
        vivid::gpu::release(cached_bind_group_);
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    WGPURenderPipeline  pipeline_      = nullptr;
    WGPUBindGroupLayout bind_layout_   = nullptr;
    WGPUBuffer          uniform_buf_   = nullptr;
    WGPUShaderModule    shader_        = nullptr;
    WGPUPipelineLayout  pipe_layout_   = nullptr;
    WGPUSampler         sampler_       = nullptr;
    WGPUTexture         fallback_tex_  = nullptr;
    WGPUTextureView     fallback_view_ = nullptr;
    WGPUBindGroup       cached_bind_group_ = nullptr;
    WGPUTextureView     cached_tex_a_  = nullptr;
    WGPUTextureView     cached_tex_b_  = nullptr;

    void create_fallback(VividGpuState* gpu) {
        WGPUTextureDescriptor td{};
        td.label = vivid_sv("Composite Fallback");
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

        // Initialize to transparent black
        const uint8_t zero[4] = {0, 0, 0, 0};
        WGPUTexelCopyTextureInfo dest_info{};
        dest_info.texture = fallback_tex_;
        dest_info.mipLevel = 0;
        dest_info.origin = {0, 0, 0};
        dest_info.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &dest_info, zero, sizeof(zero), &layout, &extent);
    }

    bool lazy_init(VividGpuState* gpu) {
        // Shader module
        std::string shader_src = std::string(vivid::gpu::FULLSCREEN_VERTEX_WGSL) + kCompositeFragment;
        WGPUShaderSourceWGSL wgsl_src{};
        wgsl_src.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl_src.code = vivid_sv(shader_src.c_str());

        WGPUShaderModuleDescriptor shader_desc{};
        shader_desc.nextInChain = &wgsl_src.chain;
        shader_desc.label = vivid_sv("Composite Shader");
        shader_ = wgpuDeviceCreateShaderModule(gpu->device, &shader_desc);
        if (!shader_) return false;

        // Uniform buffer
        WGPUBufferDescriptor buf_desc{};
        buf_desc.label = vivid_sv("Composite Uniforms");
        buf_desc.size  = sizeof(CompositeUniforms);
        buf_desc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        uniform_buf_ = wgpuDeviceCreateBuffer(gpu->device, &buf_desc);

        // Sampler
        WGPUSamplerDescriptor sampler_desc{};
        sampler_desc.label = vivid_sv("Composite Sampler");
        sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
        sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
        sampler_desc.addressModeW = WGPUAddressMode_ClampToEdge;
        sampler_desc.magFilter = WGPUFilterMode_Linear;
        sampler_desc.minFilter = WGPUFilterMode_Linear;
        sampler_desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        sampler_desc.maxAnisotropy = 1;
        sampler_ = wgpuDeviceCreateSampler(gpu->device, &sampler_desc);

        // Bind group layout: uniform(0) + sampler(1) + textureA(2) + textureB(3)
        WGPUBindGroupLayoutEntry entries[4]{};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = sizeof(CompositeUniforms);

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[2].texture.multisampled = false;

        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Fragment;
        entries[3].texture.sampleType = WGPUTextureSampleType_Float;
        entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[3].texture.multisampled = false;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("Composite BGL");
        bgl_desc.entryCount = 4;
        bgl_desc.entries = entries;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Composite Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        // Render pipeline
        WGPUColorTargetState color_target{};
        color_target.format = gpu->output_format;
        color_target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = shader_;
        fragment.entryPoint = vivid_sv("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &color_target;

        WGPURenderPipelineDescriptor rp_desc{};
        rp_desc.label = vivid_sv("Composite Pipeline");
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

VIVID_REGISTER(Composite)
