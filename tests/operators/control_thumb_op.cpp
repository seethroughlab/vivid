#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"

static const char* kControlThumbFragment = R"(
struct Uniforms {
    data: vec4f,
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

fn line_dist(p: vec2f, a: vec2f, b: vec2f) -> f32 {
    let pa = p - a;
    let ba = b - a;
    let h = clamp(dot(pa, ba) / max(dot(ba, ba), 0.0001), 0.0, 1.0);
    return length(pa - ba * h);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let bg = vec4f(18.0 / 255.0, 20.0 / 255.0, 23.0 / 255.0, 230.0 / 255.0);
    let dim = vec4f(60.0 / 255.0, 130.0 / 255.0, 160.0 / 255.0, 160.0 / 255.0);
    let bright = vec4f(100.0 / 255.0, 190.0 / 255.0, 200.0 / 255.0, 220.0 / 255.0);

    let bands = array<f32, 3>(0.72, 0.28, 0.56);
    let center_y = clamp(0.8 - uniforms.data.x * 0.6, 0.12, 0.88);
    var color = bg;

    for (var i: i32 = 0; i < 3; i = i + 1) {
        let a = vec2f(0.08, bands[i]);
        let b = vec2f(0.92, center_y);
        if (line_dist(uv, a, b) < 0.018) {
            color = dim;
        }
    }

    let active_curve = vec2f(uv.x, 0.82 - uv.x * 0.5);
    if (abs(uv.y - active_curve.y) < 0.025) {
        color = bright;
    }
    return color;
}
)";

struct ControlThumbOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "ControlThumbOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> value{"value", 0.5f, 0.0f, 1.0f};

    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroup bind_group_ = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUBuffer uniform_buf_ = nullptr;
    WGPUShaderModule shader_ = nullptr;
    WGPUPipelineLayout pipe_layout_ = nullptr;
    WGPUTextureFormat pipeline_format_ = WGPUTextureFormat_Undefined;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&value);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = value.value;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!pipeline_ || pipeline_format_ != ctx->thumbnail_format) {
            if (!lazy_init(ctx)) {
                vivid_report_thumbnail_error(ctx, "control thumbnail pipeline init failed");
                return;
            }
        }
        float uniform_data[4] = {value.value, 0.0f, 0.0f, 0.0f};
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, uniform_data, sizeof(uniform_data));
        vivid::thumbnail::run_pass(ctx, pipeline_, bind_group_, "Control Thumb Pass");
    }

    ~ControlThumbOp() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);
    }

    bool lazy_init(const VividThumbnailContext* ctx) {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(pipe_layout_);

        shader_ = vivid::thumbnail::create_shader(ctx->device, kControlThumbFragment, "Control Thumb Shader");
        if (!shader_) return false;
        uniform_buf_ = vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Control Thumb UBO");
        bind_layout_ = vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Control Thumb BGL");
        pipe_layout_ = vivid::thumbnail::create_pipeline_layout(ctx->device, bind_layout_, "Control Thumb Layout");
        bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, bind_layout_, uniform_buf_, sizeof(float) * 4, "Control Thumb BG");
        pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, shader_, pipe_layout_, ctx->thumbnail_format, "Control Thumb Pipeline");
        pipeline_format_ = ctx->thumbnail_format;
        return pipeline_ != nullptr;
    }
};

VIVID_REGISTER(ControlThumbOp)
VIVID_THUMBNAIL(ControlThumbOp)
