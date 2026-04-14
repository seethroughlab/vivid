#include "mseg.h"
#include "operator_api/thumbnail.h"

struct MsegThumbState {
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

MSEG::~MSEG() {
    if (thumb_state_) {
        thumb_state_->release_all();
        delete thumb_state_;
    }
}

void MSEG::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx) return;
    if (!thumb_state_) thumb_state_ = new MsegThumbState();

    if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
        rebuild_thumb_pipeline(ctx);
    }
    if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
        vivid_report_thumbnail_error(ctx, "mseg thumbnail pipeline init failed");
        return;
    }

    // Uniform layout: 14 vec4f = 56 floats
    // [0]: num_points, current_value, loop_enabled, loop_start
    // [1]: loop_end, pad, pad, pad
    // [2..5]: pt_time[16]
    // [6..9]: pt_value[16]
    // [10..13]: pt_curve[16] (15 + 1 pad)
    float u[56] = {};
    u[0] = ctx->param_values[0]; // num_points
    float amp = (ctx->param_count > 5) ? ctx->param_values[5] : 1.0f;
    float raw_out = (ctx->output_count > 0) ? ctx->output_values[0] : 0.0f;
    u[1] = (amp > 0.0001f) ? raw_out / amp : 0.0f; // normalized current value
    u[2] = (ctx->param_count > 2) ? ctx->param_values[2] : 0.0f; // loop_enabled
    u[3] = (ctx->param_count > 3) ? ctx->param_values[3] : 0.0f; // loop_start

    u[4] = (ctx->param_count > 4) ? ctx->param_values[4] : 3.0f; // loop_end

    // pt_time[16] at param indices 6..21 → uniform offset 8
    for (int i = 0; i < 16; ++i)
        u[8 + i] = (ctx->param_count > static_cast<uint32_t>(6 + i)) ? ctx->param_values[6 + i] : 0.0f;
    // pt_value[16] at param indices 22..37 → uniform offset 24
    for (int i = 0; i < 16; ++i)
        u[24 + i] = (ctx->param_count > static_cast<uint32_t>(22 + i)) ? ctx->param_values[22 + i] : 0.0f;
    // pt_curve[15] at param indices 38..52 → uniform offset 40
    for (int i = 0; i < 15; ++i)
        u[40 + i] = (ctx->param_count > static_cast<uint32_t>(38 + i)) ? ctx->param_values[38 + i] : 0.0f;

    wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, u, sizeof(u));
    vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "MSEG Thumb Pass");
}

void MSEG::rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
    thumb_state_->release_all();

    static const char* kThumbFragment = R"(
