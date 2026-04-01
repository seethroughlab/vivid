#include "smooth.h"
#include "operator_api/thumbnail.h"

struct SmoothThumbState {
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroup bind_group = nullptr;
    WGPUBindGroupLayout bind_layout = nullptr;
    WGPUBuffer uniform_buf = nullptr;
    WGPUShaderModule shader = nullptr;
    WGPUPipelineLayout pipe_layout = nullptr;
    WGPUTextureFormat pipeline_format = WGPUTextureFormat_Undefined;

    void release_all() {
        vivid::gpu::release(pipeline);
        vivid::gpu::release(bind_group);
        vivid::gpu::release(bind_layout);
        vivid::gpu::release(uniform_buf);
        vivid::gpu::release(shader);
        vivid::gpu::release(pipe_layout);
    }
};

Smooth::~Smooth() {
    if (thumb_state_) {
        thumb_state_->release_all();
        delete thumb_state_;
    }
}

void Smooth::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx) return;
    if (!thumb_state_) thumb_state_ = new SmoothThumbState();

    if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
        rebuild_thumb_pipeline(ctx);
    }
    if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
        vivid_report_thumbnail_error(ctx, "smooth thumbnail pipeline init failed");
        return;
    }

    struct Uniforms { float pad[4]; } u{};
    wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, &u, sizeof(u));
    vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "Smooth Thumb Pass");
}

void Smooth::rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
    thumb_state_->release_all();

    static const char* kThumbFragment = R"(
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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let dim_col = vec4f(70.0/255.0, 75.0/255.0, 85.0/255.0, 150.0/255.0);
    let bright_col = vec4f(140.0/255.0, 190.0/255.0, 230.0/255.0, 230.0/255.0);

    // Stepped input signal: 3 segments at different heights
    let pad = 0.08;
    let plot_y = (uv.y - pad) / (1.0 - 2.0 * pad);
    let x = uv.x;

    // Step positions and heights (descending from top)
    var step_val = 0.2;
    if (x < 0.33) {
        step_val = 0.8;
    } else if (x < 0.66) {
        step_val = 0.35;
    } else {
        step_val = 0.65;
    }

    // Smooth signal: exponential transitions between steps
    let tau = 0.06;
    var smooth_val = 0.8;  // start at first step
    // Transition at x=0.33 from 0.8 to 0.35
    if (x > 0.33) {
        let t1 = (x - 0.33) / tau;
        smooth_val = 0.35 + (0.8 - 0.35) * exp(-t1);
    }
    // Transition at x=0.66 from 0.35 to 0.65
    if (x > 0.66) {
        let prev = 0.35 + (0.8 - 0.35) * exp(-(0.66 - 0.33) / tau);
        let t2 = (x - 0.66) / tau;
        smooth_val = 0.65 + (prev - 0.65) * exp(-t2);
    }

    let step_y = 1.0 - step_val;
    let smooth_y = 1.0 - smooth_val;

    // Draw stepped signal (dashed dim line)
    let step_dist = abs(plot_y - step_y);
    let dash = step(0.0, sin(x * 80.0));
    if (step_dist < 0.02 && dash > 0.5) {
        return dim_col;
    }

    // Draw smooth signal (bright line)
    let smooth_dist = abs(plot_y - smooth_y);
    if (smooth_dist < 0.025) {
        return bright_col;
    }

    return bg;
}
)";

    thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "Smooth Thumb Shader");
    thumb_state_->uniform_buf =
        vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Smooth Thumb Uniforms");
    thumb_state_->bind_layout =
        vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Smooth Thumb BGL");
    thumb_state_->pipe_layout =
        vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "Smooth Thumb Layout");
    thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(
        ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, sizeof(float) * 4, "Smooth Thumb BG");
    thumb_state_->pipeline = vivid::thumbnail::create_pipeline(
        ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "Smooth Thumb Pipeline");
    thumb_state_->pipeline_format = ctx->thumbnail_format;
}

// Legacy registration removed — use _fr/_au variants instead.
VIVID_THUMBNAIL(Smooth)
