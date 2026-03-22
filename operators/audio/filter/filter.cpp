#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/thumbnail.h"

#include <cmath>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// State Variable Filter (SVF) — Hal Chamberlin / Andy Simper formulation
//
// Per-sample update (run twice for numerical stability and tighter response):
//   low  += f * band
//   high  = input - low - q * band
//   band += f * high
//   notch = high + low
//
// f = 2 * sin(pi * cutoff / sample_rate)  — MUST be clamped to <= 0.95
// q = 1 / resonance
//
// Float CV input ordinals:
//   cutoff_cv    -> input_float_values[0]  semitone offset (±72 st), default 0.0
//   resonance_cv -> input_float_values[1]  additive offset (±2.0),   default 0.0
// ---------------------------------------------------------------------------

struct Filter : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "Filter";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> cutoff    {"cutoff",    2000.0f, 20.0f, 20000.0f};
    vivid::Param<float> resonance {"resonance",  0.7f,   0.1f,   4.0f};
    vivid::Param<int>   mode      {"mode",       0, {"Low-pass", "High-pass", "Band-pass", "Notch"}};

    float low_  = 0.0f;
    float band_ = 0.0f;

    WGPURenderPipeline thumb_pipeline_ = nullptr;
    WGPUBindGroup thumb_bind_group_ = nullptr;
    WGPUBindGroupLayout thumb_bind_layout_ = nullptr;
    WGPUBuffer thumb_uniform_buf_ = nullptr;
    WGPUShaderModule thumb_shader_ = nullptr;
    WGPUPipelineLayout thumb_pipe_layout_ = nullptr;
    WGPUTextureFormat thumb_pipeline_format_ = WGPUTextureFormat_Undefined;

    Filter() {
        vivid::semantic_tag(cutoff, "frequency_hz");
        vivid::semantic_shape(cutoff, "scalar");
        vivid::semantic_unit(cutoff, "Hz");
        vivid::display_hint(cutoff, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(resonance, "amplitude_linear");
        vivid::semantic_shape(resonance, "scalar");
        vivid::semantic_intent(resonance, "resonance");
        vivid::display_hint(resonance, VIVID_DISPLAY_KNOB);

        vivid::semantic_shape(mode, "scalar");
        vivid::semantic_intent(mode, "filter_mode");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&cutoff);
        out.push_back(&resonance);
        out.push_back(&mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",        VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",       VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"cutoff_cv",    VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"resonance_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* in  = ctx->input_buffers[0];
        float*       out = ctx->output_buffers[0];
        uint32_t frames  = ctx->buffer_size;

        float cutoff_cv_val    = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float resonance_cv_val = ctx->input_float_values ? ctx->input_float_values[1] : 0.0f;

        // cutoff CV: semitone offset via 2^(cv/12), clamped to [20, 20000]
        float mod_cutoff = cutoff.value * std::pow(2.0f, cutoff_cv_val / 12.0f);
        if (mod_cutoff < 20.0f)    mod_cutoff = 20.0f;
        if (mod_cutoff > 20000.0f) mod_cutoff = 20000.0f;

        // resonance CV: additive, clamped to [0.1, 4.0]
        float mod_resonance = resonance.value + resonance_cv_val;
        if (mod_resonance < 0.1f) mod_resonance = 0.1f;
        if (mod_resonance > 4.0f) mod_resonance = 4.0f;

        float sr = static_cast<float>(ctx->sample_rate);
        float f  = 2.0f * std::sin(3.14159265f * mod_cutoff / sr);
        // Clamp f: values above ~0.95 cause unstable feedback (f can reach ~1.93 at 20kHz/48kHz)
        if (f > 0.95f) f = 0.95f;
        float q = 1.0f / mod_resonance;

        int filter_mode = mode.int_value();

        float low  = low_;
        float band = band_;

        for (uint32_t i = 0; i < frames; i++) {
            float input = in[i];

            // First SVF pass
            low  += f * band;
            float high  = input - low - q * band;
            band += f * high;
            // Second SVF pass — reduces aliasing, tightens slope (Andy Simper recommendation)
            low  += f * band;
            high  = input - low - q * band;
            band += f * high;

            float notch = high + low;

            switch (filter_mode) {
                case 0:  out[i] = low;   break;  // Low-pass
                case 1:  out[i] = high;  break;  // High-pass
                case 2:  out[i] = band;  break;  // Band-pass
                case 3:  out[i] = notch; break;  // Notch
                default: out[i] = low;   break;
            }
        }

        low_  = low;
        band_ = band;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_pipeline_ || thumb_pipeline_format_ != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_pipeline_ || !thumb_bind_group_ || !thumb_uniform_buf_) {
            vivid_report_thumbnail_error(ctx, "filter thumbnail pipeline init failed");
            return;
        }

        struct Uniforms { float cutoff, resonance, mode, pad; } u{};
        u.cutoff = ctx->param_values[0];
        u.resonance = ctx->param_values[1];
        u.mode = ctx->param_values[2];
        wgpuQueueWriteBuffer(ctx->queue, thumb_uniform_buf_, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_pipeline_, thumb_bind_group_, "Filter Thumb Pass");
    }

    ~Filter() override {
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
    let fill_col = vec4f(80.0/255.0, 130.0/255.0, 190.0/255.0, 150.0/255.0);
    let line_col = vec4f(160.0/255.0, 200.0/255.0, 240.0/255.0, 240.0/255.0);
    let ref_col = vec4f(50.0/255.0, 55.0/255.0, 65.0/255.0, 120.0/255.0);

    let cutoff = uniforms.data.x;
    let resonance = max(uniforms.data.y, 0.1);
    let mode = i32(uniforms.data.z);

    // Log frequency axis: 20 Hz to 20 kHz
    let freq = 20.0 * pow(1000.0, uv.x);
    let r = freq / cutoff;
    let Q = resonance;
    let r2 = r * r;
    let rQ = r / Q;
    let one_minus_r2 = 1.0 - r2;
    let denom = one_minus_r2 * one_minus_r2 + rQ * rQ;

    // Magnitude based on filter mode
    var mag = 0.0;
    if (mode == 0) {        // Low-pass
        mag = 1.0 / sqrt(denom);
    } else if (mode == 1) { // High-pass
        mag = r2 / sqrt(denom);
    } else if (mode == 2) { // Band-pass
        mag = rQ / sqrt(denom);
    } else {                // Notch
        mag = sqrt(one_minus_r2 * one_minus_r2 / denom);
    }

    // Convert to dB, map to Y: range -48 dB to +12 dB
    let db = 20.0 * log(max(mag, 0.0001)) / log(10.0);
    let db_norm = (db + 48.0) / 60.0;  // 0 at -48dB, 1 at +12dB
    let curve_y = 1.0 - clamp(db_norm, 0.0, 1.0);

    let plot_y = uv.y;

    // 0 dB reference line
    let ref_y = 1.0 - (48.0 / 60.0);  // 0dB position
    if (abs(plot_y - ref_y) < 0.005) {
        return ref_col;
    }

    // Fill below curve
    if (plot_y > curve_y) {
        let dist = abs(plot_y - curve_y);
        if (dist < 0.025) {
            return line_col;
        }
        return fill_col;
    }

    // Line on curve
    let dist = abs(plot_y - curve_y);
    if (dist < 0.025) {
        return line_col;
    }

    return bg;
}
)";

        thumb_shader_ = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "Filter Thumb Shader");
        thumb_uniform_buf_ =
            vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Filter Thumb Uniforms");
        thumb_bind_layout_ =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Filter Thumb BGL");
        thumb_pipe_layout_ =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_bind_layout_, "Filter Thumb Layout");
        thumb_bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_bind_layout_, thumb_uniform_buf_, sizeof(float) * 4, "Filter Thumb BG");
        thumb_pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_shader_, thumb_pipe_layout_, ctx->thumbnail_format, "Filter Thumb Pipeline");
        thumb_pipeline_format_ = ctx->thumbnail_format;
    }
};

VIVID_REGISTER(Filter)
VIVID_THUMBNAIL(Filter)
