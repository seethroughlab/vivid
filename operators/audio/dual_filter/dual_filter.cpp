#include "operator_api/operator.h"
#include "shared/filter_dsp/filter_dsp.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace {
inline float flush_audio_denormal(float x) {
    return std::fabs(x) < 1.0e-20f ? 0.0f : x;
}
} // namespace

/**
 * @brief Dual-stage audio filter with configurable routing for instrument-style filter design.
 *
 * Two independent filter stages (A and B) with 14 filter modes each, routable as
 * serial (A→B or B→A), parallel (blended), or crossover split (low→A, high→B).
 * Built on the same shared filter DSP as the single-stage Filter operator.
 *
 * @input input Audio signal to filter.
 * @input a_cutoff_cv Global cutoff modulation for stage A in semitone-like octave steps.
 * @input b_cutoff_cv Global cutoff modulation for stage B in semitone-like octave steps.
 * @input a_resonance_cv Global resonance modulation for stage A.
 * @input b_resonance_cv Global resonance modulation for stage B.
 * @input a_cutoff_mod Per-lane cutoff modulation for stage A (polyphonic envelopes).
 * @input b_cutoff_mod Per-lane cutoff modulation for stage B (polyphonic envelopes).
 * @input frequencies Per-lane note frequencies used for keytracking in poly chains.
 * @output output Filtered audio signal.
 * @tip Use serial_ab for classic subtractive synth filter stacking.
 * @tip Use split mode to independently shape low and high frequency bands.
 * @tip In poly synth graphs, pair a_cutoff_mod / b_cutoff_mod with Envelope outputs.
 * @recipe Envelope/value -> DualFilter/a_cutoff_mod
 * @recipe NoteBreakout/voice_freqs -> DualFilter/frequencies
 * @pitfall a_cutoff_cv / b_cutoff_cv are global scalar modulations; a_cutoff_mod / b_cutoff_mod are the per-lane paths for poly voices.
 * @family voice_shaper
 * @best_used_with Envelope, NoteBreakout, Gain
 * @common_companions ChordProgression, WavetableOsc, VoiceMixer, Filter
 * @param routing Signal routing between the two filter stages.
 * @param a_keytrack Scales stage A cutoff with note frequency. 1 = full tracking.
 * @param b_keytrack Scales stage B cutoff with note frequency. 1 = full tracking.
 * @see Filter, ParametricEQ
 */
