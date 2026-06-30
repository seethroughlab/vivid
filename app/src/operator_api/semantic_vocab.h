#pragma once
// Semantic-metadata contract (P4.4). Operator params/ports may carry semantic_* hints
// (tag/shape/unit) so agents + tooling can reason about them. Any field may be left
// unset, but if set it must come from a known vocabulary (or the "x_" custom-extension
// namespace). This header IS the contract — keep it in sync with
// docs/SEMANTIC-PARAM-TAGS.md. Pure + header-only (operator_api types only), so the
// same check runs in the headless test and can be reused live over any descriptor.
#include "operator_api/types.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace vivid {

inline const std::unordered_set<std::string>& semantic_tags() {
    static const std::unordered_set<std::string> v = {
        // audio / analysis
        "analysis", "amplitude_linear", "gain_db", "frequency_hz", "pan", "resonance",
        "q_factor", "bpm", "beats", "phase_01", "time_seconds", "time_milliseconds",
        "midi_note", "midi_velocity", "gate", "trigger", "sample_rate_hz",
        // visual / spatial
        "color_rgba", "position_xy", "position_xyz", "scale_xy", "scale_xyz",
        "rotation_degrees", "rotation_radians", "uv", "resolution_px",
        // generic
        "seed", "probability_01", "count", "index", "enabled",
        "path_audio", "path_image", "path_video", "path_font",
    };
    return v;
}

inline const std::unordered_set<std::string>& semantic_shapes() {
    static const std::unordered_set<std::string> v = {
        "scalar", "vec2", "vec3", "vec4", "color", "bool", "int", "enum",
        "event", "string", "path", "pattern",
    };
    return v;
}

inline const std::unordered_set<std::string>& semantic_units() {
    static const std::unordered_set<std::string> v = {
        "Hz", "s", "ms", "dB", "deg", "rad", "bpm", "px",
    };
    return v;
}

// A value conforms if it is unset, lives in the "x_" custom-extension namespace, or is in
// the allowed set. (semantic_intent is intentionally free-form and not checked.)
inline bool semantic_value_ok(const std::unordered_set<std::string>& allowed, const char* v) {
    if (!v || !*v) return true;
    std::string s(v);
    if (s.rfind("x_", 0) == 0) return true;
    return allowed.count(s) != 0;
}

// Return human-readable issues for a descriptor (empty = conformant). Checks param
// tag/shape/unit + port tag (port shapes are a looser structural vocabulary, left
// unchecked). Pure — works on any descriptor, built-in or dlopen'd.
inline std::vector<std::string> validate_semantic_metadata(const VividOperatorDescriptor& d) {
    std::vector<std::string> issues;
    const std::string op = d.name ? d.name : "?";
    auto note = [&](const std::string& where, const char* kind, const char* val) {
        issues.push_back(op + " " + where + ": unknown semantic_" + kind + " '" + val + "'");
    };
    for (uint32_t i = 0; i < d.param_count; ++i) {
        const VividParamDescriptor& p = d.params[i];
        const std::string pn = std::string("param ") + (p.name ? p.name : "?");
        if (!semantic_value_ok(semantic_tags(),   p.semantic_tag))   note(pn, "tag",   p.semantic_tag);
        if (!semantic_value_ok(semantic_shapes(), p.semantic_shape)) note(pn, "shape", p.semantic_shape);
        if (!semantic_value_ok(semantic_units(),  p.semantic_unit))  note(pn, "unit",  p.semantic_unit);
    }
    for (uint32_t i = 0; i < d.port_count; ++i) {
        const VividPortDescriptor& p = d.ports[i];
        const std::string pn = std::string("port ") + (p.name ? p.name : "?");
        if (!semantic_value_ok(semantic_tags(), p.semantic_tag)) note(pn, "tag", p.semantic_tag);
    }
    return issues;
}

}  // namespace vivid
