#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include <cmath>
#include <cstring>

static constexpr int kMaxInputs = 16;

/**
 * @brief Stereo summing mixer with per-input gain + pan for up to 16 audio inputs.
 *
 * Sums connected audio inputs into a stereo bus. Each input has its own
 * gain (0..2, default 1) and pan (-1..+1, default 0 = center) using an
 * equal-power pan law. Mono inputs fan to both legs; stereo inputs have
 * their existing image preserved (pan rotates the L/R balance).
 * Disconnected inputs contribute silence. Uses repeat-group ports
 * for grow-on-connect UI behavior.
 *
 * @see Gain, Composite, StereoPanWidth
 */
struct Mixer : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Mixer";
    static constexpr bool kTimeDependent = false;

    // Per-input params (generated in constructor)
    struct InputParams {
        char gain_name[16];
        char pan_name[16];
        char gain_desc[80];
        char pan_desc[80];
        vivid::Param<float> gain{nullptr, 1.0f, 0.0f, 2.0f};
        vivid::Param<float> pan {nullptr, 0.0f, -1.0f, 1.0f};
    };

    InputParams ip_[kMaxInputs];

    WGPURenderPipeline thumb_pipeline_ = nullptr;
    WGPUBindGroup thumb_bind_group_ = nullptr;
    WGPUBindGroupLayout thumb_bind_layout_ = nullptr;
    WGPUBuffer thumb_uniform_buf_ = nullptr;
    WGPUShaderModule thumb_shader_ = nullptr;
    WGPUPipelineLayout thumb_pipe_layout_ = nullptr;
    WGPUTextureFormat thumb_pipeline_format_ = WGPUTextureFormat_Undefined;

    Mixer() {
        for (int i = 0; i < kMaxInputs; ++i) {
            auto& I = ip_[i];
            std::snprintf(I.gain_name, sizeof(I.gain_name), "gain_%d", i);
            std::snprintf(I.pan_name,  sizeof(I.pan_name),  "pan_%d",  i);
            std::snprintf(I.gain_desc, sizeof(I.gain_desc),
                          "Level multiplier for input %d (0 = silent, 1 = unity, 2 = double)", i);
            std::snprintf(I.pan_desc, sizeof(I.pan_desc),
                          "Pan for input %d (-1 = hard left, 0 = center, +1 = hard right)", i);

            I.gain.name = I.gain_name;
            I.pan.name  = I.pan_name;

            vivid::display_hint(I.gain, VIVID_DISPLAY_KNOB);
            vivid::layout_row(I.gain, 2, 0);
            vivid::semantic_tag(I.gain, "amplitude_linear");
            vivid::semantic_shape(I.gain, "scalar");
            vivid::description(I.gain, I.gain_desc);
            vivid::repeat_group(I.gain, "input", static_cast<uint16_t>(i));

            vivid::display_hint(I.pan, VIVID_DISPLAY_KNOB);
            vivid::layout_row(I.pan, 2, 1);
            vivid::semantic_tag(I.pan, "pan");
            vivid::semantic_shape(I.pan, "scalar");
            vivid::description(I.pan, I.pan_desc);
            vivid::repeat_group(I.pan, "input", static_cast<uint16_t>(i));
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        for (int i = 0; i < kMaxInputs; ++i) {
            out.push_back(&ip_[i].gain);
            out.push_back(&ip_[i].pan);
        }
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Fields: name, type, dir, transport, payload_size, type_name, channels,
        //         default_value, stable_type_id, semantic_tag, semantic_shape,
        //         semantic_intent, description, display_hint, repeat_group, repeat_group_idx
        out.push_back({"input_0",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  0});
        out.push_back({"input_1",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  1});
        out.push_back({"input_2",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  2});
        out.push_back({"input_3",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  3});
        out.push_back({"input_4",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  4});
        out.push_back({"input_5",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  5});
        out.push_back({"input_6",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  6});
        out.push_back({"input_7",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  7});
        out.push_back({"input_8",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  8});
        out.push_back({"input_9",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  9});
        out.push_back({"input_10", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 10});
        out.push_back({"input_11", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 11});
        out.push_back({"input_12", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 12});
        out.push_back({"input_13", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 13});
        out.push_back({"input_14", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 14});
        out.push_back({"input_15", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 15});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        const uint32_t n = ctx->buffer_size;
        float* out_l = ctx->output_buffers[0];
        float* out_r = ctx->output_buffers[0] + n;  // planar stereo

        std::memset(out_l, 0, 2 * n * sizeof(float));

        static constexpr float kPiOver4 = 3.14159265358979f * 0.25f;

        for (int i = 0; i < kMaxInputs; ++i) {
            const float* in = ctx->input_buffers[i];
            if (!in) continue;
            const float g = ip_[i].gain.value;
            if (g == 0.0f) continue;

            const uint32_t in_ch = ctx->input_channel_counts
                                       ? ctx->input_channel_counts[i]
                                       : 2u;

            // Equal-power pan law: pan ∈ [-1, +1] → angle ∈ [0, π/2].
            // Mirrors operators/audio/stereo_pan_width/stereo_pan_width.cpp:86-90.
            const float angle = (ip_[i].pan.value + 1.0f) * kPiOver4;
            const float gl = g * std::cos(angle);
            const float gr = g * std::sin(angle);

            if (in_ch >= 2) {
                const float* in_l = in;
                const float* in_r = in + n;
                for (uint32_t s = 0; s < n; ++s) out_l[s] += in_l[s] * gl;
                for (uint32_t s = 0; s < n; ++s) out_r[s] += in_r[s] * gr;
            } else {
                for (uint32_t s = 0; s < n; ++s) {
                    out_l[s] += in[s] * gl;
                    out_r[s] += in[s] * gr;
                }
            }
        }
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_pipeline_ || thumb_pipeline_format_ != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_pipeline_ || !thumb_bind_group_ || !thumb_uniform_buf_) {
            vivid_report_thumbnail_error(ctx, "mixer thumbnail pipeline init failed");
            return;
        }

        struct Uniforms { float pad[4]; } u{};
        wgpuQueueWriteBuffer(ctx->queue, thumb_uniform_buf_, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_pipeline_, thumb_bind_group_, "Mixer Thumb Pass");
    }

    ~Mixer() override {
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);
    }

