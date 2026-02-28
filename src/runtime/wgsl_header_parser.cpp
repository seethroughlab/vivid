#include "runtime/wgsl_header_parser.h"
#include "yyjson.h"
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

    yyjson_read_err read_err;
    yyjson_doc* doc = yyjson_read_opts(json_str.data(),
                                        json_str.size(), 0, nullptr, &read_err);
    if (!doc) {
        error = "JSON parse error: ";
        error += read_err.msg ? read_err.msg : "unknown";
        return std::nullopt;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(doc);
        error = "JSON header must be an object";
        return std::nullopt;
    }

    WgslHeader header;

    // Required: name
    yyjson_val* name_val = yyjson_obj_get(root, "name");
    if (!name_val || !yyjson_is_str(name_val)) {
        yyjson_doc_free(doc);
        error = "Missing required field: \"name\"";
        return std::nullopt;
    }
    header.name = yyjson_get_str(name_val);

    // Optional: description
    yyjson_val* desc_val = yyjson_obj_get(root, "description");
    if (desc_val && yyjson_is_str(desc_val))
        header.description = yyjson_get_str(desc_val);

    // Optional: time_dependent
    yyjson_val* td_val = yyjson_obj_get(root, "time_dependent");
    if (td_val && yyjson_is_bool(td_val))
        header.time_dependent = yyjson_get_bool(td_val);

    // Optional: inputs
    yyjson_val* inputs_val = yyjson_obj_get(root, "inputs");
    if (inputs_val) {
        if (!yyjson_is_arr(inputs_val)) {
            yyjson_doc_free(doc);
            error = "\"inputs\" must be an array";
            return std::nullopt;
        }
        header.inputs_specified = true;
        size_t idx, max;
        yyjson_val* input_val;
        yyjson_arr_foreach(inputs_val, idx, max, input_val) {
            if (!yyjson_is_obj(input_val)) {
                yyjson_doc_free(doc);
                error = "Each input must be an object";
                return std::nullopt;
            }
            WgslHeaderInput inp;
            yyjson_val* iname = yyjson_obj_get(input_val, "name");
            if (!iname || !yyjson_is_str(iname)) {
                yyjson_doc_free(doc);
                error = "Each input must have a \"name\" string";
                return std::nullopt;
            }
            inp.name = yyjson_get_str(iname);
            header.inputs.push_back(std::move(inp));
        }
    }

    // Optional: params
    yyjson_val* params_val = yyjson_obj_get(root, "params");
    if (params_val) {
        if (!yyjson_is_arr(params_val)) {
            yyjson_doc_free(doc);
            error = "\"params\" must be an array";
            return std::nullopt;
        }
        size_t idx, max;
        yyjson_val* param_val;
        yyjson_arr_foreach(params_val, idx, max, param_val) {
            if (!yyjson_is_obj(param_val)) {
                yyjson_doc_free(doc);
                error = "Each param must be an object";
                return std::nullopt;
            }
            WgslHeaderParam p;

            yyjson_val* pname = yyjson_obj_get(param_val, "name");
            if (!pname || !yyjson_is_str(pname)) {
                yyjson_doc_free(doc);
                error = "Each param must have a \"name\" string";
                return std::nullopt;
            }
            p.name = yyjson_get_str(pname);

            yyjson_val* ptype = yyjson_obj_get(param_val, "type");
            if (ptype && yyjson_is_str(ptype))
                p.type = parse_param_type(yyjson_get_str(ptype));

            yyjson_val* pdef = yyjson_obj_get(param_val, "default");
            if (pdef && yyjson_is_num(pdef))
                p.default_value = static_cast<float>(yyjson_get_num(pdef));
            else if (pdef && yyjson_is_bool(pdef))
                p.default_value = yyjson_get_bool(pdef) ? 1.0f : 0.0f;

            yyjson_val* pmin = yyjson_obj_get(param_val, "min");
            if (pmin && yyjson_is_num(pmin))
                p.min_value = static_cast<float>(yyjson_get_num(pmin));

            yyjson_val* pmax = yyjson_obj_get(param_val, "max");
            if (pmax && yyjson_is_num(pmax))
                p.max_value = static_cast<float>(yyjson_get_num(pmax));

            yyjson_val* plabel = yyjson_obj_get(param_val, "label");
            if (plabel && yyjson_is_str(plabel))
                p.label = yyjson_get_str(plabel);

            yyjson_val* pchoices = yyjson_obj_get(param_val, "choices");
            if (pchoices && yyjson_is_arr(pchoices)) {
                p.type = VIVID_PARAM_INT;  // choices implies int type
                size_t ci, cmax;
                yyjson_val* cv;
                yyjson_arr_foreach(pchoices, ci, cmax, cv) {
                    if (yyjson_is_str(cv))
                        p.choices.push_back(yyjson_get_str(cv));
                }
            }

            yyjson_val* pdisp = yyjson_obj_get(param_val, "display");
            if (pdisp && yyjson_is_str(pdisp))
                p.display_hint = parse_display_hint(yyjson_get_str(pdisp));

            yyjson_val* pgroup = yyjson_obj_get(param_val, "group");
            if (pgroup && yyjson_is_str(pgroup))
                p.group = yyjson_get_str(pgroup);

            yyjson_val* pcols = yyjson_obj_get(param_val, "columns");
            if (pcols && yyjson_is_int(pcols))
                p.layout_columns = static_cast<uint8_t>(yyjson_get_int(pcols));

            yyjson_val* pcol = yyjson_obj_get(param_val, "column");
            if (pcol && yyjson_is_int(pcol))
                p.layout_column_index = static_cast<uint8_t>(yyjson_get_int(pcol));

            header.params.push_back(std::move(p));
        }
    }

    yyjson_doc_free(doc);

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
