#pragma once
#include <string>
#include <nlohmann/json.hpp>

// Stable, machine-readable error codes for the control server / MCP bridge.
// Clients (the agent) branch on `code` — a fixed vocabulary — not on the prose
// `error` string, which is free to change. Every failure reply is
// { "ok": false, "code": "<stable>", "error": "<human message>" }; every success
// is { "ok": true, ... }. (Lesson from vivid-classic: named codes, not stringly.)
namespace vivid::control {

namespace code {
inline constexpr const char* kBadJson       = "bad_json";        // request body wasn't valid JSON
inline constexpr const char* kUnknownMethod = "unknown_method";  // no handler for the path
inline constexpr const char* kNoSession     = "no_session";      // app state not available
inline constexpr const char* kNoGraph       = "no_graph";
inline constexpr const char* kNoVgraph      = "no_vgraph";
inline constexpr const char* kNoTransport   = "no_transport";
inline constexpr const char* kBadArg        = "bad_arg";         // missing/invalid argument
inline constexpr const char* kOutOfRange    = "out_of_range";    // index outside a valid range
inline constexpr const char* kNotFound      = "not_found";       // named/identified thing absent
inline constexpr const char* kIoError       = "io_error";        // file read/write failed
inline constexpr const char* kInternal      = "internal";        // unexpected handler failure
inline constexpr const char* kTimeout       = "timeout";         // main loop didn't drain in time
inline constexpr const char* kInvalidDescriptor = "invalid_descriptor";  // operator descriptor failed validation
}  // namespace code

inline nlohmann::json ok(nlohmann::json extra = nlohmann::json::object()) {
    extra["ok"] = true;
    return extra;
}
inline nlohmann::json err(const char* code, const std::string& message) {
    return nlohmann::json{ {"ok", false}, {"code", code}, {"error", message} };
}

}  // namespace vivid::control
