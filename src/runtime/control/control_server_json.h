#pragma once

// JSON response envelope helpers shared across control-server handlers.
// Extracted from control_server_internal.h (Audit 04-R2-F3).

#include "runtime/control/runtime_api.h"  // CommandResult
#include <nlohmann/json.hpp>
#include <string>

namespace vivid {

inline std::string json_ok(nlohmann::json result) {
    return nlohmann::json{{"ok", true}, {"result", std::move(result)}}.dump();
}

inline std::string json_ok_msg(const std::string& msg) {
    return nlohmann::json{{"ok", true}, {"message", msg}}.dump();
}

inline std::string json_err(const std::string& msg) {
    return nlohmann::json{{"ok", false}, {"error", msg}}.dump();
}

inline std::string command_result_to_json(const CommandResult& r) {
    return r.ok ? json_ok_msg(r.message) : json_err(r.message);
}

// Like command_result_to_json, but when the CommandResult.message is itself a
// JSON payload (e.g. a LockfileStatus from RuntimeAPI::verify_project_lockfile),
// parse it and inline it as `{"ok": true, "status": {...}}` so consumers only
// need a single JSON.parse.
inline std::string unwrap_status_to_json(const CommandResult& r) {
    nlohmann::ordered_json out = nlohmann::ordered_json::object();
    if (!r.ok) {
        out["ok"]    = false;
        out["error"] = r.message;
        return out.dump();
    }
    out["ok"] = true;
    try {
        out["status"] = nlohmann::json::parse(r.message);
    } catch (const nlohmann::json::exception&) {
        // Defensive: preserve as string if it somehow isn't valid JSON.
        out["status"] = r.message;
    }
    return out.dump();
}

} // namespace vivid
