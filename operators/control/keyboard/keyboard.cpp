#include "operator_api/operator.h"
#include "operator_api/input_state.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"
#include "operator_api/draw_ui_helpers.h"

#include <cctype>
#include <cstring>

// Resolve a key name string to a GLFW key code without including GLFW headers.
// Returns -1 for unknown keys.
static int resolve_key(const std::string& key) {
    if (key.empty()) return -1;

    // Single letter A-Z (case-insensitive) → 65-90
    if (key.size() == 1) {
        char c = (char)std::toupper((unsigned char)key[0]);
        if (c >= 'A' && c <= 'Z') return (int)c;
        if (c >= '0' && c <= '9') return (int)c;
        return -1;
    }

    // Named keys (case-insensitive comparison via lowercase copy)
    // We compare directly since only a few entries exist.
    const char* s = key.c_str();
    auto eq = [&](const char* lit) {
        return strcasecmp(s, lit) == 0;
    };

    if (eq("space"))                    return 32;
    if (eq("escape") || eq("esc"))      return 256;
    if (eq("return") || eq("enter"))    return 257;
    if (eq("tab"))                      return 258;
    if (eq("backspace"))                return 259;
    if (eq("right"))                    return 262;
    if (eq("left"))                     return 263;
    if (eq("down"))                     return 264;
    if (eq("up"))                       return 265;

    return -1;
}
/**
 * @brief Queries keyboard input state for a specified key.
 *
 * Monitors a named key and outputs whether it is currently held, plus
 * single-frame pressed and released triggers. Also reports modifier states.
 *
 * @param key Key name: space, escape, return, arrow keys, or single character.
 * @see Mouse, MidiInput
 */
struct Keyboard : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Keyboard";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::TextValue> key{"key", "space"};

    Keyboard() {
        vivid::description(key, "Name of the key to monitor (e.g. space, escape, A, 1)");
    }

    bool key_held = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&key);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"held",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"pressed",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"released", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"shift",    VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"ctrl",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"alt",      VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        auto& d = const_cast<VividDrawAPI&>(ctx->draw);
        void* o = d.opaque;
        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        vivid::draw_plot::draw_thumb_background(d, o, w, h);
        vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, "KEY");

        // Key name from string param
        const char* key_name = (ctx->string_param_values && ctx->string_param_count > 0)
                               ? ctx->string_param_values[0] : "?";
        if (!key_name || !*key_name) key_name = "?";

        bool held = (ctx->output_count > 0) && ctx->output_values[0] > 0.5f;

        // Key cap button
        float cap_w = std::min(w - 24.0f, 80.0f);
        float cap_h = 36.0f;
        float cap_x = (w - cap_w) * 0.5f;
        float cap_y = 20.0f;

        VividColor cap_fill = held
            ? VividColor{0.35f, 0.55f, 0.80f, 0.7f}
            : VividColor{0.18f, 0.20f, 0.24f, 0.9f};
        VividColor cap_border = held
            ? VividColor{0.5f, 0.7f, 0.95f, 0.8f}
            : VividColor{0.30f, 0.33f, 0.38f, 0.7f};
        vivid::draw_ui::draw_panel(d, o, cap_x, cap_y, cap_w, cap_h, cap_fill, cap_border, 4.0f);

        VividColor text_color = held
            ? VividColor{1.0f, 1.0f, 1.0f, 1.0f}
            : VividColor{0.7f, 0.75f, 0.8f, 0.9f};
        vivid::draw_ui::draw_text_aligned(d, o, cap_x, cap_y + 8.0f, cap_w,
                                          key_name, text_color, 1.1f, 0.5f);

        // Modifier badges at bottom
        float badge_y = h - 18.0f;
        float badge_w = 16.0f;
        float badge_gap = 4.0f;
        float total_badge_w = 3 * badge_w + 2 * badge_gap;
        float badge_x = (w - total_badge_w) * 0.5f;

        const char* mod_labels[] = {"S", "C", "A"};
        for (int i = 0; i < 3; ++i) {
            bool active = (ctx->output_count > static_cast<uint32_t>(3 + i))
                         && ctx->output_values[3 + i] > 0.5f;
            VividColor mfill = active
                ? VividColor{0.35f, 0.55f, 0.80f, 0.6f}
                : VividColor{0.15f, 0.16f, 0.18f, 0.5f};
            VividColor mtxt = active
                ? VividColor{1.0f, 1.0f, 1.0f, 0.9f}
                : VividColor{0.35f, 0.38f, 0.42f, 0.6f};
            float mx = badge_x + i * (badge_w + badge_gap);
            vivid::draw_ui::draw_value_badge(d, o, mx, badge_y, badge_w, 14.0f,
                                             mod_labels[i], mfill, mtxt, 2.0f, 0.7f);
        }
    }

    void process_frame(const VividFrameContext* ctx) override {
        const VividInputState* input = vivid_input(ctx);
        if (!input) {
            for (int i = 0; i < 6; ++i) ctx->output_values[i] = 0.0f;
            return;
        }

        int target_key = resolve_key(key.str_value);

        bool pressed_this_frame  = false;
        bool released_this_frame = false;

        for (uint32_t i = 0; i < input->event_count; ++i) {
            const VividInputEvent& ev = input->events[i];
            if (ev.type != VIVID_INPUT_KEY) continue;
            if (target_key < 0 || ev.key != target_key) continue;

            if (ev.action == 1) {       // press
                key_held = true;
                pressed_this_frame = true;
            } else if (ev.action == 0) { // release
                key_held = false;
                released_this_frame = true;
            }
        }

        ctx->output_values[0] = key_held            ? 1.0f : 0.0f;
        ctx->output_values[1] = pressed_this_frame  ? 1.0f : 0.0f;
        ctx->output_values[2] = released_this_frame ? 1.0f : 0.0f;
        ctx->output_values[3] = (input->modifiers & 1) ? 1.0f : 0.0f; // shift
        ctx->output_values[4] = (input->modifiers & 2) ? 1.0f : 0.0f; // ctrl
        ctx->output_values[5] = (input->modifiers & 4) ? 1.0f : 0.0f; // alt
    }
};

VIVID_REGISTER(Keyboard)
VIVID_THUMBNAIL(Keyboard)
