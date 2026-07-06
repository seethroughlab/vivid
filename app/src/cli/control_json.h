#pragma once
// Shared operator-descriptor → JSON serialization for the control server's discovery endpoints.
// One place so every discovery response (list_operators / list_audio_operators / …) exposes the
// SAME rich schema — including the semantic metadata (semantic_tag/shape/unit/intent) and display
// hints an agent needs to pick + wire params by intent rather than guessing names. Pure (only
// operator_api/types.h + nlohmann/json), so it is unit-testable headlessly.
#include "operator_api/types.h"

#include <nlohmann/json.hpp>

namespace vivid::control_json {

inline const char* param_type_name(uint32_t t) {
    switch (t) {
        case VIVID_PARAM_INT:  return "int";
        case VIVID_PARAM_BOOL: return "bool";
        case VIVID_PARAM_FILE: return "file";
        case VIVID_PARAM_TEXT: return "text";
        default:               return "float";
    }
}

inline const char* display_hint_name(uint32_t h) {
    switch (h) {
        case VIVID_DISPLAY_KNOB:   return "knob";
        case VIVID_DISPLAY_XY_PAD: return "xy_pad";
        case VIVID_DISPLAY_COLOR:  return "color";
        case VIVID_DISPLAY_HIDDEN: return "hidden";
        case VIVID_DISPLAY_ADSR:   return "adsr";
        case VIVID_DISPLAY_LFO:    return "lfo";
        default:                   return "default";
    }
}

// A param's full discovery schema. Required fields always present; optional string fields (the
// semantic metadata, description, group, asset kind) are emitted only when set, keeping the JSON
// tight. `semantic_*` are the intent hints; `display_hint` names the inspector widget.
inline nlohmann::json param_to_json(const VividParamDescriptor& p) {
    nlohmann::json j = {
        {"name", p.name ? p.name : ""},
        {"type", param_type_name(p.type)},
        {"default", p.default_value},
        {"min", p.min_value},
        {"max", p.max_value},
    };
    auto add = [&](const char* key, const char* v) { if (v && *v) j[key] = v; };
    add("description",     p.description);
    add("group",           p.group);
    add("semantic_tag",    p.semantic_tag);
    add("semantic_shape",  p.semantic_shape);
    add("semantic_unit",   p.semantic_unit);
    add("semantic_intent", p.semantic_intent);
    add("asset_kind",      p.asset_kind);
    if (p.display_hint != VIVID_DISPLAY_DEFAULT) j["display_hint"] = display_hint_name(p.display_hint);
    if (p.choice_count > 0 && p.choice_labels) {
        nlohmann::json choices = nlohmann::json::array();
        for (uint32_t i = 0; i < p.choice_count; ++i)
            if (p.choice_labels[i]) choices.push_back(p.choice_labels[i]);
        j["choices"] = choices;
    }
    return j;
}

// A port's discovery schema: direction + any semantic metadata.
inline nlohmann::json port_to_json(const VividPortDescriptor& p) {
    nlohmann::json j = {
        {"name", p.name ? p.name : ""},
        {"dir", p.direction == VIVID_PORT_OUTPUT ? "out" : "in"},
    };
    auto add = [&](const char* key, const char* v) { if (v && *v) j[key] = v; };
    add("semantic_tag",    p.semantic_tag);
    add("semantic_shape",  p.semantic_shape);
    add("semantic_intent", p.semantic_intent);
    add("description",     p.description);
    return j;
}

// The full operator schema shared by list_operators + list_audio_operators: identity + params +
// ports. `kind` is a caller-supplied label ("gpu_visual" / "audio_effect" / "instrument" / …).
inline nlohmann::json operator_to_json(const VividOperatorDescriptor& d, const char* kind = nullptr) {
    nlohmann::json jo;
    jo["name"] = d.name ? d.name : "";
    if (d.display_name && *d.display_name) jo["display_name"] = d.display_name;
    if (d.summary && *d.summary)           jo["summary"] = d.summary;
    if (kind && *kind)                     jo["kind"] = kind;
    nlohmann::json kws = nlohmann::json::array();
    for (uint32_t i = 0; i < d.keyword_count; ++i)
        if (d.keywords && d.keywords[i]) kws.push_back(d.keywords[i]);
    jo["keywords"] = kws;
    nlohmann::json params = nlohmann::json::array();
    for (uint32_t i = 0; i < d.param_count; ++i) params.push_back(param_to_json(d.params[i]));
    jo["params"] = params;
    nlohmann::json ports = nlohmann::json::array();
    for (uint32_t i = 0; i < d.port_count; ++i) ports.push_back(port_to_json(d.ports[i]));
    jo["ports"] = ports;
    return jo;
}

}  // namespace vivid::control_json
