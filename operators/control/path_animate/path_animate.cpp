#include "operator_api/operator.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
/**
 * @brief Cubic Bezier path evaluator with easing and loop modes.
 *
 * Defines a 2D cubic Bezier curve via 4 control points and evaluates
 * position along it over time or from an external phase input. Outputs
 * X, Y, tangent angle, and progress.
 *
 * @param loop_mode Loop, Ping-Pong, Once, or Once+Hold.
 * @param easing Easing curve: Linear, EaseIn, EaseOut, EaseInOut, Bounce.
 * @see LFO, Mouse
 */
struct PathAnimate : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "PathAnimate";
    static constexpr bool kTimeDependent = true;

    // Bezier control points
    vivid::Param<float> p0_x {"p0_x", 0.0f, -2.0f, 2.0f};
    vivid::Param<float> p0_y {"p0_y", 0.0f, -2.0f, 2.0f};
    vivid::Param<float> p1_x {"p1_x", 0.3f, -2.0f, 2.0f};
    vivid::Param<float> p1_y {"p1_y", 0.5f, -2.0f, 2.0f};
    vivid::Param<float> p2_x {"p2_x", 0.7f, -2.0f, 2.0f};
    vivid::Param<float> p2_y {"p2_y", 0.5f, -2.0f, 2.0f};
    vivid::Param<float> p3_x {"p3_x", 1.0f, -2.0f, 2.0f};
    vivid::Param<float> p3_y {"p3_y", 0.0f, -2.0f, 2.0f};

    vivid::Param<float> speed {"speed", 1.0f, 0.01f, 20.0f};

    vivid::Param<int> loop_mode {"loop_mode", 0, {"Loop", "Ping-Pong", "Once", "Once+Hold"}};
    vivid::Param<int> easing    {"easing",    0, {"Linear", "Ease-In", "Ease-Out", "Ease-In-Out", "SmoothStep"}};

    double free_phase_ = 0.0;
    bool   finished_   = false;  // for Once modes

    PathAnimate() {
        vivid::semantic_tag(p0_x, "position_xy");
        vivid::semantic_shape(p0_x, "scalar");
        vivid::semantic_tag(p0_y, "position_xy");
        vivid::semantic_shape(p0_y, "scalar");
        vivid::display_hint(p0_x, VIVID_DISPLAY_XY_PAD);
        vivid::display_hint(p0_y, VIVID_DISPLAY_XY_PAD);
        vivid::param_widget(p0_x, "seethroughlab.vivid.xy_pad_group", 8);
        vivid::description(p0_x, "X position of the first control point (curve start)");
        vivid::description(p0_y, "Y position of the first control point (curve start)");

        vivid::semantic_tag(p1_x, "position_xy");
        vivid::semantic_shape(p1_x, "scalar");
        vivid::semantic_tag(p1_y, "position_xy");
        vivid::semantic_shape(p1_y, "scalar");
        vivid::display_hint(p1_x, VIVID_DISPLAY_XY_PAD);
        vivid::display_hint(p1_y, VIVID_DISPLAY_XY_PAD);
        vivid::description(p1_x, "X position of the second control point (influences departure)");
        vivid::description(p1_y, "Y position of the second control point (influences departure)");

        vivid::semantic_tag(p2_x, "position_xy");
        vivid::semantic_shape(p2_x, "scalar");
        vivid::semantic_tag(p2_y, "position_xy");
        vivid::semantic_shape(p2_y, "scalar");
        vivid::display_hint(p2_x, VIVID_DISPLAY_XY_PAD);
        vivid::display_hint(p2_y, VIVID_DISPLAY_XY_PAD);
        vivid::description(p2_x, "X position of the third control point (influences arrival)");
        vivid::description(p2_y, "Y position of the third control point (influences arrival)");

        vivid::semantic_tag(p3_x, "position_xy");
        vivid::semantic_shape(p3_x, "scalar");
        vivid::semantic_tag(p3_y, "position_xy");
        vivid::semantic_shape(p3_y, "scalar");
        vivid::display_hint(p3_x, VIVID_DISPLAY_XY_PAD);
        vivid::display_hint(p3_y, VIVID_DISPLAY_XY_PAD);
        vivid::description(p3_x, "X position of the fourth control point (curve end)");
        vivid::description(p3_y, "Y position of the fourth control point (curve end)");

        vivid::semantic_tag(speed, "frequency_hz");
        vivid::semantic_shape(speed, "scalar");
        vivid::semantic_unit(speed, "Hz");
        vivid::description(speed, "Playback speed of the animation in cycles per second");
        vivid::description(loop_mode, "How the animation repeats: Loop, Ping-Pong, Once, or Once+Hold");
        vivid::description(easing, "Easing curve applied to the progress along the path");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&p0_x); out.push_back(&p0_y);
        out.push_back(&p1_x); out.push_back(&p1_y);
        out.push_back(&p2_x); out.push_back(&p2_y);
        out.push_back(&p3_x); out.push_back(&p3_y);
        out.push_back(&speed);
        out.push_back(&loop_mode);
        out.push_back(&easing);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"phase_in", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"trigger",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"x",        VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"y",        VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"angle",    VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"progress", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    // -------------------------------------------------------------------------
    // Easing functions
    // -------------------------------------------------------------------------

    static double apply_easing(double t, int mode) {
        switch (mode) {
            case 0: // Linear
                return t;
            case 1: // Ease-In (quadratic)
                return t * t;
            case 2: // Ease-Out (quadratic)
                return t * (2.0 - t);
            case 3: // Ease-In-Out (quadratic)
                return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
            case 4: // SmoothStep
                return t * t * (3.0 - 2.0 * t);
            default:
                return t;
        }
    }

    // -------------------------------------------------------------------------
    // Cubic bezier evaluation
    // -------------------------------------------------------------------------

    static double bezier(double t, double p0, double p1, double p2, double p3) {
        double u = 1.0 - t;
        return u * u * u * p0
             + 3.0 * u * u * t * p1
             + 3.0 * u * t * t * p2
             + t * t * t * p3;
    }

    // Derivative of cubic bezier
    static double bezier_deriv(double t, double p0, double p1, double p2, double p3) {
        double u = 1.0 - t;
        return 3.0 * u * u * (p1 - p0)
             + 6.0 * u * t * (p2 - p1)
             + 3.0 * t * t * (p3 - p2);
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values, ctx->delta_time, ctx->output_values);
    }


