#pragma once
// Internal frame-rate Smooth implementation used by ChildOp<Smooth>
// consumers. The public operator surface uses smooth_fr / smooth_au variants.

#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

struct SmoothThumbState;
/**
 * @brief Exponential smoother / envelope follower with separate rise and
 * fall times.
 *
 * Asymmetric first-order low-pass filter — independent time constants when
 * the signal is rising vs. falling. Most common uses:
 *
 * - **Envelope follower for percussive audio** (rise=0.005, fall=0.2-0.6):
 *   feed a drum's `peak` port to `input`, get a sustained pulse on `value`
 *   that drives shape scale, brightness, color, etc. Without it, drum
 *   triggers flash too briefly to register visually.
 * - **Slew limiter** (rise=fall=0.05): smooth abrupt parameter jumps so
 *   stepped signals (e.g., from quantizers) feel continuous.
 * - **Pitch glide** (rise=fall=0.15): add portamento to discrete pitch
 *   changes for a synth-style legato.
 * - **Peak hold + slow decay** (rise=0, long fall): instantaneous attack
 *   then exponential decay — classic peak-meter behavior.
 *
 * @tip Use the `Envelope follower (snappy)` factory preset as a starting
 * point for audio-reactive visuals.
 * @see LFO, SampleHold, Envelope
 */
struct Smooth : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Smooth";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> rise_time{"rise_time", 0.1f, 0.0f, 10.0f};
    vivid::Param<float> fall_time{"fall_time", 0.1f, 0.0f, 10.0f};

    Smooth() {
        vivid::semantic_tag(rise_time, "time_seconds");
        vivid::semantic_shape(rise_time, "scalar");
        vivid::semantic_unit(rise_time, "s");

        vivid::semantic_tag(fall_time, "time_seconds");
        vivid::semantic_shape(fall_time, "scalar");
        vivid::semantic_unit(fall_time, "s");

        vivid::description(rise_time, "Smoothing time when the signal is rising, in seconds");
        vivid::description(fall_time, "Smoothing time when the signal is falling, in seconds");

        // Display as side-by-side knobs
        vivid::layout_row(rise_time, 2, 0);
        vivid::display_hint(rise_time, VIVID_DISPLAY_KNOB);
        vivid::layout_row(fall_time, 2, 1);
        vivid::display_hint(fall_time, VIVID_DISPLAY_KNOB);
    }

    ~Smooth() override;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rise_time);
        out.push_back(&fall_time);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void advance(float target, float dt) {
        if (first_frame_) {
            current_ = target;
            first_frame_ = false;
        } else {
            float tau = (target > current_) ? rise_time.value : fall_time.value;
            if (tau > 0.0001f) {
                float coeff = 1.0f - std::exp(-dt / tau);
                current_ += (target - current_) * coeff;
            } else {
                current_ = target;
            }
        }
    }

    void process_frame(const VividFrameContext* ctx) override {
        advance(ctx->input_values[0], static_cast<float>(ctx->delta_time));
        ctx->output_values[0] = current_;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

private:
    float current_ = 0.0f;
    bool first_frame_ = true;
    SmoothThumbState* thumb_state_ = nullptr;

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx);
};
