#include "operator_api/operator.h"
#include "shared/filter_dsp/filter_dsp.h"
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
 * @input input Audio signal to filter.
 * @input cutoff_cv Scalar cutoff modulation in semitone-like octave steps.
 * @input resonance_cv Scalar resonance modulation added to the resonance param.
 * @input cutoff_mod Per-lane cutoff modulation for polyphonic note articulation.
 * @input frequencies Per-lane note frequencies used for keytracking in poly chains.
 * @output output Filtered audio signal.
 * @tip Use Comb mode with short delay times for Karplus-Strong-like string sounds.
 * @tip In poly synth graphs, pair cutoff_mod with an Envelope driven by NoteBreakout/voice_gates and NoteBreakout/voice_ids.
 * @recipe Envelope/value -> Filter/cutoff_mod
 * @recipe NoteBreakout/voice_freqs -> Filter/frequencies
 * @pitfall cutoff_cv is global scalar modulation; cutoff_mod is the per-lane path for poly voices.
 * @family voice_shaper
 * @best_used_with Envelope, NoteBreakout, Gain
 * @common_companions ChordProgression, WavetableOsc, VoiceMixer
 * @param mode Filter algorithm. Each mode has a distinct character.
 * @param keytrack Scales cutoff with note frequency. 1 = full tracking.
 * @see ParametricEQ, Vocoder
 */
