// Crash-recovery HTTP endpoints (Phase 5).
//
// Three routes exposed on port 9876:
//   - POST /get_last_crash        → read latest-crash.json + quarantine stats
//   - POST /clear_last_crash      → delete latest-crash.json (idempotent)
//   - POST /load_graph_safe_mode  → load a graph with safe-mode disable set
//
// Implementation delegates entirely to Phase 1 (CrashRecoveryManager),
// Phase 2 (SafeModeConfig + compute_safe_mode_config), Phase 4 (scan_quarantine),
// and the existing RuntimeAPI::load_graph synchronous load path.

#include "runtime/control/control_server_internal.h"
#include "runtime/core/crash_recovery.h"
#include "runtime/core/quarantine.h"
#include "runtime/core/safe_mode.h"
#include "runtime/core/runtime_core.h"

#include <fstream>
#include <optional>
#include <string>

namespace vivid {

namespace {

// Best-effort read of latest-crash.json.  Returns nullopt when the file is
// absent or fails to parse — the caller treats that as "no crash".
std::optional<CrashRecord> read_latest(CrashRecoveryManager* crm) {
    if (!crm) return std::nullopt;
    const std::string& path = crm->latest_crash_path();
    std::ifstream ifs(path);
    if (!ifs) return std::nullopt;
    try {
        nlohmann::json j;
        ifs >> j;
        return CrashRecord::from_json(j);
    } catch (...) {
        return std::nullopt;
    }
}

nlohmann::json quarantine_json(CrashRecoveryManager* crm) {
    auto arr = nlohmann::json::array();
    if (!crm) return arr;
    for (const auto& e : scan_quarantine(crm->crash_dir())) {
        arr.push_back({
            {"type",      e.identity.type_name},
            {"pkg",       e.identity.pkg_name},
            {"count",     e.crash_count},
            {"last_seen", e.last_seen_timestamp},
        });
    }
    return arr;
}

} // namespace

// ---------------------------------------------------------------------------

std::string handle_get_last_crash(CrashRecoveryManager* crm) {
    if (!crm) return json_err("crash recovery not available");
    nlohmann::json result;
    auto rec = read_latest(crm);
    result["crash"]       = rec ? rec->to_json() : nlohmann::json(nullptr);
    result["quarantined"] = quarantine_json(crm);
    return json_ok(std::move(result));
}

std::string handle_clear_last_crash(CrashRecoveryManager* crm) {
    if (!crm) return json_err("crash recovery not available");
    crm->clear_latest();
    return json_ok_msg("cleared");
}

std::string handle_load_graph_safe_mode(const nlohmann::json& root,
                                        CrashRecoveryManager* crm,
                                        RuntimeCore& core,
                                        RuntimeAPI& api,
                                        bool& has_gpu_ops,
                                        bool& has_audio) {
    if (!crm) return json_err("crash recovery not available");
    if (!root.contains("path") || !root["path"].is_string() ||
        root["path"].get<std::string>().empty()) {
        return json_err("load_graph_safe_mode requires 'path' parameter");
    }

    // Rebuild the config as of NOW — re-read latest-crash.json and rescan
    // quarantine so the endpoint behaves identically to the CLI --safe-mode
    // path even in a long-running session that has seen multiple crashes.
    auto rec = read_latest(crm);
    SafeModeConfig cfg = compute_safe_mode_config(rec ? &*rec : nullptr);
    for (const auto& q : scan_quarantine(crm->crash_dir())) {
        cfg.quarantined_types.insert(q.identity.type_name);
        cfg.disabled_types.erase(q.identity.type_name);
    }
    core.set_safe_mode(cfg);

    return command_result_to_json(
        api.load_graph(root["path"].get<std::string>(), has_gpu_ops, has_audio));
}

} // namespace vivid
