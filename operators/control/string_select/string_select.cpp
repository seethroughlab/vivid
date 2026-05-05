#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"
#include "operator_api/draw_ui_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
/**
 * @brief Selects a string from an input string lane array by index.
 *
 * Picks one string from an input string lane array at the given index.
 * Optional wrap mode cycles the index; otherwise it clamps.
 *
 * @see FolderList, Basename, Stack
 */
struct StringSelect : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "StringSelect";
    static constexpr bool kTimeDependent = false;

    vivid::Param<bool> wrap{"wrap", true};

    StringSelect() {
        vivid::semantic_tag(wrap, "enabled");
        vivid::semantic_shape(wrap, "bool");
        vivid::description(wrap, "Wrap index around the list instead of clamping");
    }

    std::string selected_;

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        auto& d = const_cast<VividDrawAPI&>(ctx->draw);
        void* o = d.opaque;
        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        vivid::draw_plot::draw_thumb_background(d, o, w, h);
        vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, "SEL");

        // Wrap badge top-right
        bool wrap_on = (ctx->param_count > 0) && ctx->param_values[0] > 0.5f;
        if (wrap_on) {
            vivid::draw_plot::draw_thumb_value(d, o, w - 38.0f, 4.0f, 32.0f, "WRAP",
                                               {0.7f, 0.55f, 0.35f, 0.85f}, 0.7f);
        }

        // Selected string (accessed directly from operator member)
        bool valid = (ctx->output_count > 1) && ctx->output_values[1] > 0.5f;
        if (!selected_.empty()) {
            // Extract just the filename portion for display
            std::string display = selected_;
            auto slash = display.rfind('/');
            if (slash != std::string::npos)
                display = display.substr(slash + 1);

            vivid::draw_ui::draw_clipped_text_box(d, o,
                6.0f, 24.0f, w - 12.0f, 20.0f,
                display.c_str(),
                {0.14f, 0.15f, 0.18f, 0.8f},
                {0.75f, 0.85f, 0.95f, 0.95f},
                3.0f, 0.85f, 0.0f, 4.0f);
        } else {
            vivid::draw_ui::draw_text_aligned(d, o, 0.0f, h * 0.32f, w,
                                              "---", {0.40f, 0.42f, 0.46f, 0.6f}, 0.9f, 0.5f);
        }

        // Index badge + valid indicator at bottom
        int resolved = (ctx->output_count > 2) ? static_cast<int>(ctx->output_values[2]) : 0;
        char idx_str[16];
        std::snprintf(idx_str, sizeof(idx_str), "#%d", resolved);

        VividColor valid_color = valid
            ? VividColor{0.35f, 0.75f, 0.45f, 0.8f}
            : VividColor{0.40f, 0.42f, 0.46f, 0.4f};
        float badge_y = h - 22.0f;
        vivid::draw_ui::draw_value_badge(d, o, 6.0f, badge_y, 32.0f, 16.0f,
                                         idx_str, {0.14f, 0.15f, 0.18f, 0.7f},
                                         {0.55f, 0.62f, 0.72f, 0.85f}, 3.0f, 0.75f);

        // Small valid dot
        float dot_size = 6.0f;
        float dot_x = 44.0f;
        float dot_y = badge_y + 5.0f;
        if (d.draw_rounded_rect) {
            d.draw_rounded_rect(o, dot_x, dot_y, dot_size, dot_size, dot_size * 0.5f, valid_color);
        } else if (d.draw_rect) {
            d.draw_rect(o, dot_x, dot_y, dot_size, dot_size, valid_color);
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&wrap);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"files", VIVID_PORT_STRING_LANES, VIVID_PORT_INPUT});
        out.push_back({"index", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"file", VIVID_PORT_STRING, VIVID_PORT_OUTPUT});
        out.push_back({"valid", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"resolved_index", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        selected_.clear();
        int resolved = -1;
        bool valid = false;

        const VividStringLaneView* in_lanes =
            ctx->input_string_lanes ? &ctx->input_string_lanes[0] : nullptr;
        const uint32_t n = (in_lanes && in_lanes->data) ? in_lanes->length : 0;
        if (n > 0) {
            int idx = static_cast<int>(std::floor(ctx->input_values[1]));
            if (wrap.bool_value()) {
                int m = static_cast<int>(n);
                idx = ((idx % m) + m) % m;
            } else {
                idx = std::max(0, std::min(idx, static_cast<int>(n) - 1));
            }
            const char* s = in_lanes->data[idx];
            if (s && *s) {
                selected_ = s;
                resolved = idx;
                valid = true;
            }
        }

        if (ctx->output_string_values) {
            ctx->output_string_values[0] = selected_.c_str();
        }
        if (ctx->output_values) {
            ctx->output_values[1] = valid ? 1.0f : 0.0f;
            ctx->output_values[2] = static_cast<float>(std::max(0, resolved));
        }
    }
};

VIVID_DEFINE_OP(StringSelect) {
}

VIVID_THUMBNAIL(StringSelect)