struct Filter : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Filter";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    vivid::Param<float> cutoff    {"cutoff",    2000.0f, 20.0f, 20000.0f};
    vivid::Param<float> resonance {"resonance",  0.5f,   0.0f,   1.0f};
    vivid::Param<int>   mode      {"mode",       0,
        {"LP12", "LP24", "HP12", "BP", "Notch", "Comb", "Ladder", "Formant",
         "HP24", "Peak", "Allpass", "BP24", "Diode", "MS-20"}};
    vivid::Param<float> drive     {"drive",      0.0f,   0.0f,   1.0f};
    vivid::Param<float> keytrack  {"keytrack",   0.0f,   0.0f,   1.0f};

    struct LaneState {
        audio_dsp::FilterState filter_state;
    };

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
        out.push_back({"input",        VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f, nullptr, "audio_signal",   "audio_buffer", "audio_input",          "Audio input to be filtered."});
        out.push_back({"output",       VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f, nullptr, "audio_signal",   "audio_buffer", "audio_output",         "Filtered audio output."});
        out.push_back({"cutoff_cv",    VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL,       0, nullptr, 0, 0.0f, nullptr, "pitch_like_mod", "scalar",        "global_cutoff_mod",   "Global cutoff modulation shared by all voices."});
        out.push_back({"resonance_cv", VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL,       0, nullptr, 0, 0.0f, nullptr, "resonance",      "scalar",        "global_resonance_mod","Global resonance modulation shared by all voices."});
        out.push_back({"cutoff_mod",   VIVID_PORT_LANE_ARRAY,   VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_LANE_ARRAY,   0, nullptr, 0, 0.0f, nullptr, "cutoff_mod",     "lane_array",    "per_note_cutoff_mod", "Per-lane cutoff modulation for polyphonic envelopes and note shaping."});
        out.push_back({"frequencies",  VIVID_PORT_LANE_ARRAY,   VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_LANE_ARRAY,   0, nullptr, 0, 0.0f, nullptr, "frequency_hz",   "lane_array",    "per_note_frequency",  "Per-lane note frequencies used for keytracking in polyphonic chains."});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        static constexpr uint32_t kMaxChannels = 2;
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        uint32_t frames  = ctx->buffer_size;
        float sr = static_cast<float>(ctx->sample_rate);

        // Signal CV inputs
        float cutoff_cv_val    = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 0.0f;
        float resonance_cv_val = ctx->input_buffers[2] ? ctx->input_buffers[2][0] : 0.0f;

        // Lane-array inputs via the value API: cutoff_mod (input port 3) +
        // frequencies (input port 4), indexed by lane_index. NOTE: the legacy
        // code read ctx->input_lanes[0]/[1], but the lane views are full-input-
        // ordinal-indexed, so [0]/[1] were the audio/cutoff_cv ports (empty lanes)
        // and the per-lane modulation was effectively dead. Reading the actual
        // lane-array ports (3/4) via ctx->values fixes that latent indexing bug.
        float cutoff_mod_val = 0.0f;
        float voice_freq = 0.0f;
        if (ctx->values) {
            uint32_t ci = ctx->lane_index;
            const float* cm = vivid_value_floats(&ctx->values[3]);
            uint32_t cm_n = vivid_value_count(&ctx->values[3]);
            if (cm && ci < cm_n) cutoff_mod_val = cm[ci];
            const float* fr = vivid_value_floats(&ctx->values[4]);
            uint32_t fr_n = vivid_value_count(&ctx->values[4]);
            if (fr && ci < fr_n) voice_freq = fr[ci];
        }

        float mod_cutoff = cutoff.value;
        if (cutoff_cv_val != 0.0f)
            mod_cutoff *= std::pow(2.0f, cutoff_cv_val / 12.0f);
        if (cutoff_mod_val != 0.0f)
            mod_cutoff *= std::pow(2.0f, cutoff_mod_val * 4.0f);

        float kt = keytrack.value;
        if (kt > 0.0f && voice_freq > 0.0f) {
            float oct_from_c4 = std::log2(voice_freq / 261.63f);
            mod_cutoff *= std::pow(2.0f, oct_from_c4 * kt);
        }
        mod_cutoff = std::clamp(mod_cutoff, 20.0f, 20000.0f);

        float mod_reso = std::clamp(resonance.value + resonance_cv_val, 0.0f, 1.0f);

        audio_dsp::FilterParams filter_params{};
        filter_params.type = mode.int_value();
        filter_params.cutoff_hz = mod_cutoff;
        filter_params.resonance = mod_reso;
        filter_params.drive = drive.value;
        filter_params.sample_rate = sr;
        const auto plan = audio_dsp::prepare_filter_plan(filter_params);

        for (uint32_t c = 0; c < nch; c++) {
            uint64_t key = static_cast<uint64_t>(ctx->lane_id) * kMaxChannels + c;
            auto& ls = *vivid_lane_state(ctx, key, LaneState);
            const float* in_c  = ctx->input_buffers[0]  + c * frames;
            float*       out_c = ctx->output_buffers[0] + c * frames;
            audio_dsp::process_filter_block(ls.filter_state, plan, in_c, out_c, frames);
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

    // Cutoff cursor position on log-frequency axis
    let cursor_col = vec4f(1.0, 0.78, 0.31, 0.45);
    let dot_col = vec4f(1.0, 0.78, 0.31, 0.95);
    let cutoff_x = log(cutoff / 20.0) / log(1000.0);

    // Magnitude at cutoff (r=1): denom = 0 + (1/Q)^2 = 1/Q^2
    var mag_at_cutoff = 0.0;
    let Q_at_cutoff = 1.0 / Q;
    if (mode == 0 || mode == 1) {
        mag_at_cutoff = Q;
    } else if (mode == 2 || mode == 3) {
        mag_at_cutoff = Q;
    } else if (mode == 4 || mode == 5) {
        mag_at_cutoff = 1.0;
    } else if (mode == 6) {
        mag_at_cutoff = 0.0001;
    } else {
        mag_at_cutoff = Q;
    }
    let db_at_cutoff = 20.0 * log(max(mag_at_cutoff, 0.0001)) / log(10.0);
    let dot_y = 1.0 - clamp((db_at_cutoff + 48.0) / 60.0, 0.0, 1.0);

    // Dot at curve intersection
    let dot_center = vec2f(cutoff_x, dot_y);
    if (length((uv - dot_center) * vec2f(4.0, 1.0)) < 0.04) {
        return dot_col;
    }

    // Vertical cursor line
    if (abs(uv.x - cutoff_x) < 0.006) {
        return cursor_col;
    }

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

VIVID_DEFINE_OP(Filter) {
}

VIVID_THUMBNAIL(Filter)
