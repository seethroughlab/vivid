#include "runtime/wgsl_header_parser.h"
#include <nlohmann/json.hpp>
#include <cstring>

namespace vivid {

// Find the JSON header block: starts with /*{ and ends with }*/
static bool find_header_block(const std::string& src, size_t& json_start, size_t& json_end,
                               size_t& block_end) {
    // Look for /*{ allowing optional whitespace between /* and {
    size_t open = src.find("/*");
    if (open == std::string::npos) return false;

    // Find the opening brace after /*
    size_t brace = open + 2;
    while (brace < src.size() && (src[brace] == ' ' || src[brace] == '\t' ||
                                   src[brace] == '\n' || src[brace] == '\r'))
        brace++;
    if (brace >= src.size() || src[brace] != '{') return false;

    json_start = brace;

    // Find the closing }*/
    // We need to find }*/ — scan for */ and check that the last non-whitespace before * is }
    size_t pos = brace + 1;
    while (pos < src.size()) {
        size_t close = src.find("*/", pos);
        if (close == std::string::npos) return false;

        // Check that there's a } before the */
        size_t check = close;
        while (check > json_start && (src[check - 1] == ' ' || src[check - 1] == '\t' ||
                                       src[check - 1] == '\n' || src[check - 1] == '\r'))
            check--;
        if (check > json_start && src[check - 1] == '}') {
            json_end = check;  // one past the }
            block_end = close + 2;  // one past the */
            return true;
        }
        pos = close + 2;
    }
    return false;
}

static VividParamType parse_param_type(const char* str) {
    if (!str) return VIVID_PARAM_FLOAT;
    if (std::strcmp(str, "int") == 0) return VIVID_PARAM_INT;
    if (std::strcmp(str, "bool") == 0) return VIVID_PARAM_BOOL;
    return VIVID_PARAM_FLOAT;
}

static VividDisplayHint parse_display_hint(const char* str) {
    if (!str) return VIVID_DISPLAY_DEFAULT;
    if (std::strcmp(str, "knob") == 0) return VIVID_DISPLAY_KNOB;
    if (std::strcmp(str, "xy_pad") == 0) return VIVID_DISPLAY_XY_PAD;
    if (std::strcmp(str, "color") == 0) return VIVID_DISPLAY_COLOR;
    return VIVID_DISPLAY_DEFAULT;
}

std::optional<WgslHeader> parse_wgsl_header(const std::string& file_contents,
                                             std::string& error) {
    size_t json_start, json_end, block_end;
    if (!find_header_block(file_contents, json_start, json_end, block_end)) {
        error = "No JSON header block found (expected /*{...}*/ at top of file)";
        return std::nullopt;
    }

    std::string json_str = file_contents.substr(json_start, json_end - json_start);

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_str);
    } catch (const nlohmann::json::parse_error& e) {
        error = "JSON parse error: ";
        error += e.what();
        return std::nullopt;
    }

    if (!root.is_object()) {
        error = "JSON header must be an object";
        return std::nullopt;
    }

    WgslHeader header;

    // Required: name
    auto name_it = root.find("name");
    if (name_it == root.end() || !name_it->is_string()) {
        error = "Missing required field: \"name\"";
        return std::nullopt;
    }
    header.name = name_it->get<std::string>();

    // Optional: description
    auto desc_it = root.find("description");
    if (desc_it != root.end() && desc_it->is_string())
        header.description = desc_it->get<std::string>();

    // Optional: time_dependent
    auto td_it = root.find("time_dependent");
    if (td_it != root.end() && td_it->is_boolean())
        header.time_dependent = td_it->get<bool>();

    // Optional: inputs
    auto inputs_it = root.find("inputs");
    if (inputs_it != root.end()) {
        if (!inputs_it->is_array()) {
            error = "\"inputs\" must be an array";
            return std::nullopt;
        }
        header.inputs_specified = true;
        for (auto& input_val : *inputs_it) {
            if (!input_val.is_object()) {
                error = "Each input must be an object";
                return std::nullopt;
            }
            WgslHeaderInput inp;
            auto iname = input_val.find("name");
            if (iname == input_val.end() || !iname->is_string()) {
                error = "Each input must have a \"name\" string";
                return std::nullopt;
            }
            inp.name = iname->get<std::string>();
            header.inputs.push_back(std::move(inp));
        }
    }

    // Optional: params
    auto params_it = root.find("params");
    if (params_it != root.end()) {
        if (!params_it->is_array()) {
            error = "\"params\" must be an array";
            return std::nullopt;
        }
        for (auto& param_val : *params_it) {
            if (!param_val.is_object()) {
                error = "Each param must be an object";
                return std::nullopt;
            }
            WgslHeaderParam p;

            auto pname = param_val.find("name");
            if (pname == param_val.end() || !pname->is_string()) {
                error = "Each param must have a \"name\" string";
                return std::nullopt;
            }
            p.name = pname->get<std::string>();

            auto ptype = param_val.find("type");
            if (ptype != param_val.end() && ptype->is_string())
                p.type = parse_param_type(ptype->get<std::string>().c_str());

            auto pdef = param_val.find("default");
            if (pdef != param_val.end() && pdef->is_number())
                p.default_value = pdef->get<float>();
            else if (pdef != param_val.end() && pdef->is_boolean())
                p.default_value = pdef->get<bool>() ? 1.0f : 0.0f;

            auto pmin = param_val.find("min");
            if (pmin != param_val.end() && pmin->is_number())
                p.min_value = pmin->get<float>();

            auto pmax = param_val.find("max");
            if (pmax != param_val.end() && pmax->is_number())
                p.max_value = pmax->get<float>();

            auto plabel = param_val.find("label");
            if (plabel != param_val.end() && plabel->is_string())
                p.label = plabel->get<std::string>();

            auto pchoices = param_val.find("choices");
            if (pchoices != param_val.end() && pchoices->is_array()) {
                p.type = VIVID_PARAM_INT;  // choices implies int type
                for (auto& cv : *pchoices) {
                    if (cv.is_string())
                        p.choices.push_back(cv.get<std::string>());
                }
            }

            auto pdisp = param_val.find("display");
            if (pdisp != param_val.end() && pdisp->is_string())
                p.display_hint = parse_display_hint(pdisp->get<std::string>().c_str());

            auto pgroup = param_val.find("group");
            if (pgroup != param_val.end() && pgroup->is_string())
                p.group = pgroup->get<std::string>();

            auto pcols = param_val.find("columns");
            if (pcols != param_val.end() && pcols->is_number_integer())
                p.layout_columns = static_cast<uint8_t>(pcols->get<int>());

            auto pcol = param_val.find("column");
            if (pcol != param_val.end() && pcol->is_number_integer())
                p.layout_column_index = static_cast<uint8_t>(pcol->get<int>());

            header.params.push_back(std::move(p));
        }
    }

    // Strip the header block from the source, preserving the fragment shader
    // Skip any leading whitespace/newlines after the block
    size_t src_start = block_end;
    while (src_start < file_contents.size() &&
           (file_contents[src_start] == '\n' || file_contents[src_start] == '\r'))
        src_start++;
    header.fragment_source = file_contents.substr(src_start);

    return header;
}

} // namespace vivid
