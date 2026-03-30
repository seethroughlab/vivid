#include "operator_api/operator.h"
#include "operator_api/filter_dsp.h"
#include "operator_api/thumbnail.h"

#include <cmath>
#include <cstring>

/**
 * @brief Multi-mode audio filter with 14 filter types and keytracking.
 *
 * Biquad-based filter supporting LP12, LP24, HP12, HP24, BP, BP24, Notch,
 * Peak, Allpass, Comb, Ladder, Formant, Diode, and MS-20 modes. Keytracking
 * shifts cutoff based on incoming note frequency for polyphonic chains.
 *
 * @tip Use Comb mode with short delay times for Karplus-Strong-like string sounds.
 * @param mode Filter algorithm. Each mode has a distinct character.
 * @param keytrack Scales cutoff with note frequency. 1 = full tracking.
 * @see ParametricEQ, Vocoder
 */
struct Filter : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Filter";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> cutoff    {"cutoff",    2000.0f, 20.0f, 20000.0f};
    vivid::Param<float> resonance {"resonance",  0.5f,   0.0f,   1.0f};
    vivid::Param<int>   mode      {"mode",       0,
        {"LP12", "LP24", "HP12", "BP", "Notch", "Comb", "Ladder", "Formant",
         "HP24", "Peak", "Allpass", "BP24", "Diode", "MS-20"}};
    vivid::Param<float> drive     {"drive",      0.0f,   0.0f,   1.0f};
    vivid::Param<float> keytrack  {"keytrack",   0.0f,   0.0f,   1.0f};

    audio_dsp::FilterState filter_state_;

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
        vivid::description(cutoff, "Filter cutoff frequency in Hz");

        vivid::semantic_tag(resonance, "resonance");
        vivid::semantic_shape(resonance, "scalar");
        vivid::display_hint(resonance, VIVID_DISPLAY_KNOB);
        vivid::description(resonance, "Emphasis at the cutoff frequency (0 = flat, 1 = self-oscillation)");

        vivid::description(mode, "Filter algorithm: LP, HP, BP, Notch, Comb, Ladder, Formant, and more");
        vivid::display_hint(drive, VIVID_DISPLAY_KNOB);
        vivid::description(drive, "Nonlinear saturation applied inside the filter");
        vivid::display_hint(keytrack, VIVID_DISPLAY_KNOB);
        vivid::description(keytrack, "Scales cutoff with note frequency (1 = full tracking)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        param_group(cutoff,    "Filter");
        param_group(resonance, "Filter");
        param_group(mode,      "Filter");
        param_group(drive,     "Filter");
        param_group(keytrack,  "Filter");

        layout_row(cutoff,    4, 0);
        layout_row(resonance, 4, 1);
        layout_row(drive,     4, 2);
        layout_row(keytrack,  4, 3);

        out.push_back(&cutoff);
        out.push_back(&resonance);
        out.push_back(&mode);
        out.push_back(&drive);
        out.push_back(&keytrack);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",        VIVID_PORT_AUDIO,  VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",       VIVID_PORT_AUDIO,  VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"cutoff_cv",    VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"resonance_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        // Spread inputs for per-voice modulation (used via channel_index in auto-dup)
        out.push_back({"cutoff_mod",   VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"frequencies",  VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* in  = ctx->input_buffers[0];
        float*       out = ctx->output_buffers[0];
        uint32_t frames  = ctx->buffer_size;
        float sr = static_cast<float>(ctx->sample_rate);

        // Signal CV inputs
        float cutoff_cv_val    = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float resonance_cv_val = ctx->input_float_values ? ctx->input_float_values[1] : 0.0f;

        // Spread inputs via channel_index (for auto-dup poly chains)
        float cutoff_mod_val = 0.0f;
        float voice_freq = 0.0f;
        if (ctx->input_spreads) {
            uint8_t ci = ctx->channel_index;
            auto& cutoff_mod_sp = ctx->input_spreads[0];  // cutoff_mod spread
            if (cutoff_mod_sp.data && ci < cutoff_mod_sp.length)
                cutoff_mod_val = cutoff_mod_sp.data[ci];
            auto& freq_sp = ctx->input_spreads[1];  // frequencies spread
            if (freq_sp.data && ci < freq_sp.length)
                voice_freq = freq_sp.data[ci];
        }

        // Base cutoff with CV modulation
        float mod_cutoff = cutoff.value;
        if (cutoff_cv_val != 0.0f)
            mod_cutoff *= std::pow(2.0f, cutoff_cv_val / 12.0f);

        // Per-voice cutoff modulation from spread (±4 octaves)
        if (cutoff_mod_val != 0.0f)
            mod_cutoff *= std::pow(2.0f, cutoff_mod_val * 4.0f);

        // Keytracking: shift cutoff based on voice frequency relative to C4
        float kt = keytrack.value;
        if (kt > 0.0f && voice_freq > 0.0f) {
            float oct_from_c4 = std::log2(voice_freq / 261.63f);
            mod_cutoff *= std::pow(2.0f, oct_from_c4 * kt);
        }

        mod_cutoff = std::clamp(mod_cutoff, 20.0f, 20000.0f);

        // Resonance with CV
        float mod_reso = resonance.value + resonance_cv_val;
        mod_reso = std::clamp(mod_reso, 0.0f, 1.0f);

        float drv = drive.value;
        int ftype = mode.int_value();

        for (uint32_t i = 0; i < frames; i++) {
            out[i] = filter_state_.process(in[i], mod_cutoff, mod_reso, drv, ftype, sr);
        }
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
    if (mode == 0 || mode == 1) {        // Low-pass
        mag = 1.0 / sqrt(denom);
    } else if (mode == 2 || mode == 3) { // High-pass
        mag = r2 / sqrt(denom);
    } else if (mode == 4 || mode == 5) { // Band-pass
        mag = rQ / sqrt(denom);
    } else if (mode == 6) {              // Notch
        mag = sqrt(one_minus_r2 * one_minus_r2 / denom);
    } else {                             // Other modes: show LP approximation
        mag = 1.0 / sqrt(denom);
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
