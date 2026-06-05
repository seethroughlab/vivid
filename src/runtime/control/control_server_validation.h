#pragma once

// Dispatch field-extraction helpers and connect-address/port validation helpers
// shared across control-server handlers. Extracted from
// control_server_internal.h (Audit 04-R2-F3).

#include "runtime/control/control_server_json.h"  // json_err
#include "runtime/graph/graph.h"                   // Graph, NodeDef, ConnectionDef
#include "runtime/operators/operator_registry.h"   // OperatorRegistry
#include "operator_api/types.h"
#include "operator_api/type_id.h"  // vivid_is_custom_port_type
#include <nlohmann/json.hpp>
#include <cctype>
#include <string>
#include <vector>

namespace vivid {

// ---------------------------------------------------------------------------
// Dispatch field-extraction helpers (audit 04-R2-F1)
//
// On failure each `require_*` sets `err` to a ready-to-return json_err naming
// the offending field and returns false; on success it fills `out` and returns
// true. Handlers do: `std::string err; if (!require_string(root,"x",x,err)) return err;`
// The response envelope ({ok:false,"error":...}) is preserved; only the
// per-field message text is standardized.
// ---------------------------------------------------------------------------

inline bool require_string(const nlohmann::json& root, const char* name,
                           std::string& out, std::string& err) {
    if (!root.contains(name) || !root[name].is_string()) {
        err = json_err(std::string("missing or invalid string field '") + name + "'");
        return false;
    }
    out = root[name].get<std::string>();
    return true;
}

inline bool require_int(const nlohmann::json& root, const char* name,
                        int& out, std::string& err) {
    if (!root.contains(name) || !root[name].is_number()) {
        err = json_err(std::string("missing or invalid numeric field '") + name + "'");
        return false;
    }
    out = root[name].get<int>();
    return true;
}

inline bool require_float(const nlohmann::json& root, const char* name,
                          float& out, std::string& err) {
    if (!root.contains(name) || !root[name].is_number()) {
        err = json_err(std::string("missing or invalid numeric field '") + name + "'");
        return false;
    }
    out = root[name].get<float>();
    return true;
}

inline bool require_bool(const nlohmann::json& root, const char* name,
                         bool& out, std::string& err) {
    if (!root.contains(name) || !root[name].is_boolean()) {
        err = json_err(std::string("missing or invalid boolean field '") + name + "'");
        return false;
    }
    out = root[name].get<bool>();
    return true;
}

inline std::string optional_string(const nlohmann::json& root, const char* name,
                                   const std::string& dflt) {
    return (root.contains(name) && root[name].is_string())
        ? root[name].get<std::string>() : dflt;
}

inline float optional_float(const nlohmann::json& root, const char* name, float dflt) {
    return (root.contains(name) && root[name].is_number())
        ? root[name].get<float>() : dflt;
}

inline int optional_int(const nlohmann::json& root, const char* name, int dflt) {
    return (root.contains(name) && root[name].is_number())
        ? root[name].get<int>() : dflt;
}

inline bool optional_bool(const nlohmann::json& root, const char* name, bool dflt) {
    return (root.contains(name) && root[name].is_boolean())
        ? root[name].get<bool>() : dflt;
}

inline bool split_addr_local(const std::string& addr, std::string& node, std::string& port) {
    size_t slash = addr.find('/');
    if (slash == std::string::npos) return false;
    node = addr.substr(0, slash);
    port = addr.substr(slash + 1);
    return !node.empty() && !port.empty();
}

inline const ConnectionDef* find_connection_by_addr(const Graph& graph,
                                                    const std::string& from_addr,
                                                    const std::string& to_addr) {
    std::string fn, fp, tn, tp;
    if (!split_addr_local(from_addr, fn, fp) || !split_addr_local(to_addr, tn, tp))
        return nullptr;
    for (const auto& c : graph.connections()) {
        if (c.from_node == fn && c.from_port == fp && c.to_node == tn && c.to_port == tp)
            return &c;
    }
    return nullptr;
}

// Returns a human-readable warning when a connect address names a port that
// the operator's descriptor doesn't expose (the most common silent failure:
// e.g. "mixer/input" instead of "mixer/input_0"). The connection is still
// stored — an unresolved port is dropped at compile and surfaced by
// get_graph_errors — but this gives immediate feedback. Empty string = ok.
// Conservative: only warns when the descriptor resolves AND the port is
// neither an exact match nor a member of a repeat group (so grow-on-connect
// variadic ports never produce a false warning).
inline std::string connect_port_issue(OperatorRegistry& registry,
                                       const NodeDef* node,
                                       const std::string& port,
                                       bool want_output) {
    if (!node) return {};
    const VividOperatorDescriptor* desc = registry.probe_descriptor(node->type);
    if (!desc || !desc->ports) return {};  // unknown descriptor → stay silent

    const VividPortDirection want_dir = want_output ? VIVID_PORT_OUTPUT : VIVID_PORT_INPUT;

    // Strip a trailing "_<digits>" so repeat-group bases match (input_0 → input).
    auto strip_index = [](const std::string& s) -> std::string {
        size_t us = s.find_last_of('_');
        if (us == std::string::npos || us + 1 >= s.size()) return s;
        for (size_t i = us + 1; i < s.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return s;
        return s.substr(0, us);
    };
    const std::string port_base = strip_index(port);

    bool exact_match = false;
    std::vector<std::string> candidates;  // valid same-direction port names
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        const auto& p = desc->ports[i];
        if (!p.name || p.direction != want_dir) continue;
        const std::string name = p.name;
        candidates.push_back(name);
        if (name == port) { exact_match = true; break; }
        // Repeat-group tolerance: accept any "<group>_<n>" when the group base
        // or an enumerated member shares the stripped prefix.
        if ((p.repeat_group && port_base == p.repeat_group) ||
            strip_index(name) == port_base) {
            exact_match = true;
            break;
        }
    }
    if (exact_match) return {};

    // A connection may target (or source) a PARAMETER directly rather than a
    // port: a control/signal → param edge drives that param every frame
    // (a targets_param edge, applied by the executor) — a first-class feature,
    // NOT a dropped connection. If the address names a real param, it's valid;
    // don't emit the "dropped at compile" warning.
    if (desc->params) {
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            if (desc->params[i].name && port == desc->params[i].name)
                return {};
        }
    }

