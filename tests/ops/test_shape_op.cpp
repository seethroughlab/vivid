// Unit tests for the Shape GPU operator — descriptor surface only.
//
// Verifies the new position_x / position_y params (added in Phase 2 of the
// operator-gaps plan) without requiring a live GPU device: the test loads
// shape.dylib, inspects the descriptor, and confirms names, defaults, ranges,
// ordering, semantic tags, and display hints. Render behavior is covered
// manually via the arpeggiator visualization end-to-end check.
//
// Partition 10: pure CPU, no GPU/audio/window.

#include "runtime/operators/operator_loader.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include "test_helpers.h"

namespace {

// Find a param by name. Returns nullptr if absent.
const VividParamDescriptor* find_param(const VividOperatorDescriptor& desc, const char* name) {
    for (uint32_t i = 0; i < desc.param_count; ++i) {
        if (std::strcmp(desc.params[i].name, name) == 0) return &desc.params[i];
    }
    return nullptr;
}

// Find a param's index by name. Returns -1 if absent.
int find_param_index(const VividOperatorDescriptor& desc, const char* name) {
    for (uint32_t i = 0; i < desc.param_count; ++i) {
        if (std::strcmp(desc.params[i].name, name) == 0) return static_cast<int>(i);
    }
    return -1;
}

void test_descriptor_basics(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Descriptor basics ---\n");
    check(std::strcmp(desc.name, "Shape") == 0, "descriptor name is \"Shape\"");
    check(desc.param_count == 10, "param count is 10 (was 8 before position_x/y)");
    check(desc.port_count  == 1,  "one output port");
    if (desc.port_count >= 1) {
        check(std::strcmp(desc.ports[0].name, "texture") == 0, "output port name is \"texture\"");
    }
}

void test_existing_params_preserved(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Existing params preserved ---\n");

    // Defaults and ranges that existed before Phase 2 must be unchanged so no
    // saved graph renders differently.
    auto* radius = find_param(desc, "radius");
    check(radius != nullptr, "has radius");
    if (radius) {
        check_float(radius->default_value, 0.3f, "radius default = 0.3");
        check_float(radius->min_value, 0.01f, "radius min = 0.01");
        check_float(radius->max_value, 1.0f,  "radius max = 1.0");
    }

    auto* sides = find_param(desc, "sides");
    check(sides != nullptr, "has sides");
    if (sides) {
        check_float(sides->default_value, 4.0f,  "sides default = 4");
        check_float(sides->min_value,    3.0f,  "sides min = 3");
        check_float(sides->max_value,    64.0f, "sides max = 64");
    }

    check(find_param(desc, "star")     != nullptr, "has star");
    check(find_param(desc, "rotation") != nullptr, "has rotation");
    check(find_param(desc, "softness") != nullptr, "has softness");
    check(find_param(desc, "r")        != nullptr, "has r");
    check(find_param(desc, "g")        != nullptr, "has g");
    check(find_param(desc, "b")        != nullptr, "has b");
}

void test_new_position_params(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- New position params ---\n");

    auto* px = find_param(desc, "position_x");
    check(px != nullptr, "has position_x");
    if (px) {
        check_float(px->default_value, 0.0f,  "position_x default = 0");
        check_float(px->min_value,    -1.0f,  "position_x min = -1");
        check_float(px->max_value,     1.0f,  "position_x max = +1");
        check(px->type == VIVID_PARAM_FLOAT,  "position_x type = FLOAT");
        check(px->semantic_tag    && std::strcmp(px->semantic_tag,    "position_xy") == 0,
              "position_x semantic_tag = position_xy");
        check(px->semantic_shape  && std::strcmp(px->semantic_shape,  "scalar") == 0,
              "position_x semantic_shape = scalar");
        check(px->semantic_intent && std::strcmp(px->semantic_intent, "x_component") == 0,
              "position_x semantic_intent = x_component");
        check(px->display_hint == VIVID_DISPLAY_XY_PAD,
              "position_x display_hint = XY_PAD");
    }

    auto* py = find_param(desc, "position_y");
    check(py != nullptr, "has position_y");
    if (py) {
        check_float(py->default_value, 0.0f,  "position_y default = 0");
        check_float(py->min_value,    -1.0f,  "position_y min = -1");
        check_float(py->max_value,     1.0f,  "position_y max = +1");
        check(py->type == VIVID_PARAM_FLOAT,  "position_y type = FLOAT");
        check(py->semantic_tag    && std::strcmp(py->semantic_tag,    "position_xy") == 0,
              "position_y semantic_tag = position_xy");
        check(py->semantic_intent && std::strcmp(py->semantic_intent, "y_component") == 0,
              "position_y semantic_intent = y_component");
        check(py->display_hint == VIVID_DISPLAY_XY_PAD,
              "position_y display_hint = XY_PAD");
    }
}

void test_param_ordering(const VividOperatorDescriptor& desc) {
    std::fprintf(stderr, "\n--- Param ordering ---\n");

    // The inspector renders params in declaration order. XY-pad widgets pair
    // CONSECUTIVE x/y params, so position_x and position_y must be adjacent
    // and in that order. They must also sit between softness and the color
    // block so spatial params group together.
    int ix_px = find_param_index(desc, "position_x");
    int ix_py = find_param_index(desc, "position_y");
    int ix_softness = find_param_index(desc, "softness");
    int ix_r = find_param_index(desc, "r");

    check(ix_px >= 0 && ix_py >= 0, "both position params indexed");
    if (ix_px >= 0 && ix_py >= 0) {
        check(ix_py == ix_px + 1, "position_y immediately follows position_x (XY_PAD pairing)");
    }
    if (ix_softness >= 0 && ix_px >= 0) {
        check(ix_px > ix_softness, "position_x comes after softness");
    }
    if (ix_r >= 0 && ix_py >= 0) {
        check(ix_r > ix_py, "color params (r) come after position_y");
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string path = build_dir + "/shape.dylib";

    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "FATAL: %s not found (build shape.dylib first)\n", path.c_str());
        return 1;
    }

    std::fprintf(stderr, "=== Test: Shape operator (descriptor surface) ===\n");

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
    test_existing_params_preserved(*desc);
    test_new_position_params(*desc);
    test_param_ordering(*desc);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
