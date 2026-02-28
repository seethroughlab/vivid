#pragma once

#include "operator_api/types.h"
#include <optional>
#include <string>
#include <vector>

namespace vivid {

struct WgslHeaderParam {
    std::string name;
    VividParamType type = VIVID_PARAM_FLOAT;
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    std::string label;                        // display label (empty = use name)
    std::vector<std::string> choices;         // for int enums
    VividDisplayHint display_hint = VIVID_DISPLAY_DEFAULT;
    std::string group;                        // collapsible group name
    uint8_t layout_columns = 0;              // 0 = full-width
    uint8_t layout_column_index = 0;
};

struct WgslHeaderInput {
    std::string name;
};

struct WgslHeader {
    std::string name;
    std::string description;
    bool time_dependent = false;
    std::vector<WgslHeaderInput> inputs;
    bool inputs_specified = false;   // false = use default 1-in/1-out
    std::vector<WgslHeaderParam> params;
    std::string fragment_source;     // .wgsl with header comment stripped
};

// Parse a self-describing .wgsl file.
// Returns nullopt on failure, with details in `error`.
std::optional<WgslHeader> parse_wgsl_header(const std::string& file_contents,
                                             std::string& error);

} // namespace vivid