private:
    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);

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
    let dim_col = vec4f(80.0/255.0, 100.0/255.0, 140.0/255.0, 160.0/255.0);
    let bright_col = vec4f(160.0/255.0, 200.0/255.0, 240.0/255.0, 230.0/255.0);

    // 4 input lines converging to single output
    let x = uv.x;
    let y = uv.y;

    // Input positions (left side, spread vertically)
    let in_y0 = 0.15;
    let in_y1 = 0.38;
    let in_y2 = 0.62;
    let in_y3 = 0.85;
    let out_y = 0.5;  // output at center

    // Quadratic interpolation from input to output
    let t = x * x;  // quadratic ease
    let line0 = in_y0 + (out_y - in_y0) * t;
    let line1 = in_y1 + (out_y - in_y1) * t;
    let line2 = in_y2 + (out_y - in_y2) * t;
    let line3 = in_y3 + (out_y - in_y3) * t;

    let thickness = 0.018;

    // Check each input line (dim)
    if (abs(y - line0) < thickness) { return dim_col; }
    if (abs(y - line1) < thickness) { return dim_col; }
    if (abs(y - line2) < thickness) { return dim_col; }
    if (abs(y - line3) < thickness) { return dim_col; }

    // Output line on right side (bright, only after convergence)
    if (x > 0.85 && abs(y - out_y) < thickness * 1.5) {
        return bright_col;
    }

    return bg;
}
)";

        thumb_shader_ = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "Mixer Thumb Shader");
        thumb_uniform_buf_ =
            vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Mixer Thumb Uniforms");
        thumb_bind_layout_ =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Mixer Thumb BGL");
        thumb_pipe_layout_ =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_bind_layout_, "Mixer Thumb Layout");
        thumb_bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_bind_layout_, thumb_uniform_buf_, sizeof(float) * 4, "Mixer Thumb BG");
        thumb_pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_shader_, thumb_pipe_layout_, ctx->thumbnail_format, "Mixer Thumb Pipeline");
        thumb_pipeline_format_ = ctx->thumbnail_format;
    }
};

VIVID_DEFINE_OP(Mixer) {
}

VIVID_THUMBNAIL(Mixer)
