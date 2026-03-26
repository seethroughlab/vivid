#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <string>

// Minimal GPU test operator: fills the output texture with a solid color.

static const char* kFillFragment = R"(

struct Uniforms {
    color: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

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
    return uniforms.color;
}
)";

struct FillUniforms {
    float color[4];
};

struct GpuFillOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "GpuFillOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> r {"r", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> g {"g", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> b {"b", 0.0f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[gpu_fill_op] lazy_init FAILED\n");
                return;
            }
        }

        FillUniforms u{};
        u.color[0] = r.value;
        u.color[1] = g.value;
        u.color[2] = b.value;
        u.color[3] = 1.0f;
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "GpuFill Pass");
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!thumb_pipeline_ || thumb_pipeline_format_ != ctx->thumbnail_format) {
            if (!lazy_init_thumb(ctx)) {
                vivid_report_thumbnail_error(ctx, "gpu_fill thumbnail pipeline init failed");
                return;
            }
        }

        FillUniforms u{};
        u.color[0] = r.value;
        u.color[1] = g.value;
        u.color[2] = b.value;
        u.color[3] = 1.0f;
        wgpuQueueWriteBuffer(ctx->queue, thumb_uniform_buf_, 0, &u, sizeof(u));

        vivid::thumbnail::run_pass(ctx, thumb_pipeline_, thumb_bind_group_, "GpuFill Thumb Pass");
    }

    ~GpuFillOp() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);
    }

private:
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPURenderPipeline  thumb_pipeline_ = nullptr;
    WGPUBindGroup       thumb_bind_group_ = nullptr;
    WGPUBindGroupLayout thumb_bind_layout_ = nullptr;
    WGPUBuffer          thumb_uniform_buf_ = nullptr;
    WGPUShaderModule    thumb_shader_ = nullptr;
    WGPUPipelineLayout  thumb_pipe_layout_ = nullptr;
    WGPUTextureFormat   thumb_pipeline_format_ = WGPUTextureFormat_Undefined;

    bool lazy_init(const VividGpuContext* ctx) {
        shader_ = vivid::gpu::create_shader(ctx->device, kFillFragment, "GpuFill Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(ctx->device, sizeof(FillUniforms), "GpuFill Uniforms");

        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = sizeof(FillUniforms);

        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("GpuFill BGL");
        bgl_desc.entryCount = 1;
        bgl_desc.entries = &bgl_entry;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bgl_desc);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("GpuFill Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pl_desc);

        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0;
        bg_entry.buffer  = uniform_buf_;
        bg_entry.offset  = 0;
        bg_entry.size    = sizeof(FillUniforms);

        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("GpuFill Bind Group");
        bg_desc.layout = bind_layout_;
        bg_desc.entryCount = 1;
        bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);

        pipeline_ = vivid::gpu::create_pipeline(ctx->device, shader_, pipe_layout_, ctx->output_format, "GpuFill Pipeline");
        if (!pipeline_) return false;

        return true;
    }

    bool lazy_init_thumb(const VividThumbnailContext* ctx) {
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);

        thumb_shader_ = vivid::thumbnail::create_shader(ctx->device, kFillFragment, "GpuFill Thumb Shader");
        if (!thumb_shader_) return false;

        thumb_uniform_buf_ =
            vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(FillUniforms), "GpuFill Thumb Uniforms");
        thumb_bind_layout_ =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(FillUniforms), "GpuFill Thumb BGL");
        thumb_pipe_layout_ =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_bind_layout_, "GpuFill Thumb Layout");
        thumb_bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_bind_layout_, thumb_uniform_buf_, sizeof(FillUniforms), "GpuFill Thumb BG");
        thumb_pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_shader_, thumb_pipe_layout_, ctx->thumbnail_format, "GpuFill Thumb Pipeline");
        thumb_pipeline_format_ = ctx->thumbnail_format;
        return thumb_pipeline_ != nullptr;
    }
};

VIVID_REGISTER(GpuFillOp)
VIVID_THUMBNAIL(GpuFillOp)
