#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"
#include "operator_api/draw_ui_helpers.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

struct StepCounterFr : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "StepCounterFr";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int> initial{"initial", 0, -1000000, 1000000};

    StepCounterFr() {
        vivid::semantic_tag(initial, "index");
        vivid::semantic_shape(initial, "int");
        vivid::description(initial, "Starting count value and reset target");
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        auto& d = const_cast<VividDrawAPI&>(ctx->draw);
        void* o = d.opaque;
        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        vivid::draw_plot::draw_thumb_background(d, o, w, h);
        vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, "STEP");

        int index = (ctx->output_count > 0) ? static_cast<int>(ctx->output_values[0]) : 0;
        char idx_str[16];
        std::snprintf(idx_str, sizeof(idx_str), "%d", index);

        // Large centered index value
        vivid::draw_ui::draw_text_aligned(d, o, 0.0f, 18.0f, w,
                                          idx_str, {0.75f, 0.85f, 0.95f, 0.95f}, 1.6f, 0.5f);

        // Step grid at bottom
        constexpr int kGridSize = 8;
        float grid_x = 8.0f;
        float grid_y = h - 22.0f;
        float grid_w = w - 16.0f;
        float grid_h = 14.0f;
        int current = ((index % kGridSize) + kGridSize) % kGridSize;

        vivid::draw_plot::draw_step_grid(d, o, grid_x, grid_y, grid_w, grid_h,
                                         kGridSize, nullptr, current,
                                         {0.35f, 0.55f, 0.80f, 0.7f},
                                         {0.15f, 0.17f, 0.20f, 0.6f},
                                         {0.5f, 0.7f, 0.95f, 0.8f},
                                         2.0f, 2.0f);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&initial);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"trigger", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"modulus", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"reset", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"index", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"wrapped", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (!initialized_) {
            step_ = initial.int_value();
            initialized_ = true;
        }

        float trigger = ctx->input_values[0];
        int modulus = std::max(1, static_cast<int>(std::floor(ctx->input_values[1])));
        bool reset = ctx->input_values[2] > 0.5f;

        bool wrapped = false;
        if (reset) {
            step_ = initial.int_value();
            if (step_ >= modulus || step_ < 0) {
                step_ = ((step_ % modulus) + modulus) % modulus;
                wrapped = true;
            }
        } else if (trigger > 0.5f && prev_trigger_ <= 0.5f) {
            step_++;
            if (step_ >= modulus) {
                step_ = 0;
                wrapped = true;
            }
        }

        prev_trigger_ = trigger;
        ctx->output_values[0] = static_cast<float>(step_);
        ctx->output_values[1] = wrapped ? 1.0f : 0.0f;
    }

private:
    float prev_trigger_ = 0.0f;
    bool initialized_ = false;
    int step_ = 0;
};

VIVID_REGISTER(StepCounterFr)
VIVID_THUMBNAIL(StepCounterFr)