struct Uniforms {
    header0: vec4f,      // num_points, current_value, loop_enabled, loop_start
    header1: vec4f,      // loop_end, pad, pad, pad
    pt_time: array<vec4f, 4>,   // 16 floats
    pt_value: array<vec4f, 4>,  // 16 floats
    pt_curve: array<vec4f, 4>,  // 16 floats (15 used + 1 pad)
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

fn get_time(i: i32) -> f32 {
    let idx = i / 4;
    let comp = i % 4;
    return uniforms.pt_time[idx][comp];
}

fn get_value(i: i32) -> f32 {
    let idx = i / 4;
    let comp = i % 4;
    return uniforms.pt_value[idx][comp];
}

fn get_curve(i: i32) -> f32 {
    let idx = i / 4;
    let comp = i % 4;
    return uniforms.pt_curve[idx][comp];
}

fn curve_interp(t: f32, curve: f32) -> f32 {
    if (abs(curve) < 0.001) { return t; }
    let k = curve * 4.0;
    return (exp(k * t) - 1.0) / (exp(k) - 1.0);
}

fn mseg_at(x: f32, np: i32) -> f32 {
    if (np < 2) { return 0.0; }
    for (var i = 0; i < np - 1; i++) {
        let t0 = get_time(i);
        let t1 = get_time(i + 1);
        if (x <= t1 || i == np - 2) {
            let seg_len = t1 - t0;
            var t = 0.0f;
            if (seg_len > 0.0001) { t = clamp((x - t0) / seg_len, 0.0, 1.0); }
            let shaped = curve_interp(t, get_curve(i));
            return get_value(i) + (get_value(i + 1) - get_value(i)) * shaped;
        }
    }
    return get_value(np - 1);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let np = i32(uniforms.header0.x);
    let current_value = clamp(uniforms.header0.y, 0.0, 1.0);
    let loop_on = uniforms.header0.z > 0.5;
    let ls = i32(uniforms.header0.w);
    let le = i32(uniforms.header1.x);

    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let fill_col = vec4f(80.0/255.0, 130.0/255.0, 190.0/255.0, 160.0/255.0);
    let line_col = vec4f(160.0/255.0, 200.0/255.0, 240.0/255.0, 220.0/255.0);
    let loop_col = vec4f(100.0/255.0, 180.0/255.0, 100.0/255.0, 80.0/255.0);
    let playhead_col = vec4f(255.0/255.0, 200.0/255.0, 80.0/255.0, 220.0/255.0);

    let pad = 0.08;
    let plot_y = (uv.y - pad) / (1.0 - 2.0 * pad);

    let env = mseg_at(uv.x, np);
    let curve_y = 1.0 - env;

    // Loop region tint
    if (loop_on && ls < np && le < np) {
        let ls_time = get_time(ls);
        let le_time = get_time(le);
        if (uv.x >= ls_time && uv.x <= le_time) {
            if (plot_y > curve_y) {
                return loop_col + fill_col;
            }
            let dist = abs(plot_y - curve_y);
            if (dist < 0.025) { return line_col; }
            return loop_col;
        }
    }

    // Fill below curve
    if (plot_y > curve_y) {
        let dist = abs(plot_y - curve_y);
        if (dist < 0.025) { return line_col; }
        return fill_col;
    }

    // Line on curve
    let dist = abs(plot_y - curve_y);
    if (dist < 0.025) { return line_col; }

    // Playhead horizontal line
    if (current_value > 0.001) {
        let ph_y = 1.0 - current_value;
        let ph_dist = abs(plot_y - ph_y);
        if (ph_dist < 0.015) { return playhead_col; }
    }

    return bg;
}
)";

    constexpr size_t kUniformSize = sizeof(float) * 56;
    thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "MSEG Thumb Shader");
    thumb_state_->uniform_buf =
        vivid::thumbnail::create_uniform_buffer(ctx->device, kUniformSize, "MSEG Thumb Uniforms");
    thumb_state_->bind_layout =
        vivid::thumbnail::create_uniform_bind_layout(ctx->device, kUniformSize, "MSEG Thumb BGL");
    thumb_state_->pipe_layout =
        vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "MSEG Thumb Layout");
    thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(
        ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, kUniformSize, "MSEG Thumb BG");
    thumb_state_->pipeline = vivid::thumbnail::create_pipeline(
        ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "MSEG Thumb Pipeline");
    thumb_state_->pipeline_format = ctx->thumbnail_format;
}

