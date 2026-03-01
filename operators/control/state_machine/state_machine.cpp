#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

struct StateMachine : vivid::OperatorBase {
    static constexpr const char* kName   = "StateMachine";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    // Parameters (16 total)
    vivid::Param<int>   states       {"states",        4, 1, 8};
    vivid::Param<int>   transition   {"transition",    0, {"sequential", "manual", "threshold"}};
    vivid::Param<bool>  quantize     {"quantize",      true};
    vivid::Param<bool>  loop         {"loop",          true};
    vivid::Param<int>   bars_per_beat{"bars_per_beat", 4, 1, 32};
    vivid::Param<float> threshold    {"threshold",     0.5f, 0.0f, 1.0f};
    vivid::Param<int>   xfade_mode   {"xfade_mode",   0, {"cut", "crossfade", "morph"}};
    vivid::Param<float> xfade_bars   {"xfade_bars",   0.0f, 0.0f, 32.0f};
    vivid::Param<float> dur_0       {"dur_0", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_1       {"dur_1", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_2       {"dur_2", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_3       {"dur_3", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_4       {"dur_4", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_5       {"dur_5", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_6       {"dur_6", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_7       {"dur_7", 4.0f, 0.0f, 256.0f};

    // State variables
    int    current_state_   = 0;
    int    bar_count_       = 0;
    int    beat_count_      = 0;
    float  prev_phase_      = 0.0f;
    bool   prev_trigger_    = false;
    bool   prev_reset_      = false;
    float  prev_signal_     = 0.0f;
    bool   pending_advance_ = false;
    bool   finished_        = false;

    // Crossfade state
    bool   xfade_active_    = false;
    float  xfade_start_bar_ = 0.0f;  // bar position when crossfade started

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&states);         // 0
        out.push_back(&transition);     // 1
        out.push_back(&quantize);       // 2
        out.push_back(&loop);           // 3
        out.push_back(&bars_per_beat);  // 4
        out.push_back(&threshold);      // 5
        out.push_back(&xfade_mode);     // 6
        out.push_back(&xfade_bars);     // 7
        out.push_back(&dur_0);          // 8
        out.push_back(&dur_1);          // 9
        out.push_back(&dur_2);          // 10
        out.push_back(&dur_3);          // 11
        out.push_back(&dur_4);          // 12
        out.push_back(&dur_5);          // 13
        out.push_back(&dur_6);          // 14
        out.push_back(&dur_7);          // 15
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Inputs (indexed 0-3)
        out.push_back({"beat_phase", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"trigger",    VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"reset",      VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"signal",     VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        // Outputs (indexed 0-5)
        out.push_back({"state",    VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"progress", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"trigger",  VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"bar",      VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"beat",     VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"xfade",    VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        // 1. Read inputs
        float beat_phase = ctx->input_values[0];
        float trigger_in = ctx->input_values[1];
        float reset_in   = ctx->input_values[2];
        float signal_in  = ctx->input_values[3];

        // 2. Read params
        int   num_states     = static_cast<int>(ctx->param_values[0]);
        int   trans_mode     = static_cast<int>(ctx->param_values[1]);
        bool  quant          = ctx->param_values[2] > 0.5f;
        bool  do_loop        = ctx->param_values[3] > 0.5f;
        int   bpb            = static_cast<int>(ctx->param_values[4]);
        float thresh         = ctx->param_values[5];
        int   xf_mode        = static_cast<int>(ctx->param_values[6]);
        float xf_bars        = ctx->param_values[7];
        // dur_0..dur_7 at param indices 8..15

        num_states = std::max(1, std::min(8, num_states));
        bpb = std::max(1, bpb);

        // Duration for current state
        auto get_dur = [&](int s) -> float {
            return ctx->param_values[8 + std::max(0, std::min(7, s))];
        };

        // Edge detection helpers
        bool trigger_on = trigger_in > 0.5f;
        bool trigger_edge = trigger_on && !prev_trigger_;
        prev_trigger_ = trigger_on;

        bool reset_on = reset_in > 0.5f;
        bool reset_edge = reset_on && !prev_reset_;
        prev_reset_ = reset_on;

        // 3. Reset detection
        bool did_reset = false;
        if (reset_edge) {
            current_state_ = 0;
            bar_count_ = 0;
            beat_count_ = 0;
            pending_advance_ = false;
            finished_ = false;
            prev_phase_ = beat_phase;
            did_reset = true;
        }

        // 4. Beat counting via phase-wrap
        bool new_bar = false;
        if (!did_reset) {
            float delta = beat_phase - prev_phase_;
            if (delta < -0.5f) {
                beat_count_++;
            }
            prev_phase_ = beat_phase;

            // Bar counting
            while (beat_count_ >= bpb) {
                beat_count_ -= bpb;
                bar_count_++;
                new_bar = true;
            }
        }

        // 5. Advance decision
        bool should_advance = false;

        if (!finished_ && !did_reset) {
            float dur = get_dur(current_state_);

            switch (trans_mode) {
            case 0: // sequential
                if (dur > 0.0f && bar_count_ >= static_cast<int>(dur)) {
                    should_advance = true;
                }
                break;
            case 1: // manual
                if (trigger_edge) {
                    should_advance = true;
                }
                break;
            case 2: // threshold
            {
                bool crossed = (prev_signal_ < thresh && signal_in >= thresh) ||
                               (prev_signal_ >= thresh && signal_in < thresh);
                if (crossed) {
                    should_advance = true;
                }
                prev_signal_ = signal_in;
                break;
            }
            }

            // Duration-0 override: manual trigger always works
            if (dur == 0.0f && trigger_edge) {
                should_advance = true;
            }

            // 6. Quantization
            if (should_advance && quant && !new_bar) {
                pending_advance_ = true;
                should_advance = false;
            }

            if (pending_advance_ && new_bar) {
                pending_advance_ = false;
                should_advance = true;
            }
        }

        // 7. State transition
        bool transition_fired = false;
        if (should_advance) {
            int next = current_state_ + 1;
            if (next >= num_states) {
                if (do_loop) {
                    next = 0;
                } else {
                    finished_ = true;
                    next = current_state_;
                }
            }
            if (next != current_state_) {
                current_state_ = next;
                bar_count_ = 0;
                beat_count_ = 0;
                transition_fired = true;
            }
        }

        // 8. Crossfade logic
        if (transition_fired && xf_mode > 0 && xf_bars > 0.0f) {
            xfade_active_ = true;
            xfade_start_bar_ = static_cast<float>(bar_count_)
                             + static_cast<float>(beat_count_) / static_cast<float>(bpb)
                             + beat_phase / static_cast<float>(bpb);
        }

        float xfade_out = 0.0f;
        if (xfade_active_) {
            float current_bar_pos = static_cast<float>(bar_count_)
                                  + static_cast<float>(beat_count_) / static_cast<float>(bpb)
                                  + beat_phase / static_cast<float>(bpb);
            float elapsed = current_bar_pos - xfade_start_bar_;
            float t = (xf_bars > 0.0f) ? (elapsed / xf_bars) : 1.0f;
            t = std::max(0.0f, std::min(1.0f, t));
            if (xf_mode == 2) {
                // Morph: smoothstep (3t^2 - 2t^3)
                t = t * t * (3.0f - 2.0f * t);
            }
            xfade_out = t;
            if (t >= 1.0f) {
                xfade_active_ = false;
            }
        }

        // 9. Compute outputs
        float state_out = static_cast<float>(current_state_);
        float bar_out   = static_cast<float>(bar_count_);

        float dur = get_dur(current_state_);
        float progress_out = 0.0f;
        if (dur > 0.0f) {
            float fractional_bar = static_cast<float>(beat_count_) / static_cast<float>(bpb)
                                 + beat_phase / static_cast<float>(bpb);
            progress_out = (static_cast<float>(bar_count_) + fractional_bar) / dur;
            progress_out = std::max(0.0f, std::min(1.0f, progress_out));
        }

        float beat_out = beat_phase;
        float trigger_out = transition_fired ? 1.0f : 0.0f;

        // 10. Write outputs
        ctx->output_values[0] = state_out;
        ctx->output_values[1] = progress_out;
        ctx->output_values[2] = trigger_out;
        ctx->output_values[3] = bar_out;
        ctx->output_values[4] = beat_out;
        ctx->output_values[5] = xfade_out;
    }
};

VIVID_REGISTER(StateMachine)
