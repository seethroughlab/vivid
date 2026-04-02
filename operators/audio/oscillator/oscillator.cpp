#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"
#include "operator_api/thumbnail.h"
#include <cmath>

struct OscThumbState {
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

/**
 * @brief Basic waveform oscillator with frequency and amplitude CV.
 *
 * Phase-accumulating oscillator generating sine, saw, square, or triangle
 * waveforms. CV inputs accept control signals with +/-120 semitone range.
 *
 * @see FmSynth, LFO, Noise
 */
struct Oscillator : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Oscillator";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> frequency{"frequency", 440.0f, 20.0f, 20000.0f};
    vivid::Param<float> amplitude{"amplitude", 0.5f, 0.0f, 1.0f};
    vivid::Param<int>   waveform {"waveform",  0, {"sine", "saw", "square", "triangle"}};

    double phase_ = 0.0;
    OscThumbState* thumb_state_ = nullptr;

    ~Oscillator() override {
        if (thumb_state_) { thumb_state_->release_all(); delete thumb_state_; }
    }

    Oscillator() {
        vivid::semantic_tag(frequency, "frequency_hz");
        vivid::semantic_shape(frequency, "scalar");
        vivid::semantic_unit(frequency, "Hz");
        vivid::description(frequency, "Base pitch of the oscillator in Hz");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::description(amplitude, "Output level of the waveform");
        vivid::description(waveform, "Shape of the generated waveform");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&frequency);
        out.push_back(&amplitude);
        out.push_back(&waveform);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        out.push_back({"freq_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"amp_cv",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 1.0f});
        vivid::append_analysis_ports(out);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_state_) thumb_state_ = new OscThumbState();
        if (!thumb_state_->pipeline || thumb_state_->pipeline_format != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_state_->pipeline || !thumb_state_->bind_group || !thumb_state_->uniform_buf) {
            vivid_report_thumbnail_error(ctx, "oscillator thumbnail pipeline init failed");
            return;
        }
        struct Uniforms { float waveform, amplitude, pad0, pad1; } u{};
        u.waveform = (ctx->param_count > 2) ? ctx->param_values[2] : 0.0f;
        u.amplitude = (ctx->param_count > 1) ? ctx->param_values[1] : 0.5f;
        wgpuQueueWriteBuffer(ctx->queue, thumb_state_->uniform_buf, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_state_->pipeline, thumb_state_->bind_group, "Osc Thumb Pass");
    }

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
        thumb_state_->release_all();
        static const char* kShader = R"(
struct Uniforms { data: vec4f, };
struct VertexOutput { @builtin(position) position: vec4f, @location(0) uv: vec2f, }
@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

const PI: f32 = 3.14159265359;

fn osc_wave(phase: f32, wave: i32) -> f32 {
    switch (wave) {
        case 0: { return sin(phase * 2.0 * PI); }
        case 1: { return 2.0 * phase - 1.0; }
        case 2: { return select(-1.0, 1.0, phase < 0.5); }
        case 3: {
            let t = phase * 4.0;
            if (t < 1.0) { return t; }
            if (t < 3.0) { return 2.0 - t; }
            return t - 4.0;
        }
        default: { return sin(phase * 2.0 * PI); }
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let wave = i32(uniforms.data.x);
    let amp = clamp(uniforms.data.y, 0.0, 1.0);

    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let fill_col = vec4f(80.0/255.0, 130.0/255.0, 190.0/255.0, 160.0/255.0);
    let line_col = vec4f(160.0/255.0, 200.0/255.0, 240.0/255.0, 240.0/255.0);

    // Show 2 cycles across the thumbnail
    let phase = fract(uv.x * 2.0);
    let wave_val = osc_wave(phase, wave) * amp;

    let pad = 0.08;
    let plot_y = (uv.y - pad) / (1.0 - 2.0 * pad);
    let center = 0.5;
    let curve_y = center - wave_val * 0.45;

    // Fill between center and curve
    let lo = min(center, curve_y);
    let hi = max(center, curve_y);
    if (plot_y >= lo && plot_y <= hi) {
        let dist = min(abs(plot_y - curve_y), abs(plot_y - center));
        if (abs(plot_y - curve_y) < 0.02) { return line_col; }
        return fill_col;
    }

    // Line on curve
    if (abs(plot_y - curve_y) < 0.02) { return line_col; }

    // Center line
    if (abs(plot_y - center) < 0.008) {
        return vec4f(60.0/255.0, 65.0/255.0, 75.0/255.0, 180.0/255.0);
    }

    return bg;
}
)";
        thumb_state_->shader = vivid::thumbnail::create_shader(ctx->device, kShader, "Osc Thumb Shader");
        thumb_state_->uniform_buf =
            vivid::thumbnail::create_uniform_buffer(ctx->device, sizeof(float) * 4, "Osc Thumb Uniforms");
        thumb_state_->bind_layout =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, sizeof(float) * 4, "Osc Thumb BGL");
        thumb_state_->pipe_layout =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_state_->bind_layout, "Osc Thumb Layout");
        thumb_state_->bind_group = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_state_->bind_layout, thumb_state_->uniform_buf, sizeof(float) * 4, "Osc Thumb BG");
        thumb_state_->pipeline = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_state_->shader, thumb_state_->pipe_layout, ctx->thumbnail_format, "Osc Thumb Pipeline");
        thumb_state_->pipeline_format = ctx->thumbnail_format;
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        float freq_cv_val = ctx->input_buffers[0] ? ctx->input_buffers[0][0] : 0.0f;
        float amp_cv_val  = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 1.0f;
        // Clamp CV to ±120 semitones (~10 octaves) to prevent pow() overflow.
        if (freq_cv_val < -120.0f) freq_cv_val = -120.0f;
        if (freq_cv_val >  120.0f) freq_cv_val =  120.0f;
        float mod_freq = frequency.value * std::pow(2.0f, freq_cv_val / 12.0f);
        float mod_amp  = amplitude.value * amp_cv_val;
        double phase_inc = static_cast<double>(mod_freq) / ctx->sample_rate;
        // Recover from NaN/Inf in phase accumulator (defensive).
        if (!std::isfinite(phase_)) phase_ = 0.0;
        int wave = waveform.int_value();

        for (uint32_t i = 0; i < ctx->buffer_size; i++) {
            double sample = audio_dsp::waveform(phase_, wave);
            out[i] = static_cast<float>(sample) * mod_amp;
            phase_ += phase_inc;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }
};

VIVID_REGISTER(Oscillator)
VIVID_THUMBNAIL(Oscillator)
