#include "runtime/gpu/wgsl_header_parser.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int passes  = 0;
static int failures = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
        failures++; \
        return; \
    } \
} while (0)

#define PASS() do { \
    std::fprintf(stderr, "  PASS: %s\n", __func__); \
    passes++; \
} while (0)

// ============================================================================
// Tests
// ============================================================================

static void test_basic_header() {
    std::string src = R"(/*{
  "name": "TestEffect",
  "description": "A test effect",
  "time_dependent": true,
  "params": [
    {"name": "amount", "default": 0.5, "min": 0.0, "max": 1.0}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return vec4f(1.0);
})";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->name == "TestEffect", "wrong name");
    ASSERT(result->description == "A test effect", "wrong description");
    ASSERT(result->time_dependent == true, "wrong time_dependent");
    ASSERT(result->params.size() == 1, "wrong param count");
    ASSERT(result->params[0].name == "amount", "wrong param name");
    ASSERT(result->params[0].default_value == 0.5f, "wrong default");
    ASSERT(result->params[0].min_value == 0.0f, "wrong min");
    ASSERT(result->params[0].max_value == 1.0f, "wrong max");
    ASSERT(result->params[0].type == VIVID_PARAM_FLOAT, "wrong type");
    ASSERT(!result->inputs_specified, "inputs should not be specified");
    ASSERT(result->fragment_source.find("@fragment") != std::string::npos, "missing fragment");
    PASS();
}

static void test_no_header() {
    std::string src = R"(@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return vec4f(1.0);
})";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(!result.has_value(), "should fail without header");
    ASSERT(!error.empty(), "should have error message");
    PASS();
}

static void test_malformed_json() {
    std::string src = R"(/*{ "name": "Bad", "params": [ }*/
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0); })";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(!result.has_value(), "should fail with malformed JSON");
    PASS();
}

static void test_missing_name() {
    std::string src = R"(/*{ "description": "no name" }*/
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0); })";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(!result.has_value(), "should fail without name");
    ASSERT(error.find("name") != std::string::npos, "error should mention name");
    PASS();
}

static void test_generator_empty_inputs() {
    std::string src = R"(/*{
  "name": "Generator",
  "inputs": [],
  "params": [
    {"name": "r", "default": 1.0, "min": 0.0, "max": 1.0}
  ]
}*/
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return vec4f(u.r, 0.0, 0.0, 1.0);
})";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->inputs_specified, "inputs_specified should be true");
    ASSERT(result->inputs.empty(), "should have 0 inputs");
    PASS();
}

static void test_multi_input() {
    std::string src = R"(/*{
  "name": "MultiIn",
  "inputs": [{"name": "source"}, {"name": "map"}],
  "params": [{"name": "blend", "default": 0.5, "min": 0.0, "max": 1.0}]
}*/
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return vec4f(1.0);
})";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->inputs_specified, "inputs_specified should be true");
    ASSERT(result->inputs.size() == 2, "should have 2 inputs");
    ASSERT(result->inputs[0].name == "source", "wrong input 0 name");
    ASSERT(result->inputs[1].name == "map", "wrong input 1 name");
    PASS();
}

static void test_choices() {
    std::string src = R"(/*{
  "name": "WithChoices",
  "params": [
    {"name": "mode", "type": "int", "default": 0, "min": 0, "max": 2,
     "choices": ["Linear", "Radial", "Spiral"]}
  ]
}*/
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f { return vec4f(1.0); })";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->params.size() == 1, "wrong param count");
    ASSERT(result->params[0].type == VIVID_PARAM_INT, "should be int type");
    ASSERT(result->params[0].choices.size() == 3, "should have 3 choices");
    ASSERT(result->params[0].choices[0] == "Linear", "wrong choice 0");
    ASSERT(result->params[0].choices[1] == "Radial", "wrong choice 1");
    ASSERT(result->params[0].choices[2] == "Spiral", "wrong choice 2");
    PASS();
}

