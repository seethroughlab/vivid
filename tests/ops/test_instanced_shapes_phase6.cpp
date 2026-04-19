// Phase 6 of the operator-gaps plan: `Instanced Shapes` now accepts two
// additional lane-array inputs — `rotation` and `shape_idx` — so each
// instance can have its own rotation and shape primitive. This closes the
// remaining polyphonic-sprite gap identified in the arp-viz session.
//
// Verifies:
//   1. Total input ports = 7 (5 from Phase 5 + 2 from Phase 6).
//   2. `rotation` and `shape_idx` exist with LANE_ARRAY type and correct
//      semantic metadata.
//   3. Phase 5 lane ordinals (pos_x=0 … brightness=4) are preserved.
//   4. Phase 6 ordinals: rotation=5, shape_idx=6.
//   5. Existing static params still present with unchanged defaults.
//
// Partition 10: pure CPU, no GPU/audio/window. Render-level stability is
// covered by test_operator_sweep's headless GPU smoke.

#include "runtime/operators/operator_loader.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include "test_helpers.h"

namespace {

const VividPortDescriptor* find_port(const VividOperatorDescriptor& desc,
                                     const char* name, VividPortDirection dir) {
    for (uint32_t i = 0; i < desc.port_count; ++i) {
        const auto& p = desc.ports[i];
        if (p.direction == dir && std::strcmp(p.name, name) == 0) return &p;
    }
    return nullptr;
}

int input_port_index(const VividOperatorDescriptor& desc, const char* name) {
    int idx = 0;
    for (uint32_t i = 0; i < desc.port_count; ++i) {
        const auto& p = desc.ports[i];
        if (p.direction != VIVID_PORT_INPUT) continue;
        if (std::strcmp(p.name, name) == 0) return idx;
        ++idx;
    }
    return -1;
}

const VividParamDescriptor* find_param(const VividOperatorDescriptor& desc,
                                       const char* name) {
    for (uint32_t i = 0; i < desc.param_count; ++i) {
        if (std::strcmp(desc.params[i].name, name) == 0) return &desc.params[i];
    }
    return nullptr;
}

void assert_lane_input(const VividOperatorDescriptor& desc,
                       const char* port_name,
                       const char* expected_tag,
                       const char* expected_intent) {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "has input port '%s'", port_name);
    const auto* port = find_port(desc, port_name, VIVID_PORT_INPUT);
    check(port != nullptr, msg);
    if (!port) return;

    std::snprintf(msg, sizeof(msg), "%s type = LANE_ARRAY", port_name);
    check(port->type == VIVID_PORT_LANE_ARRAY, msg);

    std::snprintf(msg, sizeof(msg), "%s semantic_tag = %s", port_name, expected_tag);
    check(port->semantic_tag && std::strcmp(port->semantic_tag, expected_tag) == 0, msg);

    std::snprintf(msg, sizeof(msg), "%s semantic_shape = lane_array", port_name);
    check(port->semantic_shape && std::strcmp(port->semantic_shape, "lane_array") == 0, msg);

    std::snprintf(msg, sizeof(msg), "%s semantic_intent = %s", port_name, expected_intent);
    check(port->semantic_intent && std::strcmp(port->semantic_intent, expected_intent) == 0, msg);

    std::snprintf(msg, sizeof(msg), "%s has a description", port_name);
    check(port->description != nullptr && port->description[0] != '\0', msg);
}

void test_port_counts(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Port counts ---\n");
    int inputs = 0, outputs = 0;
    for (uint32_t i = 0; i < desc.port_count; ++i) {
        if (desc.ports[i].direction == VIVID_PORT_INPUT)  ++inputs;
        if (desc.ports[i].direction == VIVID_PORT_OUTPUT) ++outputs;
    }
    check(inputs  == 7, "7 input ports (5 from Phase 5 + 2 from Phase 6)");
    check(outputs == 1, "1 output port (drawable)");
}

