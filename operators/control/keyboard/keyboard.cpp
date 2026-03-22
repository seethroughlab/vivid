#include "operator_api/operator.h"
#include "operator_api/input_state.h"

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

struct Keyboard : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "Keyboard";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::TextValue> key{"key", "space"};

    bool key_held = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&key);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"held",     VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"pressed",  VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"released", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"shift",    VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"ctrl",     VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"alt",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
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
