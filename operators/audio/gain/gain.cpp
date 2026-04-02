#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

/**
 * @brief Simple amplitude multiplier with CV modulation.
 *
 * Scales the input signal by a gain factor. Connect a control signal
 * to the amplitude CV input for dynamic volume control.
 *
 * @input input Audio signal to scale.
 * @input amplitude_cv Scalar gain multiplier applied on top of the gain param.
 * @output output Scaled audio signal.
 * @family voice_shaper
 * @best_used_with EnvelopeAu, Filter, VoiceMixer
 * @common_companions ChordProgressionAu, WavetableOsc
 * @see Mixer, Compressor, Limiter
 */
struct Gain : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Gain";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 2.0f};

    Gain() {
        vivid::semantic_tag(gain, "amplitude_linear");
        vivid::semantic_shape(gain, "scalar");
        vivid::semantic_intent(gain, "input_gain");
        vivid::description(gain, "Scales the input signal amplitude (0 = silence, 1 = unity)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor input_port{"input", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,
                                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f};
        vivid::semantic_tag(input_port, "audio_signal");
        vivid::semantic_shape(input_port, "audio_buffer");
        vivid::semantic_intent(input_port, "audio_input");
        vivid::description(input_port, "Audio input signal to scale.");
        out.push_back(input_port);

        VividPortDescriptor output_port{"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                                        VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f};
        vivid::semantic_tag(output_port, "audio_signal");
        vivid::semantic_shape(output_port, "audio_buffer");
        vivid::semantic_intent(output_port, "audio_output");
        vivid::description(output_port, "Scaled audio output.");
        out.push_back(output_port);

        VividPortDescriptor amp_cv_port{"amplitude_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,
                                        VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 1.0f};
        vivid::semantic_tag(amp_cv_port, "amplitude_linear");
        vivid::semantic_shape(amp_cv_port, "scalar");
        vivid::semantic_intent(amp_cv_port, "global_gain_mod");
        vivid::description(amp_cv_port, "Scalar gain modulation input, often driven by an envelope or macro.");
        out.push_back(amp_cv_port);
        vivid::append_analysis_ports(out);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        const auto& d = ctx->draw;
        void* o = d.opaque;

        float g = (ctx->param_count > 0) ? std::clamp(ctx->param_values[0], 0.0f, 2.0f) : 1.0f;

        float w = static_cast<float>(ctx->thumbnail_logical_width  ? ctx->thumbnail_logical_width  : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        // Background
        d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

        // dB label
        float db = (g > 0.0001f) ? 20.0f * std::log10(g) : -60.0f;
        char db_label[16];
        if (db <= -60.0f)
            std::snprintf(db_label, sizeof(db_label), "-inf dB");
        else
            std::snprintf(db_label, sizeof(db_label), "%+.1fdB", db);
        d.draw_text(o, 6, 3, db_label, {0.45f, 0.55f, 0.65f, 1.0f}, 1.0f);

        // Bar layout
        float pad = 6.0f;
        float bar_top = 22.0f;
        float bar_bot = h - pad;
        float bar_h = bar_bot - bar_top;
        float bar_left = w * 0.3f;
        float bar_right = w * 0.7f;
        float bar_w = bar_right - bar_left;

        // Bar background
        d.draw_rounded_rect(o, bar_left, bar_top, bar_w, bar_h, 2.0f, {0.16f, 0.16f, 0.19f, 0.8f});

        // Filled portion: gain normalized 0-2 -> 0-1
        float fill_norm = std::clamp(g / 2.0f, 0.0f, 1.0f);
        float fill_h = fill_norm * bar_h;
        float fill_top = bar_bot - fill_h;

        // Draw gradient via stacked rects (8 slices)
        int slices = 8;
        float slice_h = fill_h / slices;
        for (int i = 0; i < slices; ++i) {
            float sy = fill_top + i * slice_h;
            // norm_y: 0 at bottom (green), 1 at top (red)
            float norm_y = 1.0f - (float(i) + 0.5f) / slices;

            float r, gn, b;
            if (norm_y < 0.5f) {
                float t = norm_y * 2.0f;
                r  = 0.31f + t * (0.86f - 0.31f);
                gn = 0.75f + t * (0.78f - 0.75f);
                b  = 0.39f + t * (0.24f - 0.39f);
            } else {
                float t = (norm_y - 0.5f) * 2.0f;
                r  = 0.86f + t * (0.86f - 0.86f);
                gn = 0.78f + t * (0.31f - 0.78f);
                b  = 0.24f + t * (0.24f - 0.24f);
            }
            d.draw_rect(o, bar_left + 1, sy, bar_w - 2, slice_h + 0.5f, {r, gn, b, 0.86f});
        }

        // Unity (1.0) marker line at the midpoint of the bar
        float unity_y = bar_top + bar_h * 0.5f;
        d.draw_line(o, bar_left - 3, unity_y, bar_right + 3, unity_y, 1.5f, {0.78f, 0.82f, 0.86f, 0.7f});

        // Unity label
        float unity_label_w = d.text_width ? d.text_width(o, "1.0", 0.8f) : 14.0f;
        d.draw_text(o, bar_right + 5, unity_y - 5, "1.0", {0.55f, 0.6f, 0.65f, 0.7f}, 0.8f);
        (void)unity_label_w;
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        float amp_cv_val = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 1.0f;
        float g = gain.value * amp_cv_val;

        for (uint32_t i = 0; i < ctx->buffer_size; i++)
            out[i] = in[i] * g;
    }
};

VIVID_REGISTER(Gain)
VIVID_THUMBNAIL(Gain)
