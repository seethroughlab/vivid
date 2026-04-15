#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"
#include "operator_api/draw_ui_helpers.h"

#include <string>
/**
 * @brief Extracts the filename from a full file path string.
 *
 * @see FolderList, StringSelect
 */
struct Basename : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "Basename";
    static constexpr bool kTimeDependent = false;

    std::string result_;

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        auto& d = const_cast<VividDrawAPI&>(ctx->draw);
        void* o = d.opaque;
        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        vivid::draw_plot::draw_thumb_background(d, o, w, h);

        // Stylized path segments: dim rects representing path components
        float seg_y = h * 0.25f;
        float seg_h = 10.0f;
        float seg_gap = 3.0f;
        VividColor dim_seg = {0.20f, 0.22f, 0.26f, 0.6f};
        VividColor bright_seg = {0.35f, 0.55f, 0.80f, 0.8f};

        // Three dim path segments + one bright "name" segment
        float total_w = w - 24.0f;
        float dim_w = total_w * 0.15f;
        float name_w = total_w - 3 * (dim_w + seg_gap);
        float sx = 12.0f;

        for (int i = 0; i < 3; ++i) {
            if (d.draw_rounded_rect)
                d.draw_rounded_rect(o, sx, seg_y, dim_w, seg_h, 2.0f, dim_seg);
            else if (d.draw_rect)
                d.draw_rect(o, sx, seg_y, dim_w, seg_h, dim_seg);
            // Separator slash
            if (d.draw_text)
                d.draw_text(o, sx + dim_w + 0.5f, seg_y - 1.0f, "/",
                            {0.30f, 0.32f, 0.36f, 0.5f}, 0.7f);
            sx += dim_w + seg_gap;
        }
        if (d.draw_rounded_rect)
            d.draw_rounded_rect(o, sx, seg_y, name_w, seg_h, 2.0f, bright_seg);
        else if (d.draw_rect)
            d.draw_rect(o, sx, seg_y, name_w, seg_h, bright_seg);

        // Show the extracted name if available, otherwise the static label
        if (!result_.empty()) {
            vivid::draw_ui::draw_clipped_text_box(d, o,
                6.0f, h * 0.55f, w - 12.0f, 20.0f,
                result_.c_str(),
                {0.14f, 0.15f, 0.18f, 0.8f},
                {0.75f, 0.85f, 0.95f, 0.95f},
                3.0f, 0.85f, 0.0f, 4.0f);
        } else {
            vivid::draw_ui::draw_text_aligned(d, o, 0.0f, h * 0.55f, w,
                                              "path  ->  name",
                                              {0.45f, 0.50f, 0.58f, 0.7f}, 0.8f, 0.5f);
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"path", VIVID_PORT_STRING, VIVID_PORT_INPUT});
        out.push_back({"name", VIVID_PORT_STRING, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        result_.clear();

        if (ctx->input_string_values) {
            const char* s = ctx->input_string_values[0];
            if (s && *s) {
                std::string path(s);
                auto slash = path.rfind('/');
                if (slash != std::string::npos)
                    result_ = path.substr(slash + 1);
                else
                    result_ = path;
            }
        }

        if (ctx->output_string_values) {
            ctx->output_string_values[0] = result_.c_str();
        }
    }
};

VIVID_REGISTER(Basename)
VIVID_THUMBNAIL(Basename)
