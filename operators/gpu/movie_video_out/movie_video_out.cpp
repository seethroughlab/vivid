#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <cstdio>

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

struct MovieVideoOut : vivid::OperatorBase {
    static constexpr const char* kName = "MovieVideoOut";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"media_stream", VIVID_PORT_DATA, VIVID_PORT_INPUT, "media_stream_v1"});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[movie_video_out] lazy_init failed\n");
                return;
            }
        }

        WGPUTextureView src = nullptr;
        if (gpu->input_texture_views && gpu->input_texture_count >= 2) {
            src = gpu->input_texture_views[1];
        } else if (gpu->input_texture_views && gpu->input_texture_count >= 1) {
            src = gpu->input_texture_views[0];
        }
        if (!src) {
            vivid::gpu::run_pass(gpu->command_encoder, pipeline_, nullptr,
                                 gpu->output_texture_view, "MovieVideoOut Empty",
                                 WGPUColor{0, 0, 0, 1});
            return;
        }

        if (src != cached_view_) {
            vivid::gpu::release(bind_group_);
            WGPUBindGroupEntry e[2]{};
            e[0].binding = 0;
            e[0].sampler = sampler_;
            e[1].binding = 1;
            e[1].textureView = src;

            WGPUBindGroupDescriptor bg{};
            bg.label = vivid_sv("MovieVideoOut BG");
            bg.layout = bind_layout_;
            bg.entryCount = 2;
            bg.entries = e;
            bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &bg);
            cached_view_ = src;
        }

        vivid::gpu::run_pass(gpu->command_encoder, pipeline_, bind_group_,
                             gpu->output_texture_view, "MovieVideoOut Pass",
                             WGPUColor{0, 0, 0, 1});
    }

    ~MovieVideoOut() override {
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(sampler_);
    }

private:
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUSampler         sampler_     = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUTextureView     cached_view_ = nullptr;

    bool lazy_init(VividGpuState* gpu) {
        if (!gpu || !gpu->device) return false;

        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "MovieVideoOut Sampler");

        WGPUBindGroupLayoutEntry bgl[2]{};
        bgl[0].binding = 0;
        bgl[0].visibility = WGPUShaderStage_Fragment;
        bgl[0].sampler.type = WGPUSamplerBindingType_Filtering;

        bgl[1].binding = 1;
        bgl[1].visibility = WGPUShaderStage_Fragment;
        bgl[1].texture.sampleType = WGPUTextureSampleType_Float;
        bgl[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        bgl[1].texture.multisampled = false;

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("MovieVideoOut BGL");
        bgl_desc.entryCount = 2;
        bgl_desc.entries = bgl;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("MovieVideoOut PL");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        shader_ = vivid::gpu::create_shader(gpu->device, kBlitFragment, "MovieVideoOut Shader");
        if (!shader_) return false;

        WGPUColorTargetState color_target{};
        color_target.format = gpu->output_format;
        color_target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment{};
        fragment.module = shader_;
        fragment.entryPoint = vivid_sv("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &color_target;

        WGPURenderPipelineDescriptor rp{};
        rp.label = vivid_sv("MovieVideoOut Pipeline");
        rp.layout = pipe_layout_;
        rp.vertex.module = shader_;
        rp.vertex.entryPoint = vivid_sv("vs_main");
        rp.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        rp.primitive.frontFace = WGPUFrontFace_CCW;
        rp.primitive.cullMode = WGPUCullMode_None;
        rp.multisample.count = 1;
        rp.fragment = &fragment;

        pipeline_ = wgpuDeviceCreateRenderPipeline(gpu->device, &rp);
        return pipeline_ != nullptr;
    }
};

VIVID_REGISTER(MovieVideoOut)
