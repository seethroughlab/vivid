// Phase 5 of the operator-gaps plan: `Instanced Shapes` now accepts five
// lane-array input ports so polyphonic control data (gates, notes, velocities,
// FFT bins) drives per-instance position/size/hue/brightness directly.
//
// This test verifies the descriptor surface — the new ports exist with the
// right types, directions, and semantic metadata, and the existing static
// params are untouched. Render-level behavior is verified manually via MCP
// once the runtime is up; it needs a WebGPU device that partition 10 doesn't
// have.
//
// Partition 10: pure CPU, no GPU/audio/window.

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

void test_descriptor_basics(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Descriptor basics ---\n");
    check(std::strcmp(desc.name, "ShapeField") == 0,
          "operator name is \"Instanced Shapes\"");

    // Output `texture` still present.
    const auto* tex = find_port(desc, "texture", VIVID_PORT_OUTPUT);
    check(tex != nullptr, "has output port \"texture\"");
    if (tex) check(tex->type == VIVID_PORT_TEXTURE, "texture output is TEXTURE");
}

void test_new_lane_inputs(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- New lane-array input ports ---\n");
    assert_lane_input(desc, "pos_x",      "position_xy",      "x_component");
    assert_lane_input(desc, "pos_y",      "position_xy",      "y_component");
    assert_lane_input(desc, "size",       "amplitude_linear", "scale_multiplier");
    assert_lane_input(desc, "hue",        "color_hue",        "hue_cycles");
    assert_lane_input(desc, "brightness", "amplitude_linear", "per_note_amplitude");
}

void test_lane_input_ordering(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Lane input ordering ---\n");
    // The runtime indexes ctx->input_lanes[] by input-port ordinal, so the
    // order here must match kLanePosX=0, kLanePosY=1, kLaneSize=2, kLaneHue=3,
    // kLaneBrightness=4 in the .cpp.
    check(input_port_index(desc, "pos_x")      == 0, "pos_x is input index 0");
    check(input_port_index(desc, "pos_y")      == 1, "pos_y is input index 1");
    check(input_port_index(desc, "size")       == 2, "size is input index 2");
    check(input_port_index(desc, "hue")        == 3, "hue is input index 3");
    check(input_port_index(desc, "brightness") == 4, "brightness is input index 4");
}

void test_existing_params_preserved(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Existing params preserved ---\n");

    // Spot-check a few params that must NOT regress. Full list of params is
    // large (layout modulation etc.); we only assert the headline user-facing
    // surface stayed intact.
    auto* count = find_param(desc, "count");
    check(count != nullptr, "has count param");
    if (count) {
        check_float(count->default_value, 16.0f, "count default = 16");
        check_float(count->min_value, 1.0f,      "count min = 1");
        check_float(count->max_value, 64.0f,     "count max = 64");
    }

    auto* shape = find_param(desc, "shape");
    check(shape != nullptr, "has shape param");
    if (shape) {
        check(shape->choice_count == 6, "shape has 6 choices");
    }

    auto* layout = find_param(desc, "layout");
    check(layout != nullptr, "has layout param");
    if (layout) {
        check(layout->choice_count == 4, "layout has 4 choices");
    }

    check(find_param(desc, "base_size")         != nullptr, "has base_size");
    check(find_param(desc, "softness")          != nullptr, "has softness");
    check(find_param(desc, "color_r")           != nullptr, "has color_r");
    check(find_param(desc, "color_g")           != nullptr, "has color_g");
    check(find_param(desc, "color_b")           != nullptr, "has color_b");
    check(find_param(desc, "scale_enabled")     != nullptr, "has scale_enabled");
    check(find_param(desc, "rotation_enabled")  != nullptr, "has rotation_enabled");
    check(find_param(desc, "color_mod_enabled") != nullptr, "has color_mod_enabled");
}

void test_port_counts(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Port counts ---\n");
    int inputs = 0, outputs = 0;
    for (uint32_t i = 0; i < desc.port_count; ++i) {
        if (desc.ports[i].direction == VIVID_PORT_INPUT)  ++inputs;
        if (desc.ports[i].direction == VIVID_PORT_OUTPUT) ++outputs;
    }
    check(inputs  == 7, "7 input ports (pos_x, pos_y, size, hue, brightness, rotation, shape_idx)");
    check(outputs == 1, "1 output port (texture)");
}

} // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string path = build_dir + "/shape_field.dylib";

    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "FATAL: %s not found (build instanced_shapes.dylib first)\n",
                     path.c_str());
        return 1;
    }

    std::fprintf(stderr, "=== Test: Instanced Shapes lane-array inputs ===\n");

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

    test_descriptor_basics(*desc);
    test_port_counts(*desc);
    test_new_lane_inputs(*desc);
    test_lane_input_ordering(*desc);
    test_existing_params_preserved(*desc);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
