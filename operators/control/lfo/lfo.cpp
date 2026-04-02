#include "lfo.h"
#include "operator_api/thumbnail.h"

struct LfoThumbState {
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

LFO::~LFO() {
    if (thumb_state_) {
        thumb_state_->release_all();
        delete thumb_state_;
    }
}

void LFO::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx) return;
    if (!thumb_state_) thumb_state_ = new LfoThumbState();

    if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
        rebuild_thumb_pipeline(ctx);
    }
    if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
        vivid_report_thumbnail_error(ctx, "lfo thumbnail pipeline init failed");
        return;
    }

    // Pack uniforms: waveform, polarity, current_value, time
    struct Uniforms { float waveform; float polarity; float current_value; float time; } u{};
    u.waveform      = (ctx->param_count > 5) ? ctx->param_values[5] : 0.0f;
    u.polarity      = (ctx->param_count > 7) ? ctx->param_values[7] : 0.0f;
    u.current_value = (ctx->output_count > 0) ? ctx->output_values[0] : 0.0f;
    u.time          = static_cast<float>(ctx->time);

    wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, &u, sizeof(u));
    vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "LFO Thumb Pass");
}

void LFO::rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
    thumb_state_->release_all();

    static const char* kThumbShader = R"(
struct Uniforms {
    data: vec4f,   // x=waveform, y=polarity, z=current_value, w=time
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

fn lfo_wave(phase: f32, wf: i32) -> f32 {
    let p = fract(phase);
    switch (wf) {
        case 0: { return sin(p * 6.28318530718); }                         // sine
        case 1: { return 2.0 * p - 1.0; }                                  // saw
        case 2: { return select(-1.0, 1.0, p < 0.5); }                     // square
        case 3: { return 4.0 * select(1.0 - p, p, p < 0.5) - 1.0; }       // triangle
        default: { return sin(p * 6.28318530718); }                         // fallback for random modes
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let wf = i32(uniforms.data.x + 0.5);
    let polarity = uniforms.data.y;
    let current_val = uniforms.data.z;
    let time = uniforms.data.w;

    let bg     = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let fill   = vec4f(80.0/255.0, 130.0/255.0, 190.0/255.0, 100.0/255.0);
    let line   = vec4f(160.0/255.0, 200.0/255.0, 240.0/255.0, 240.0/255.0);
    let marker = vec4f(255.0/255.0, 200.0/255.0, 80.0/255.0, 200.0/255.0);

    // Padding
    let pad = 0.08;
    let plot_x = (uv.x - pad) / (1.0 - 2.0 * pad);  // 0..1 across plot area
    let plot_y = (uv.y - pad) / (1.0 - 2.0 * pad);   // 0..1 top to bottom

    // Outside plot area: background
    if (plot_x < 0.0 || plot_x > 1.0 || plot_y < 0.0 || plot_y > 1.0) {
        return bg;
    }

    // Compute waveform value at this X position (2 cycles)
    let phase = plot_x * 2.0;
    let raw = lfo_wave(phase, wf);

    // Apply polarity: bipolar raw is -1..1, unipolar maps to 0..1
    var wave_val: f32;
    if (polarity > 0.5) {
        wave_val = raw * 0.5 + 0.5;   // unipolar: 0..1
    } else {
        wave_val = raw * 0.5 + 0.5;   // map -1..1 to 0..1 for display
    }

    // wave_val is 0..1 where 1 = top, but plot_y 0 = top
    // so curve_y in plot space = 1 - wave_val
    let curve_y = 1.0 - wave_val;

    // Center line for bipolar mode
    var col = bg;
    if (polarity < 0.5) {
        let center_dist = abs(plot_y - 0.5);
        if (center_dist < 0.004) {
            col = vec4f(1.0, 1.0, 1.0, 0.12);
        }
    }

    // Fill below curve
    if (polarity < 0.5) {
        // Bipolar: fill between center (0.5) and curve
        let top = min(curve_y, 0.5);
        let bot = max(curve_y, 0.5);
        if (plot_y >= top && plot_y <= bot) {
            col = fill;
        }
    } else {
        // Unipolar: fill between curve and bottom
        if (plot_y >= curve_y) {
            col = fill;
        }
    }

    // Curve line
    let dist = abs(plot_y - curve_y);
    if (dist < 0.025) {
        let t = 1.0 - dist / 0.025;
        col = mix(col, line, t * t);
    }

    // Playhead: horizontal line at current output value
    var marker_y: f32;
    if (polarity > 0.5) {
        marker_y = 1.0 - clamp(current_val, 0.0, 1.0);
    } else {
        marker_y = 1.0 - (clamp(current_val, -1.0, 1.0) * 0.5 + 0.5);
    }
    let m_dist = abs(plot_y - marker_y);
    if (m_dist < 0.015) {
        let mt = 1.0 - m_dist / 0.015;
        col = mix(col, marker, mt * mt * 0.7);
    }

    return col;
}
)";

    constexpr size_t kUniformSize = sizeof(float) * 4;
    thumb_state_->shader      = vivid::thumbnail::create_shader(ctx->device, kThumbShader, "LFO Thumb Shader");
    thumb_state_->uniform_buf = vivid::thumbnail::create_uniform_buffer(ctx->device, kUniformSize, "LFO Thumb Uniforms");
    thumb_state_->bind_layout = vivid::thumbnail::create_uniform_bind_layout(ctx->device, kUniformSize, "LFO Thumb BGL");
    thumb_state_->pipe_layout = vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "LFO Thumb Layout");
    thumb_state_->bind_group  = vivid::thumbnail::create_uniform_bind_group(
        ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, kUniformSize, "LFO Thumb BG");
    thumb_state_->pipeline    = vivid::thumbnail::create_pipeline(
        ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "LFO Thumb Pipeline");
    thumb_state_->pipeline_format = ctx->thumbnail_format;
}