void MSEG::draw_inspector(VividInspectorContext* ctx) {
    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;

    float px = ctx->content_x;
    float py = ctx->content_y;
    float w = ctx->content_width;
    constexpr float h = 120.0f;
    constexpr float pad = 6.0f;

    py += 4;

    // Background
    d.draw_rect(o, px, py, w, h, {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

    // Read params from inspector context
    // Param order: num_points=0, total_time=1, loop_enabled=2, loop_start=3, loop_end=4, amplitude=5
    // then pt_time[0..15] at indices 6..21, pt_value[0..15] at 22..37, pt_curve[0..14] at 38..52
    int np = 4;
    bool do_loop = false;
    int ls = 0, le = 3;
    if (ctx->param_count > 0) np = std::max(2, std::min(kMaxPoints, static_cast<int>(ctx->param_values[0])));
    if (ctx->param_count > 2) do_loop = ctx->param_values[2] > 0.5f;
    if (ctx->param_count > 3) ls = static_cast<int>(ctx->param_values[3]);
    if (ctx->param_count > 4) le = static_cast<int>(ctx->param_values[4]);

    // Helper lambdas for coordinate mapping
    float plot_x = px + pad;
    float plot_y = py + pad;
    float plot_w = w - 2.0f * pad;
    float plot_h = h - 2.0f * pad;

    auto time_to_x = [&](float t) -> float {
        return plot_x + t * plot_w;
    };
    auto value_to_y = [&](float v) -> float {
        return plot_y + (1.0f - v) * plot_h;
    };
    auto x_to_time = [&](float x) -> float {
        return (x - plot_x) / plot_w;
    };
    auto y_to_value = [&](float y) -> float {
        return 1.0f - (y - plot_y) / plot_h;
    };

    // Read point data from param_values
    float times[kMaxPoints];
    float values[kMaxPoints];
    float curves[kMaxCurves];
    for (int i = 0; i < kMaxPoints; ++i) {
        times[i]  = (ctx->param_count > static_cast<uint32_t>(6 + i))  ? ctx->param_values[6 + i]  : 0.0f;
        values[i] = (ctx->param_count > static_cast<uint32_t>(22 + i)) ? ctx->param_values[22 + i] : 0.0f;
    }
    for (int i = 0; i < kMaxCurves; ++i) {
        curves[i] = (ctx->param_count > static_cast<uint32_t>(38 + i)) ? ctx->param_values[38 + i] : 0.0f;
    }

    // Draw loop region markers (dashed vertical lines)
    if (do_loop && ls < np && le < np) {
        float lsx = time_to_x(times[ls]);
        float lex = time_to_x(times[le]);
        for (float dy = plot_y; dy < plot_y + plot_h; dy += 8.0f) {
            float dash_end = std::min(dy + 4.0f, plot_y + plot_h);
            d.draw_line(o, lsx, dy, lsx, dash_end, 1.0f,
                        {th.accent.r, th.accent.g, th.accent.b, 0.4f});
            d.draw_line(o, lex, dy, lex, dash_end, 1.0f,
                        {th.accent.r, th.accent.g, th.accent.b, 0.4f});
        }
    }

    // Fill under curve (column fill, same pattern as adsr_inspector)
    float bottom_y = value_to_y(0.0f);
    int cols = static_cast<int>(plot_w / 3.0f);
    float col_w = plot_w / static_cast<float>(cols);

    for (int c = 0; c < cols; ++c) {
        float fx = plot_x + static_cast<float>(c) * col_w;
        float t_norm = static_cast<float>(c) / static_cast<float>(cols);

        // Find segment and interpolate
        float val = 0.0f;
        for (int i = 0; i < np - 1; ++i) {
            if (t_norm <= times[i + 1] || i == np - 2) {
                float seg_len = times[i + 1] - times[i];
                float seg_t = (seg_len > 0.0001f) ? (t_norm - times[i]) / seg_len : 0.0f;
                seg_t = std::max(0.0f, std::min(1.0f, seg_t));
                float shaped = curve_interp(seg_t, curves[i]);
                val = values[i] + (values[i + 1] - values[i]) * shaped;
                break;
            }
        }

        float ey = value_to_y(val);
        float fill_h = bottom_y - ey;
        if (fill_h > 0.0f) {
            d.draw_rect(o, fx, ey, col_w, fill_h,
                        {th.accent.r, th.accent.g, th.accent.b, 0.15f});
        }
    }

    // Draw curve line (polyline with ~20 sub-segments per segment)
    constexpr int kSubSegments = 20;
    for (int i = 0; i < np - 1; ++i) {
        float t0 = times[i];
        float t1 = times[i + 1];
        float v0 = values[i];
        float v1 = values[i + 1];
        float crv = curves[i];

        float prev_x = time_to_x(t0);
        float prev_y = value_to_y(v0);

        for (int s = 1; s <= kSubSegments; ++s) {
            float frac = static_cast<float>(s) / static_cast<float>(kSubSegments);
            float t_pos = t0 + (t1 - t0) * frac;
            float shaped = curve_interp(frac, crv);
            float val = v0 + (v1 - v0) * shaped;

            float cx = time_to_x(t_pos);
            float cy = value_to_y(val);
            d.draw_line(o, prev_x, prev_y, cx, cy, 1.5f,
                        {th.accent.r, th.accent.g, th.accent.b, 0.9f});
            prev_x = cx;
            prev_y = cy;
        }
    }

    // Draw point handles (6x6 rects)
    constexpr float handle_size = 6.0f;
    constexpr float half_handle = handle_size * 0.5f;
    for (int i = 0; i < np; ++i) {
        float hx = time_to_x(times[i]) - half_handle;
        float hy = value_to_y(values[i]) - half_handle;
        VividColor handle_color = (i == dragged_point_)
            ? VividColor{th.bright_text.r, th.bright_text.g, th.bright_text.b, 1.0f}
            : VividColor{th.accent.r, th.accent.g, th.accent.b, 1.0f};
        d.draw_rect(o, hx, hy, handle_size, handle_size, handle_color);
    }

    // Draw playhead dot at current output value
    if (ctx->output_count > 0) {
        float cur_val = ctx->output_values[0];
        float amp = (ctx->param_count > 5) ? ctx->param_values[5] : 1.0f;
        if (amp > 0.0001f) cur_val /= amp; // normalize back
        cur_val = std::max(0.0f, std::min(1.0f, cur_val));

        // Find approximate time position from elapsed state
        // We don't have direct access to elapsed_ from inspector, so just show
        // a horizontal playhead line at current value
        float playhead_y = value_to_y(cur_val);
        if (cur_val > 0.001f) {
            d.draw_line(o, plot_x, playhead_y, plot_x + plot_w, playhead_y, 1.0f,
                        {1.0f, 0.78f, 0.31f, 0.5f}); // warm yellow
        }
    }

    // --- Drag interaction ---
    constexpr float hit_radius = 8.0f;

    if (ctx->mouse.left_clicked) {
        dragged_point_ = -1;
        for (int i = 0; i < np; ++i) {
            float hx = time_to_x(times[i]);
            float hy = value_to_y(values[i]);
            float dx = ctx->mouse.x - hx;
            float dy = ctx->mouse.y - hy;
            if (dx * dx + dy * dy < hit_radius * hit_radius) {
                dragged_point_ = i;
                break;
            }
        }
    }

    if (ctx->mouse.left_down && dragged_point_ >= 0 && dragged_point_ < np) {
        float new_time  = x_to_time(ctx->mouse.x);
        float new_value = y_to_value(ctx->mouse.y);

        // Clamp value to [0, 1]
        new_value = std::max(0.0f, std::min(1.0f, new_value));

        // Clamp time: first point stays at 0, last at 1, others between neighbors
        if (dragged_point_ == 0) {
            new_time = 0.0f;
        } else if (dragged_point_ == np - 1) {
            new_time = 1.0f;
        } else {
            float prev_t = times[dragged_point_ - 1] + 0.001f;
            float next_t = times[dragged_point_ + 1] - 0.001f;
            new_time = std::max(prev_t, std::min(next_t, new_time));
        }

        // Issue set_param commands
        char name_buf[32];
        std::snprintf(name_buf, sizeof(name_buf), "pt_time_%d", dragged_point_);
        ctx->commands.set_param(ctx->commands.opaque, name_buf, new_time);

        std::snprintf(name_buf, sizeof(name_buf), "pt_value_%d", dragged_point_);
        ctx->commands.set_param(ctx->commands.opaque, name_buf, new_value);
    }

    if (!ctx->mouse.left_down) {
        dragged_point_ = -1;
    }

    ctx->consumed_height = 4.0f + h + 4.0f;
}

// --- Audio-rate operator ---

#include "control/audio_scalar_utils.h"

struct MsegAudio : MSEG, vivid::AudioProcessable {
    static constexpr const char* kName = "Mseg";

    void process_audio(const VividAudioContext* ctx) override {
        float sample_dt = ctx->sample_rate > 0
            ? 1.0f / static_cast<float>(ctx->sample_rate)
            : static_cast<float>(ctx->delta_time);
        const vivid::MetronomeTransport metronome = vivid::metronome_transport(ctx);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float gate_in = vivid::audio_scalar_sample(ctx, 0, i);
            vivid::MetronomeTransport sample_metronome =
                vivid::metronome_transport_sample(metronome, i, ctx->sample_rate);
            float phase_in = vivid::resolve_clock_phase(
                clock_source.int_value(), vivid::audio_scalar_sample(ctx, 1, i), sample_metronome);
            compute(gate_in, phase_in, sample_dt);
            ctx->output_buffers[0][i] = current_value_ * amplitude.value;
        }
    }
};

VIVID_REGISTER(MsegAudio)
VIVID_THUMBNAIL(MsegAudio)
VIVID_INSPECTOR(MsegAudio)