    std::string msg = "port '" + node->id + "/" + port + "' is not "
        + (want_output ? "an output" : "an input")
        + " of operator '" + node->type + "'";
    if (!candidates.empty()) {
        msg += " (available: ";
        for (size_t i = 0; i < candidates.size() && i < 8; ++i)
            msg += (i ? ", " : "") + candidates[i];
        if (candidates.size() > 8) msg += ", …";
        msg += ")";
    }
    msg += "; the connection was stored but will be dropped at compile — "
           "check get_graph_errors";
    return msg;
}

// Resolve a port's declared type by name, for connect() type-compat warnings.
// Returns true and sets out_type only when `port` names a real port in the
// wanted direction (including repeat-group members). Returns false for params,
// unknown ports, or unknown descriptors — callers stay silent in those cases
// (param targeting and name issues are handled elsewhere).
inline bool resolve_exact_port_type(OperatorRegistry& registry,
                                    const NodeDef* node,
                                    const std::string& port,
                                    bool want_output,
                                    VividPortType& out_type) {
    if (!node) return false;
    const VividOperatorDescriptor* desc = registry.probe_descriptor(node->type);
    if (!desc || !desc->ports) return false;
    const VividPortDirection want_dir = want_output ? VIVID_PORT_OUTPUT : VIVID_PORT_INPUT;
    auto strip_index = [](const std::string& s) -> std::string {
        size_t us = s.find_last_of('_');
        if (us == std::string::npos || us + 1 >= s.size()) return s;
        for (size_t i = us + 1; i < s.size(); ++i)
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return s;
        return s.substr(0, us);
    };
    const std::string port_base = strip_index(port);
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        const auto& p = desc->ports[i];
        if (!p.name || p.direction != want_dir) continue;
        const std::string name = p.name;
        if (name == port ||
            (p.repeat_group && port_base == p.repeat_group) ||
            strip_index(name) == port_base) {
            out_type = p.type;
            return true;
        }
    }
    return false;
}

// Mirror the graph compiler's Pass-2 type validation (graph_compiler.cpp): string
// ports must match exactly (otherwise the whole compile fails), a texture port
// only connects to a texture port, and a custom 'data' port only connects to the
// same custom type — these mismatches are silently dropped/rejected at compile,
// so warn immediately. All other combinations (scalar / control / audio /
// lane-array, or exact matches) are compatible. Empty string = no warning.
inline std::string connect_type_issue(VividPortType from_type, VividPortType to_type) {
    if (from_type == to_type) return {};
    if (from_type == VIVID_PORT_LANE_ARRAY || to_type == VIVID_PORT_LANE_ARRAY) return {};
    const char* tail = "; this connection will be dropped at compile — check get_graph_errors";
    if (from_type == VIVID_PORT_STRING || from_type == VIVID_PORT_STRING_LANES ||
        to_type == VIVID_PORT_STRING || to_type == VIVID_PORT_STRING_LANES)
        return std::string("type mismatch: string ports must match exactly") + tail;
    if (from_type == VIVID_PORT_TEXTURE || to_type == VIVID_PORT_TEXTURE)
        return std::string("type mismatch: a gpu_texture port only connects to another "
                           "gpu_texture port") + tail;
    if (vivid_is_custom_port_type(from_type) != vivid_is_custom_port_type(to_type))
        return std::string("type mismatch: a custom 'data' port only connects to the same "
                           "data_type") + tail;
    return {};
}

} // namespace vivid