static void test_display_hints() {
    std::string src = R"(/*{
  "name": "Hints",
  "params": [
    {"name": "x", "display": "xy_pad"},
    {"name": "y", "display": "xy_pad"},
    {"name": "r", "display": "color"},
    {"name": "size", "display": "knob"},
    {"name": "amount", "display": "default"}
  ]
}*/
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f { return vec4f(1.0); })";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->params[0].display_hint == VIVID_DISPLAY_XY_PAD, "wrong hint 0");
    ASSERT(result->params[1].display_hint == VIVID_DISPLAY_XY_PAD, "wrong hint 1");
    ASSERT(result->params[2].display_hint == VIVID_DISPLAY_COLOR, "wrong hint 2");
    ASSERT(result->params[3].display_hint == VIVID_DISPLAY_KNOB, "wrong hint 3");
    ASSERT(result->params[4].display_hint == VIVID_DISPLAY_DEFAULT, "wrong hint 4");
    PASS();
}

static void test_groups_and_layout() {
    std::string src = R"(/*{
  "name": "Layout",
  "params": [
    {"name": "a", "group": "Color", "columns": 2, "column": 0},
    {"name": "b", "group": "Color", "columns": 2, "column": 1}
  ]
}*/
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f { return vec4f(1.0); })";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->params[0].group == "Color", "wrong group 0");
    ASSERT(result->params[1].group == "Color", "wrong group 1");
    ASSERT(result->params[0].layout_columns == 2, "wrong columns 0");
    ASSERT(result->params[0].layout_column_index == 0, "wrong col index 0");
    ASSERT(result->params[1].layout_columns == 2, "wrong columns 1");
    ASSERT(result->params[1].layout_column_index == 1, "wrong col index 1");
    PASS();
}

static void test_bool_param() {
    std::string src = R"(/*{
  "name": "BoolTest",
  "params": [
    {"name": "enabled", "type": "bool", "default": true}
  ]
}*/
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f { return vec4f(1.0); })";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->params[0].type == VIVID_PARAM_BOOL, "should be bool");
    ASSERT(result->params[0].default_value == 1.0f, "default should be 1.0 for true");
    PASS();
}

static void test_label() {
    std::string src = R"(/*{
  "name": "Labels",
  "params": [
    {"name": "hue_shift", "label": "Hue Shift", "default": 0.0, "min": 0.0, "max": 360.0}
  ]
}*/
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f { return vec4f(1.0); })";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->params[0].label == "Hue Shift", "wrong label");
    ASSERT(result->params[0].name == "hue_shift", "name should be the identifier");
    PASS();
}

static void test_minimal_header() {
    std::string src = R"(/*{ "name": "Minimal" }*/
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f { return vec4f(1.0); })";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->name == "Minimal", "wrong name");
    ASSERT(result->params.empty(), "should have no params");
    ASSERT(!result->inputs_specified, "inputs should not be specified");
    ASSERT(result->time_dependent == false, "should not be time dependent");
    PASS();
}

static void test_choices_imply_int_type() {
    std::string src = R"(/*{
  "name": "ImpliedInt",
  "params": [
    {"name": "mode", "default": 0, "choices": ["A", "B"]}
  ]
}*/
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f { return vec4f(1.0); })";

    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->params[0].type == VIVID_PARAM_INT, "choices should imply int type");
    PASS();
}

static void test_fragment_source_stripping() {
    std::string src = "/*{ \"name\": \"Strip\" }*/\n@fragment\nfn fs_main() {}";
    std::string error;
    auto result = vivid::parse_wgsl_header(src, error);
    ASSERT(result.has_value(), error.c_str());
    ASSERT(result->fragment_source == "@fragment\nfn fs_main() {}", "incorrect stripping");
    PASS();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::fprintf(stderr, "=== test_wgsl_header ===\n");

    test_basic_header();
    test_no_header();
    test_malformed_json();
    test_missing_name();
    test_generator_empty_inputs();
    test_multi_input();
    test_choices();
    test_display_hints();
    test_groups_and_layout();
    test_bool_param();
    test_label();
    test_minimal_header();
    test_choices_imply_int_type();
    test_fragment_source_stripping();

    std::fprintf(stderr, "\n========================================\n");
    std::fprintf(stderr, "WGSL header parser: %d passed, %d failed\n", passes, failures);
    std::fprintf(stderr, "========================================\n");

    return failures > 0 ? 1 : 0;
}
