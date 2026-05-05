#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"

/**
 * @brief Boolean logic gate operating on two control signals.
 *
 * Applies AND, OR, XOR, NOT, NAND, or NOR to two inputs (threshold
 * > 0.5 = true). NOT only uses input A.
 *
 * @see Math, Gate
 */
struct Logic : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Logic";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> operation{"operation", 0, {"AND", "OR", "XOR", "NOT", "NAND", "NOR"}};

    Logic() {
        vivid::description(operation, "Boolean operation to apply (NOT uses only input A)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&operation);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"b",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"result", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    float compute(float a_val, float b_val, int op) const {
        bool a = a_val > 0.5f;
        bool b = b_val > 0.5f;
        bool result = false;
        switch (op) {
            case 0: result = a && b;     break;  // AND
            case 1: result = a || b;     break;  // OR
            case 2: result = a != b;     break;  // XOR
            case 3: result = !a;         break;  // NOT
            case 4: result = !(a && b);  break;  // NAND
            case 5: result = !(a || b);  break;  // NOR
        }
        return result ? 1.0f : 0.0f;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        const auto& d = ctx->draw;
        void* o = d.opaque;

        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        int op = (ctx->param_count > 0) ? std::clamp(static_cast<int>(ctx->param_values[0]), 0, 5) : 0;
        bool output_on = (ctx->output_count > 0) && (ctx->output_values[0] > 0.5f);
        bool is_inverted = (op == 3 || op == 4 || op == 5); // NOT, NAND, NOR
        bool is_not = (op == 3);

        // Dark background
        d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

        VividColor on_col  = {0.31f, 0.75f, 0.47f, 0.9f};
        VividColor off_col = {0.47f, 0.55f, 0.67f, 0.9f};
        VividColor gate_col = output_on ? on_col : off_col;
        VividColor dim = {0.3f, 0.33f, 0.36f, 0.5f};

        // Gate body — centered rounded rect
        float body_w = 40.0f;
        float body_h = 36.0f;
        float bx = w * 0.5f - body_w * 0.5f;
        float by = h * 0.5f - body_h * 0.5f - 6.0f;
        d.draw_rounded_rect(o, bx, by, body_w, body_h, 4.0f, {gate_col.r * 0.3f, gate_col.g * 0.3f, gate_col.b * 0.3f, 0.6f});
        // Gate outline
        d.draw_line(o, bx, by, bx + body_w, by, 1.5f, gate_col);
        d.draw_line(o, bx, by + body_h, bx + body_w, by + body_h, 1.5f, gate_col);
        d.draw_line(o, bx, by, bx, by + body_h, 1.5f, gate_col);
        d.draw_line(o, bx + body_w, by, bx + body_w, by + body_h, 1.5f, gate_col);

        // Inversion bubble (small circle outline on right side)
        if (is_inverted) {
            float bub_x = bx + body_w + 3.0f;
            float bub_y = by + body_h * 0.5f;
            float bub_r = 4.0f;
            d.draw_rounded_rect(o, bub_x - bub_r, bub_y - bub_r, bub_r * 2, bub_r * 2, bub_r, gate_col);
        }

        // Input dots (left side)
        float dot_r = 3.0f;
        float in_x = bx - 8.0f;
        // Input A
        d.draw_rounded_rect(o, in_x - dot_r, by + body_h * 0.3f - dot_r, dot_r * 2, dot_r * 2, dot_r, dim);
        // Input B (skip for NOT)
        if (!is_not) {
            d.draw_rounded_rect(o, in_x - dot_r, by + body_h * 0.7f - dot_r, dot_r * 2, dot_r * 2, dot_r, dim);
        }

        // Output dot (right side)
        float out_x = is_inverted ? (bx + body_w + 12.0f) : (bx + body_w + 5.0f);
        float out_y = by + body_h * 0.5f;
        VividColor out_dot_col = output_on ? on_col : dim;
        d.draw_rounded_rect(o, out_x - dot_r, out_y - dot_r, dot_r * 2, dot_r * 2, dot_r, out_dot_col);

        // Input wires
        d.draw_line(o, in_x + dot_r, by + body_h * 0.3f, bx, by + body_h * 0.3f, 1.0f, dim);
        if (!is_not) {
            d.draw_line(o, in_x + dot_r, by + body_h * 0.7f, bx, by + body_h * 0.7f, 1.0f, dim);
        }
        // Output wire
        float wire_start = is_inverted ? (bx + body_w + 7.0f) : (bx + body_w);
        d.draw_line(o, wire_start, out_y, out_x - dot_r, out_y, 1.0f, dim);

        // Gate name text
        static const char* kNames[] = {"AND", "OR", "XOR", "NOT", "NAND", "NOR"};
        float tw = d.text_width(o, kNames[op], 0.85f);
        d.draw_text(o, w * 0.5f - tw * 0.5f, h - 18, kNames[op], {0.5f, 0.55f, 0.6f, 0.8f}, 0.85f);

        // Result indicator
        const char* state = output_on ? "1" : "0";
        float sw = d.text_width(o, state, 0.8f);
        d.draw_text(o, w * 0.5f - sw * 0.5f, by + body_h * 0.5f - 5.0f, state, gate_col, 0.8f);
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = compute(ctx->input_values[0], ctx->input_values[1],
                                        operation.int_value());
    }

};

VIVID_DEFINE_OP(Logic) {
}

VIVID_THUMBNAIL(Logic)