private:
    void compute(const float* input_values, double delta_time, float* output_values) {
        float phase_in = input_values[0];
        float trigger  = input_values[1];

        // Trigger resets phase
        if (trigger > 0.5f) {
            free_phase_ = 0.0;
            finished_ = false;
        }

        double raw_phase;
        if (phase_in != 0.0f) {
            // Externally driven
            raw_phase = static_cast<double>(phase_in);
        } else {
            // Free-running accumulation
            free_phase_ += delta_time * static_cast<double>(speed.value);
            raw_phase = free_phase_;
        }

        // Apply loop mode to get t in [0, 1]
        double t = 0.0;
        int mode = loop_mode.int_value();
        switch (mode) {
            case 0: // Loop
                t = std::fmod(raw_phase, 1.0);
                if (t < 0.0) t += 1.0;
                break;
            case 1: { // Ping-Pong
                double cycle = std::fmod(raw_phase, 2.0);
                if (cycle < 0.0) cycle += 2.0;
                t = cycle <= 1.0 ? cycle : 2.0 - cycle;
                break;
            }
            case 2: // Once (clamp then go to 0)
                if (raw_phase >= 1.0) {
                    t = 0.0;
                    finished_ = true;
                } else {
                    t = raw_phase < 0.0 ? 0.0 : raw_phase;
                }
                break;
            case 3: // Once+Hold (clamp at 1)
                t = raw_phase < 0.0 ? 0.0 : (raw_phase > 1.0 ? 1.0 : raw_phase);
                break;
        }

        // Apply easing
        double eased_t = apply_easing(t, easing.int_value());

        // Evaluate bezier
        double bx = bezier(eased_t, p0_x.value, p1_x.value, p2_x.value, p3_x.value);
        double by = bezier(eased_t, p0_y.value, p1_y.value, p2_y.value, p3_y.value);

        // Tangent angle from derivative
        double dx = bezier_deriv(eased_t, p0_x.value, p1_x.value, p2_x.value, p3_x.value);
        double dy = bezier_deriv(eased_t, p0_y.value, p1_y.value, p2_y.value, p3_y.value);
        double angle = std::atan2(dy, dx);

        output_values[0] = static_cast<float>(bx);       // x
        output_values[1] = static_cast<float>(by);       // y
        output_values[2] = static_cast<float>(angle);    // angle (radians)
        output_values[3] = static_cast<float>(t);        // progress (pre-easing)
    }
};

VIVID_DEFINE_OP(PathAnimate) {
    name = "PathAnimate";
    keywords = {"path", "bezier", "animate", "easing", "motion", "keyframe", "spline"};
    summary = "Animates x/y/angle along a cubic Bezier path with easing modes.";
}

VIVID_REGISTER_V2(PathAnimate)