void test_phase5_lanes_preserved(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Phase 5 lanes preserved (ordinals unchanged) ---\n");
    // The Phase-5 consumer pattern indexes ctx->input_lanes[0..4] in these
    // exact slots. Any reorder here would silently miswire existing graphs.
    check(input_port_index(desc, "pos_x")      == 0, "pos_x is input index 0");
    check(input_port_index(desc, "pos_y")      == 1, "pos_y is input index 1");
    check(input_port_index(desc, "size")       == 2, "size is input index 2");
    check(input_port_index(desc, "hue")        == 3, "hue is input index 3");
    check(input_port_index(desc, "brightness") == 4, "brightness is input index 4");

    check(find_port(desc, "pos_x",      VIVID_PORT_INPUT) != nullptr, "pos_x still present");
    check(find_port(desc, "pos_y",      VIVID_PORT_INPUT) != nullptr, "pos_y still present");
    check(find_port(desc, "size",       VIVID_PORT_INPUT) != nullptr, "size still present");
    check(find_port(desc, "hue",        VIVID_PORT_INPUT) != nullptr, "hue still present");
    check(find_port(desc, "brightness", VIVID_PORT_INPUT) != nullptr, "brightness still present");
}

void test_phase6_lanes(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Phase 6 new lane inputs ---\n");
    assert_lane_input(desc, "rotation",  "rotation_radians", "angle_turns");
    assert_lane_input(desc, "shape_idx", "enum_index",       "shape_selector");
}

void test_phase6_lane_ordering(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Phase 6 lane ordering ---\n");
    check(input_port_index(desc, "rotation")  == 5, "rotation is input index 5");
    check(input_port_index(desc, "shape_idx") == 6, "shape_idx is input index 6");
}

void test_existing_params_preserved(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Existing params preserved ---\n");

    auto* count = find_param(desc, "count");
    check(count != nullptr, "has count param");
    if (count) {
        check_float(count->default_value, 16.0f, "count default = 16");
        check_float(count->max_value,     64.0f, "count max still 64 (no cap raise in Phase 6)");
    }

    // `shape` remains as the global fallback when the shape_idx lane is not
    // connected. Its choice list defines the valid range for shape_idx [0,5].
    auto* shape = find_param(desc, "shape");
    check(shape != nullptr, "has shape param (still the global fallback)");
    if (shape) {
        check(shape->choice_count == 6, "shape still has 6 choices");
    }

    check(find_param(desc, "base_size")         != nullptr, "has base_size");
    check(find_param(desc, "softness")          != nullptr, "has softness");
    check(find_param(desc, "color_r")           != nullptr, "has color_r");
    check(find_param(desc, "color_g")           != nullptr, "has color_g");
    check(find_param(desc, "color_b")           != nullptr, "has color_b");
    check(find_param(desc, "layout")            != nullptr, "has layout");
    check(find_param(desc, "scale_enabled")     != nullptr, "has scale_enabled");
    check(find_param(desc, "rotation_enabled")  != nullptr, "has rotation_enabled");
    check(find_param(desc, "color_mod_enabled") != nullptr, "has color_mod_enabled");
}

} // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string path = build_dir + "/shape_field.dylib";

    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "FATAL: %s not found\n", path.c_str());
        return 1;
    }

    std::fprintf(stderr, "=== Test: Instanced Shapes Phase 6 (rotation + shape_idx lanes) ===\n");

    vivid::OperatorLoader loader;
    if (!loader.load(path.c_str())) {
        std::fprintf(stderr, "FATAL: could not load %s\n", path.c_str());
        return 1;
    }
    const auto* desc = loader.descriptor();
    if (!desc) {
        std::fprintf(stderr, "FATAL: null descriptor\n");
        return 1;
    }

    check(std::strcmp(desc->name, "ShapeField") == 0, "name is \"Instanced Shapes\"");

    test_port_counts(*desc);
    test_phase5_lanes_preserved(*desc);
    test_phase6_lanes(*desc);
    test_phase6_lane_ordering(*desc);
    test_existing_params_preserved(*desc);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
