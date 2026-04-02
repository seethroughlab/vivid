#include "clock_core.h"
#include "operator_api/thumbnail.h"

struct ClockThumbState {
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

ClockCore::~ClockCore() {
    if (thumb_state_) {
        thumb_state_->release_all();
        delete thumb_state_;
    }
}

void ClockCore::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx) return;
    if (!thumb_state_) thumb_state_ = new ClockThumbState();

    if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
        rebuild_thumb_pipeline(ctx);
    }
    if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
        vivid_report_thumbnail_error(ctx, "clock thumbnail pipeline init failed");
        return;
    }

    struct Uniforms { float phase; float pad[3]; } u{};
    u.phase = (ctx->output_count > 0) ? ctx->output_values[0] : 0.0f;
    wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, &u, sizeof(u));
    vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "Clock Thumb Pass");
}

void ClockCore::rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
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
    let p = input.uv * 2.0 - vec2f(1.0, 1.0);
    let aspect = 140.0 / 88.0;
    let corrected = vec2f(p.x * aspect, p.y);
    let dist = length(corrected);
    let radius = 0.82;
    if (dist > radius + 0.03) {
        return vec4f(0.0, 0.0, 0.0, 0.0);
    }

    let face = vec4f(18.0 / 255.0, 20.0 / 255.0, 23.0 / 255.0, 230.0 / 255.0);
    let fill = vec4f(100.0 / 255.0, 130.0 / 255.0, 170.0 / 255.0, 200.0 / 255.0);
    let rim = vec4f(192.0 / 255.0, 200.0 / 255.0, 208.0 / 255.0, 180.0 / 255.0);

    if (dist > radius - 0.03) {
        return rim;
    }

    var angle = atan2(corrected.x, -corrected.y);
    if (angle < 0.0) {
        angle = angle + 6.28318530718;
    }
    let phase_angle = clamp(uniforms.data.x, 0.0, 1.0) * 6.28318530718;
    return select(face, fill, angle <= phase_angle);
}
)";

    thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "Clock Thumb Shader");
    thumb_state_->uniform_buf =
        vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Clock Thumb Uniforms");
    thumb_state_->bind_layout =
        vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Clock Thumb BGL");
    thumb_state_->pipe_layout =
        vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "Clock Thumb Layout");
    thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(
        ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, sizeof(float) * 4, "Clock Thumb BG");
    thumb_state_->pipeline = vivid::thumbnail::create_pipeline(
        ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "Clock Thumb Pipeline");
    thumb_state_->pipeline_format = ctx->thumbnail_format;
}

// Thumbnail entry point is exported by each _fr/_au wrapper via VIVID_THUMBNAIL.
