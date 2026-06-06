#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>

// Value-API GPU example operator (lane-value clean-break, Phase 4c).
//
// Obtains its output render target via the value API (ctx->value_outputs[0].resize
// → the runtime texture) instead of ctx->output_texture_view, and reads its texture
// input via the value API (ctx->values[0], VIVID_VALUE_TEXTURE). Fills the output:
// GREEN when a texture input is present (proving the texture INPUT value view is
// wired), else the (r,g,b) param color. Exercises GPU texture I/O through the value
// API end-to-end.

static const char* kFillFragment = R"(
struct Uniforms { color: vec4f, };
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
fn fs_main(input: VertexOutput) -> @location(0) vec4f { return uniforms.color; }
)";

struct FillUniforms { float color[4]; };

struct GpuValueFillOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "GpuValueFillOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> r {"r", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> g {"g", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> b {"b", 0.0f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&r); out.push_back(&g); out.push_back(&b);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",      VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_ && !lazy_init(ctx)) return;

        // Read the texture INPUT via the value API.
        const bool has_input = ctx->values &&
                               ctx->values[0].value_type == VIVID_VALUE_TEXTURE &&
                               ctx->values[0].data != nullptr;

        FillUniforms u{};
        if (has_input) { u.color[0] = 0.0f; u.color[1] = 1.0f; u.color[2] = 0.0f; }
        else           { u.color[0] = r.value; u.color[1] = g.value; u.color[2] = b.value; }
        u.color[3] = 1.0f;
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        // Obtain the output render target via the value API (resize → the runtime
        // texture view); fall back to output_texture_view if unpopulated.
        WGPUTextureView target = ctx->output_texture_view;
        if (ctx->value_outputs && ctx->value_outputs[0].resize) {
            void* h = ctx->value_outputs[0].resize(ctx->value_outputs[0].handle, 1);
            if (h) target = static_cast<WGPUTextureView>(h);
        }

        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_, target,
                             "GpuValueFill Pass");

        if (ctx->value_outputs && ctx->value_outputs[0].commit)
            ctx->value_outputs[0].commit(ctx->value_outputs[0].handle, 1);
    }

    ~GpuValueFillOp() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

private:
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;

    bool lazy_init(const VividGpuContext* ctx) {
        shader_ = vivid::gpu::create_shader(ctx->device, kFillFragment, "GpuValueFill Shader");
        if (!shader_) return false;
        uniform_buf_ = vivid::gpu::create_uniform_buffer(ctx->device, sizeof(FillUniforms),
                                                         "GpuValueFill Uniforms");
        WGPUBindGroupLayoutEntry bgl_entry{};
        bgl_entry.binding = 0;
        bgl_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bgl_entry.buffer.type = WGPUBufferBindingType_Uniform;
        bgl_entry.buffer.minBindingSize = sizeof(FillUniforms);
        WGPUBindGroupLayoutDescriptor bgl_desc{};
        bgl_desc.label = vivid_sv("GpuValueFill BGL");
        bgl_desc.entryCount = 1; bgl_desc.entries = &bgl_entry;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bgl_desc);
        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("GpuValueFill Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1; pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(ctx->device, &pl_desc);
        WGPUBindGroupEntry bg_entry{};
        bg_entry.binding = 0; bg_entry.buffer = uniform_buf_;
        bg_entry.offset = 0; bg_entry.size = sizeof(FillUniforms);
        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("GpuValueFill Bind Group");
        bg_desc.layout = bind_layout_; bg_desc.entryCount = 1; bg_desc.entries = &bg_entry;
        bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg_desc);
        pipeline_ = vivid::gpu::create_pipeline(ctx->device, shader_, pipe_layout_,
                                                ctx->output_format, "GpuValueFill Pipeline");
        return pipeline_ != nullptr;
    }
};