struct DualFilter : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "DualFilter";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    // --- Filter A ---
    vivid::Param<bool>  a_enabled   {"a_enabled",   true};
    vivid::Param<int>   a_mode      {"a_mode",      0,
        {"LP12", "LP24", "HP12", "BP", "Notch", "Comb", "Ladder", "Formant",
         "HP24", "Peak", "Allpass", "BP24", "Diode", "MS-20"}};
    vivid::Param<float> a_cutoff    {"a_cutoff",    2000.0f, 20.0f, 20000.0f};
    vivid::Param<float> a_resonance {"a_resonance",  0.5f,   0.0f,   1.0f};
    vivid::Param<float> a_drive     {"a_drive",      0.0f,   0.0f,   1.0f};
    vivid::Param<float> a_keytrack  {"a_keytrack",   0.0f,   0.0f,   1.0f};

    // --- Filter B ---
    vivid::Param<bool>  b_enabled   {"b_enabled",   true};
    vivid::Param<int>   b_mode      {"b_mode",      2,      // HP12 by default
        {"LP12", "LP24", "HP12", "BP", "Notch", "Comb", "Ladder", "Formant",
         "HP24", "Peak", "Allpass", "BP24", "Diode", "MS-20"}};
    vivid::Param<float> b_cutoff    {"b_cutoff",    4000.0f, 20.0f, 20000.0f};
    vivid::Param<float> b_resonance {"b_resonance",  0.5f,   0.0f,   1.0f};
    vivid::Param<float> b_drive     {"b_drive",      0.0f,   0.0f,   1.0f};
    vivid::Param<float> b_keytrack  {"b_keytrack",   0.0f,   0.0f,   1.0f};

    // --- Routing ---
    vivid::Param<int>   routing     {"routing",     0,
        {"Serial A→B", "Serial B→A", "Parallel", "Split"}};
    vivid::Param<float> parallel_balance {"parallel_balance", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> split_freq       {"split_freq", 1000.0f, 20.0f, 20000.0f};
    vivid::Param<float> output_gain      {"output_gain", 1.0f, 0.0f, 2.0f};

    // Routing enum
    enum Routing { SERIAL_AB = 0, SERIAL_BA = 1, PARALLEL = 2, SPLIT = 3 };

    // Per-lane persistent state
    struct LaneState {
        audio_dsp::FilterState filter_a;
        audio_dsp::FilterState filter_b;
        // Complementary split state: two cascaded one-pole lowpass sections.
        float xover_z1 = 0.f;
        float xover_z2 = 0.f;
    };

    std::vector<float> scratch_;

    DualFilter() {
        // Filter A metadata
        vivid::description(a_enabled, "Enable or bypass filter stage A");
        vivid::description(a_mode, "Filter A algorithm: LP, HP, BP, Notch, Comb, Ladder, Formant, and more");
        vivid::semantic_tag(a_cutoff, "frequency_hz");
        vivid::semantic_shape(a_cutoff, "scalar");
        vivid::semantic_unit(a_cutoff, "Hz");
        vivid::display_hint(a_cutoff, VIVID_DISPLAY_KNOB);
        vivid::description(a_cutoff, "Filter A cutoff frequency in Hz");
        vivid::semantic_tag(a_resonance, "resonance");
        vivid::semantic_shape(a_resonance, "scalar");
        vivid::display_hint(a_resonance, VIVID_DISPLAY_KNOB);
        vivid::description(a_resonance, "Stage A emphasis at the cutoff frequency (0 = flat, 1 = self-oscillation)");
        vivid::display_hint(a_drive, VIVID_DISPLAY_KNOB);
        vivid::description(a_drive, "Nonlinear saturation applied inside filter A");
        vivid::display_hint(a_keytrack, VIVID_DISPLAY_KNOB);
        vivid::description(a_keytrack, "Scales stage A cutoff with note frequency (1 = full tracking)");

        // Filter B metadata
        vivid::description(b_enabled, "Enable or bypass filter stage B");
        vivid::description(b_mode, "Filter B algorithm: LP, HP, BP, Notch, Comb, Ladder, Formant, and more");
        vivid::semantic_tag(b_cutoff, "frequency_hz");
        vivid::semantic_shape(b_cutoff, "scalar");
        vivid::semantic_unit(b_cutoff, "Hz");
        vivid::display_hint(b_cutoff, VIVID_DISPLAY_KNOB);
        vivid::description(b_cutoff, "Filter B cutoff frequency in Hz");
        vivid::semantic_tag(b_resonance, "resonance");
        vivid::semantic_shape(b_resonance, "scalar");
        vivid::display_hint(b_resonance, VIVID_DISPLAY_KNOB);
        vivid::description(b_resonance, "Stage B emphasis at the cutoff frequency (0 = flat, 1 = self-oscillation)");
        vivid::display_hint(b_drive, VIVID_DISPLAY_KNOB);
        vivid::description(b_drive, "Nonlinear saturation applied inside filter B");
        vivid::display_hint(b_keytrack, VIVID_DISPLAY_KNOB);
        vivid::description(b_keytrack, "Scales stage B cutoff with note frequency (1 = full tracking)");

        // Routing metadata
        vivid::description(routing, "Signal routing between the two filter stages");
        vivid::display_hint(parallel_balance, VIVID_DISPLAY_KNOB);
        vivid::description(parallel_balance, "Blend between stage A (0) and stage B (1) in parallel mode");
        vivid::semantic_tag(split_freq, "frequency_hz");
        vivid::semantic_shape(split_freq, "scalar");
        vivid::semantic_unit(split_freq, "Hz");
        vivid::display_hint(split_freq, VIVID_DISPLAY_KNOB);
        vivid::description(split_freq, "Crossover frequency for split mode (low → A, high → B)");
        vivid::semantic_tag(output_gain, "amplitude_linear");
        vivid::semantic_shape(output_gain, "scalar");
        vivid::display_hint(output_gain, VIVID_DISPLAY_KNOB);
        vivid::description(output_gain, "Output gain applied after routing");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        // Filter A group
        param_group(a_enabled,   "Filter A");
        param_group(a_mode,      "Filter A");
        param_group(a_cutoff,    "Filter A");
        param_group(a_resonance, "Filter A");
        param_group(a_drive,     "Filter A");
        param_group(a_keytrack,  "Filter A");

        layout_row(a_cutoff,    4, 0);
        layout_row(a_resonance, 4, 1);
        layout_row(a_drive,     4, 2);
        layout_row(a_keytrack,  4, 3);

        // Filter B group
        param_group(b_enabled,   "Filter B");
        param_group(b_mode,      "Filter B");
        param_group(b_cutoff,    "Filter B");
        param_group(b_resonance, "Filter B");
        param_group(b_drive,     "Filter B");
        param_group(b_keytrack,  "Filter B");

        layout_row(b_cutoff,    4, 0);
        layout_row(b_resonance, 4, 1);
        layout_row(b_drive,     4, 2);
        layout_row(b_keytrack,  4, 3);

        // Routing group
        param_group(routing,          "Routing");
        param_group(parallel_balance, "Routing");
        param_group(split_freq,       "Routing");
        param_group(output_gain,      "Routing");

        layout_row(parallel_balance, 4, 0);
        layout_row(split_freq,       4, 1);
        layout_row(output_gain,      4, 2);

        out.push_back(&a_enabled);
        out.push_back(&a_mode);
        out.push_back(&a_cutoff);
        out.push_back(&a_resonance);
        out.push_back(&a_drive);
        out.push_back(&a_keytrack);
        out.push_back(&b_enabled);
        out.push_back(&b_mode);
        out.push_back(&b_cutoff);
        out.push_back(&b_resonance);
        out.push_back(&b_drive);
        out.push_back(&b_keytrack);
        out.push_back(&routing);
        out.push_back(&parallel_balance);
        out.push_back(&split_freq);
        out.push_back(&output_gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",          VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f, nullptr, "audio_signal",    "audio_buffer", "audio_input",           "Audio input to be filtered."});
        out.push_back({"output",         VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f, nullptr, "audio_signal",    "audio_buffer", "audio_output",          "Filtered audio output."});
        out.push_back({"a_cutoff_cv",    VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL,       0, nullptr, 0, 0.0f, nullptr, "pitch_like_mod",  "scalar",        "global_cutoff_mod",    "Global cutoff modulation for stage A shared by all voices."});
        out.push_back({"b_cutoff_cv",    VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL,       0, nullptr, 0, 0.0f, nullptr, "pitch_like_mod",  "scalar",        "global_cutoff_mod",    "Global cutoff modulation for stage B shared by all voices."});
        out.push_back({"a_resonance_cv", VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL,       0, nullptr, 0, 0.0f, nullptr, "resonance",       "scalar",        "global_resonance_mod", "Global resonance modulation for stage A shared by all voices."});
        out.push_back({"b_resonance_cv", VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL,       0, nullptr, 0, 0.0f, nullptr, "resonance",       "scalar",        "global_resonance_mod", "Global resonance modulation for stage B shared by all voices."});
        out.push_back({"a_cutoff_mod",   VIVID_PORT_LANE_ARRAY,   VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_LANE_ARRAY,   0, nullptr, 0, 0.0f, nullptr, "cutoff_mod",      "lane_array",    "per_note_cutoff_mod",  "Per-lane cutoff modulation for stage A (polyphonic envelopes)."});
        out.push_back({"b_cutoff_mod",   VIVID_PORT_LANE_ARRAY,   VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_LANE_ARRAY,   0, nullptr, 0, 0.0f, nullptr, "cutoff_mod",      "lane_array",    "per_note_cutoff_mod",  "Per-lane cutoff modulation for stage B (polyphonic envelopes)."});
        out.push_back({"frequencies",    VIVID_PORT_LANE_ARRAY,   VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_LANE_ARRAY,   0, nullptr, 0, 0.0f, nullptr, "frequency_hz",    "lane_array",    "per_note_frequency",   "Per-lane note frequencies used for keytracking in polyphonic chains."});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        auto& ls = *vivid_lane_state(ctx, ctx->lane_id, LaneState);

        const float* in  = ctx->input_buffers[0];
        float*       out = ctx->output_buffers[0];
        uint32_t frames  = ctx->buffer_size;
        float sr = static_cast<float>(ctx->sample_rate);

        // Scalar CV inputs (input_buffers: 0=audio, 1=a_cutoff_cv, 2=b_cutoff_cv, 3=a_reso_cv, 4=b_reso_cv)
        float a_cutoff_cv_val = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 0.0f;
        float b_cutoff_cv_val = ctx->input_buffers[2] ? ctx->input_buffers[2][0] : 0.0f;
        float a_reso_cv_val   = ctx->input_buffers[3] ? ctx->input_buffers[3][0] : 0.0f;
        float b_reso_cv_val   = ctx->input_buffers[4] ? ctx->input_buffers[4][0] : 0.0f;

        // Lane-array inputs (input_lanes: 0=a_cutoff_mod, 1=b_cutoff_mod, 2=frequencies)
        float a_cutoff_mod_val = 0.0f;
        float b_cutoff_mod_val = 0.0f;
        float voice_freq = 0.0f;
        if (ctx->input_lanes) {
            uint32_t ci = ctx->lane_index;
            auto& a_mod_sp = ctx->input_lanes[0];
            if (a_mod_sp.data && ci < a_mod_sp.length)
                a_cutoff_mod_val = a_mod_sp.data[ci];
            auto& b_mod_sp = ctx->input_lanes[1];
            if (b_mod_sp.data && ci < b_mod_sp.length)
                b_cutoff_mod_val = b_mod_sp.data[ci];
            auto& freq_sp = ctx->input_lanes[2];
            if (freq_sp.data && ci < freq_sp.length)
                voice_freq = freq_sp.data[ci];
        }

        // Compute modulated cutoffs
        float mod_cutoff_a = a_cutoff.value;
        if (a_cutoff_cv_val != 0.0f)
            mod_cutoff_a *= std::pow(2.0f, a_cutoff_cv_val / 12.0f);
        if (a_cutoff_mod_val != 0.0f)
            mod_cutoff_a *= std::pow(2.0f, a_cutoff_mod_val * 4.0f);
        float kt_a = a_keytrack.value;
        if (kt_a > 0.0f && voice_freq > 0.0f) {
            float oct_from_c4 = std::log2(voice_freq / 261.63f);
            mod_cutoff_a *= std::pow(2.0f, oct_from_c4 * kt_a);
        }
        mod_cutoff_a = std::clamp(mod_cutoff_a, 20.0f, 20000.0f);

        float mod_cutoff_b = b_cutoff.value;
        if (b_cutoff_cv_val != 0.0f)
            mod_cutoff_b *= std::pow(2.0f, b_cutoff_cv_val / 12.0f);
        if (b_cutoff_mod_val != 0.0f)
            mod_cutoff_b *= std::pow(2.0f, b_cutoff_mod_val * 4.0f);
        float kt_b = b_keytrack.value;
        if (kt_b > 0.0f && voice_freq > 0.0f) {
            float oct_from_c4 = std::log2(voice_freq / 261.63f);
            mod_cutoff_b *= std::pow(2.0f, oct_from_c4 * kt_b);
        }
        mod_cutoff_b = std::clamp(mod_cutoff_b, 20.0f, 20000.0f);

        // Compute modulated resonances
        float mod_reso_a = std::clamp(a_resonance.value + a_reso_cv_val, 0.0f, 1.0f);
        float mod_reso_b = std::clamp(b_resonance.value + b_reso_cv_val, 0.0f, 1.0f);

        float drv_a = a_drive.value;
        float drv_b = b_drive.value;
        int ftype_a = a_mode.int_value();
        int ftype_b = b_mode.int_value();
        bool en_a = a_enabled.value;
        bool en_b = b_enabled.value;

        int route = routing.int_value();
        float bal = parallel_balance.value;
        float gain = output_gain.value;

        // Precompute crossover coefficient for split mode
        float xover_g = 0.0f;
        if (route == SPLIT) {
            float fc = std::clamp(split_freq.value, 20.0f, sr * 0.45f);
            xover_g = 1.0f - std::exp(-2.0f * 3.14159265f * fc / sr);
        }

        audio_dsp::FilterParams params_a{};
        params_a.type = ftype_a;
        params_a.cutoff_hz = mod_cutoff_a;
        params_a.resonance = mod_reso_a;
        params_a.drive = drv_a;
        params_a.sample_rate = sr;
        const auto plan_a = audio_dsp::prepare_filter_plan(params_a);

        audio_dsp::FilterParams params_b{};
        params_b.type = ftype_b;
        params_b.cutoff_hz = mod_cutoff_b;
        params_b.resonance = mod_reso_b;
        params_b.drive = drv_b;
        params_b.sample_rate = sr;
        const auto plan_b = audio_dsp::prepare_filter_plan(params_b);

        switch (route) {
            case SERIAL_AB: {
                if (en_a) {
                    audio_dsp::process_filter_block(ls.filter_a, plan_a, in, out, frames);
                    if (en_b)
                        audio_dsp::process_filter_block(ls.filter_b, plan_b, out, out, frames);
                } else if (en_b) {
                    audio_dsp::process_filter_block(ls.filter_b, plan_b, in, out, frames);
                } else if (out != in) {
                    std::memcpy(out, in, sizeof(float) * frames);
                }
                for (uint32_t i = 0; i < frames; ++i)
                    out[i] = flush_audio_denormal(out[i] * gain);
                break;
            }
            case SERIAL_BA: {
                if (en_b) {
                    audio_dsp::process_filter_block(ls.filter_b, plan_b, in, out, frames);
                    if (en_a)
                        audio_dsp::process_filter_block(ls.filter_a, plan_a, out, out, frames);
                } else if (en_a) {
                    audio_dsp::process_filter_block(ls.filter_a, plan_a, in, out, frames);
                } else if (out != in) {
                    std::memcpy(out, in, sizeof(float) * frames);
                }
                for (uint32_t i = 0; i < frames; ++i)
                    out[i] = flush_audio_denormal(out[i] * gain);
                break;
            }
            case PARALLEL: {
                const float ga = 1.0f - bal;
                const float gb = bal;
                if (en_a && en_b) {
                    if (scratch_.size() < frames) scratch_.resize(frames);
                    audio_dsp::process_filter_block(ls.filter_a, plan_a, in, out, frames);
                    audio_dsp::process_filter_block(ls.filter_b, plan_b, in, scratch_.data(), frames);
                    for (uint32_t i = 0; i < frames; ++i)
                        out[i] = flush_audio_denormal((out[i] * ga + scratch_[i] * gb) * gain);
                } else if (en_a) {
                    audio_dsp::process_filter_block(ls.filter_a, plan_a, in, out, frames);
                    for (uint32_t i = 0; i < frames; ++i)
                        out[i] = flush_audio_denormal(out[i] * ga * gain);
                } else if (en_b) {
                    audio_dsp::process_filter_block(ls.filter_b, plan_b, in, out, frames);
                    for (uint32_t i = 0; i < frames; ++i)
                        out[i] = flush_audio_denormal(out[i] * gb * gain);
                } else {
                    std::memset(out, 0, sizeof(float) * frames);
                }
                break;
            }
            case SPLIT: {
                for (uint32_t i = 0; i < frames; i++) {
                    float s = in[i];
                    float lp1 = ls.xover_z1 + xover_g * (s - ls.xover_z1);
                    ls.xover_z1 = flush_audio_denormal(lp1);
                    float lp2 = ls.xover_z2 + xover_g * (lp1 - ls.xover_z2);
                    ls.xover_z2 = flush_audio_denormal(lp2);

                    float low = flush_audio_denormal(lp2);
                    float high = flush_audio_denormal(s - lp2);

                    float out_a = en_a ? ls.filter_a.process_prepared(low, plan_a) : 0.0f;
                    float out_b = en_b ? ls.filter_b.process_prepared(high, plan_b) : 0.0f;
                    out[i] = flush_audio_denormal((out_a + out_b) * gain);
                }
                break;
            }
            default: {
                if (out != in)
                    std::memcpy(out, in, sizeof(float) * frames);
                for (uint32_t i = 0; i < frames; ++i)
                    out[i] = flush_audio_denormal(out[i] * gain);
                break;
            }
        }
    }
};

VIVID_DEFINE_OP(DualFilter) {
}

