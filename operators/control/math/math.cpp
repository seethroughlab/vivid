#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include <cmath>
#include <algorithm>

/**
 * @brief Binary math operation on two control signals.
 *
 * Performs add, multiply, min, or max on inputs A and B. Chain multiple
 * Math operators for complex expressions.
 *
 * @see Logic, Macro, Quantizer
 */
struct Math : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Math";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> operation{"operation", 0, {"add", "multiply", "min", "max"}};

    Math() {
        vivid::description(operation, "Binary operation applied to inputs A and B");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&operation);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"b",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"result", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    float compute(float a, float b, int op) const {
        switch (op) {
            case 0: return a + b;
            case 1: return a * b;
            case 2: return std::min(a, b);
            case 3: return std::max(a, b);
        }
        return 0.0f;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        const auto& d = ctx->draw;
        void* o = d.opaque;

        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        int op = (ctx->param_count > 0) ? std::clamp(static_cast<int>(ctx->param_values[0]), 0, 3) : 0;
        float result = (ctx->output_count > 0) ? ctx->output_values[0] : 0.0f;

        // Dark background
        d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

        VividColor glyph = {0.63f, 0.78f, 0.94f, 0.9f};
        float cx = w * 0.5f;
        float cy = h * 0.38f;
        float sz = 16.0f;
        float th = 2.5f;

        // Draw symbol with lines
        switch (op) {
            case 0: // + (add)
                d.draw_line(o, cx - sz, cy, cx + sz, cy, th, glyph);
                d.draw_line(o, cx, cy - sz, cx, cy + sz, th, glyph);
                break;
            case 1: // × (multiply)
                d.draw_line(o, cx - sz * 0.7f, cy - sz * 0.7f, cx + sz * 0.7f, cy + sz * 0.7f, th, glyph);
                d.draw_line(o, cx + sz * 0.7f, cy - sz * 0.7f, cx - sz * 0.7f, cy + sz * 0.7f, th, glyph);
                break;
            case 2: // ∧ (min — down chevron)
                d.draw_line(o, cx - sz, cy - sz * 0.5f, cx, cy + sz * 0.5f, th, glyph);
                d.draw_line(o, cx, cy + sz * 0.5f, cx + sz, cy - sz * 0.5f, th, glyph);
                break;
            case 3: // ∨ (max — up chevron)
                d.draw_line(o, cx - sz, cy + sz * 0.5f, cx, cy - sz * 0.5f, th, glyph);
                d.draw_line(o, cx, cy - sz * 0.5f, cx + sz, cy + sz * 0.5f, th, glyph);
                break;
        }

        // Operation name
        static const char* kNames[] = {"Add", "Multiply", "Min", "Max"};
        float tw = d.text_width(o, kNames[op], 0.85f);
        d.draw_text(o, cx - tw * 0.5f, h - 26, kNames[op], {0.5f, 0.55f, 0.6f, 0.8f}, 0.85f);

        // Live result value
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.2f", result);
        float rw = d.text_width(o, buf, 0.75f);
        d.draw_text(o, cx - rw * 0.5f, h - 13, buf, {0.45f, 0.55f, 0.65f, 0.6f}, 0.75f);
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = compute(ctx->input_values[0], ctx->input_values[1],
                                        operation.int_value());
    }

};

VIVID_REGISTER(Math)
VIVID_THUMBNAIL(Math)
