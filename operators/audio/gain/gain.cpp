#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"
#include "shared/audio_kernels/audio_buffer_kernels.h"

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
 * @best_used_with Envelope, Filter, VoiceMixer
 * @common_companions ChordProgression, WavetableOsc
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
        auto& d = const_cast<VividDrawAPI&>(ctx->draw);
        void* o = d.opaque;

        float g = (ctx->param_count > 0) ? std::clamp(ctx->param_values[0], 0.0f, 2.0f) : 1.0f;

        float w = static_cast<float>(ctx->thumbnail_logical_width  ? ctx->thumbnail_logical_width  : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        vivid::draw_plot::draw_thumb_background(d, o, w, h);

        float db = (g > 0.0001f) ? 20.0f * std::log10(g) : -60.0f;
        char db_label[16];
        if (db <= -60.0f)
            std::snprintf(db_label, sizeof(db_label), "-inf dB");
        else
            std::snprintf(db_label, sizeof(db_label), "%+.1fdB", db);
        vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, db_label, {0.45f, 0.55f, 0.65f, 0.95f}, 0.8f);

        float pad = 6.0f;
        float bar_top = 22.0f;
        float bar_bot = h - pad;
        float bar_h = bar_bot - bar_top;
        float bar_left = w * 0.3f;
        float bar_right = w * 0.7f;
        float bar_w = bar_right - bar_left;

        vivid::draw_plot::draw_scalar_meter(d, o,
                                            bar_left, bar_top, bar_w, bar_h,
                                            std::clamp(g / 2.0f, 0.0f, 1.0f),
                                            {0.16f, 0.16f, 0.19f, 0.8f},
                                            {0.31f, 0.75f, 0.39f, 0.86f},
                                            {0.86f, 0.31f, 0.24f, 0.86f},
                                            2.0f,
                                            0.5f,
                                            {0.78f, 0.82f, 0.86f, 0.7f});
        vivid::draw_plot::draw_thumb_value(d, o, bar_right + 5.0f, bar_top + bar_h * 0.5f - 5.0f, 24.0f,
                                           "1.0", {0.55f, 0.6f, 0.65f, 0.7f}, 0.8f);
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        float amp_cv_val = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 1.0f;
        float g = gain.value * amp_cv_val;

        vivid::audio_kernels::scale(in, out, ctx->buffer_size, g);
    }
};

VIVID_REGISTER(Gain)
VIVID_THUMBNAIL(Gain)
