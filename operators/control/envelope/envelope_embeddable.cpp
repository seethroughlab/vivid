#include "envelope.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"

#include <algorithm>

// Embeddable support for ChildOp<Envelope>.
//
// Envelope's public operators link vivid_embeddable_op_support, so the shared
// thumbnail implementation lives here with the out-of-line destructor.

namespace {

float thumb_env_value(float x, float attack, float decay, float sustain, float release, int curve) {
    attack = std::max(attack, 0.001f);
    decay = std::max(decay, 0.001f);
    release = std::max(release, 0.001f);
    sustain = std::clamp(sustain, 0.0f, 1.0f);

    float total = attack + decay + (attack + decay + release) * 0.43f + release;
    float x_a = attack / total;
    float x_d = (attack + decay) / total;
    float x_s = 1.0f - release / total;

    if (x < x_a) {
        return Envelope::shape_attack(x / x_a, curve);
    }
    if (x < x_d) {
        float t = (x - x_a) / std::max(0.001f, x_d - x_a);
        return 1.0f - (1.0f - sustain) * Envelope::shape_decay(t, curve);
    }
    if (x < x_s) {
        return sustain;
    }
    float t = (x - x_s) / std::max(0.001f, 1.0f - x_s);
    return sustain * (1.0f - Envelope::shape_decay(t, curve));
}

} // namespace

Envelope::~Envelope() = default;

void Envelope::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx || !ctx->draw.opaque) return;
    auto& d = const_cast<VividDrawAPI&>(ctx->draw);
    void* o = d.opaque;

    float attack = (ctx->param_count > 0) ? ctx->param_values[0] : 0.001f;
    float decay = (ctx->param_count > 1) ? ctx->param_values[1] : 0.2f;
    float sustain = (ctx->param_count > 2) ? ctx->param_values[2] : 0.7f;
    float release = (ctx->param_count > 3) ? ctx->param_values[3] : 0.3f;
    int curve = (ctx->param_count > 6) ? static_cast<int>(ctx->param_values[6]) : 1;
    float current = (ctx->output_count > 0) ? std::clamp(ctx->output_values[0], 0.0f, 1.0f) : 0.0f;

    float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
    float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

    vivid::draw_plot::draw_thumb_background(d, o, w, h);
    vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, "ADSR", {0.45f, 0.55f, 0.65f, 0.9f}, 0.8f);

    auto env_fn = [attack, decay, sustain, release, curve](float x) {
        return thumb_env_value(x, attack, decay, sustain, release, curve);
    };

    vivid::draw_plot::draw_envelope_plot(d, o,
                                         8.0f, 20.0f, w - 16.0f, h - 26.0f,
                                         env_fn,
                                         {0.31f, 0.51f, 0.75f, 0.35f},
                                         {0.63f, 0.78f, 0.94f, 0.95f},
                                         current,
                                         {1.0f, 0.78f, 0.31f, 0.8f},
                                         2.0f);
}
