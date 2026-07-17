#pragma once

#include <optional>
#include <string>
#include <nlohmann/json.hpp>

// ADR-0018 (R2): turn the async-signal-safe crash marker (crash_guard.h) into a durable, attributed
// record. On launch, if a marker from a prior crash exists, reconstruct a CrashRecord (which
// operator, which node, which signal, when) from the marker + the last warm snapshot, append it to a
// capped history under {user_data_dir}/crashes/, and remove the marker. While running, a warm
// snapshot of the graph (node id ↔ operator type) is written periodically so the NEXT crash can be
// attributed to a specific node. Trimmed from vivid-classic's crash_recovery.
namespace vivid {

struct App;

struct CrashRecord {
    std::string timestamp;       // ISO 8601 UTC ("2026-07-16T07:30:00Z")
    long long   unix_time = 0;   // seconds (for the 24h quarantine window in R3)
    int         signal = 0;
    std::string signal_name;     // "SIGSEGV", …
    std::string operator_name;   // the op type active at crash (from g_current_operator), "" if none
    std::string node_id;         // resolved from the snapshot when the operator matches a node
    std::string node_type;       // ditto (== operator_name for a visual op)
    std::string app_version;

    nlohmann::json to_json() const;
    static CrashRecord from_json(const nlohmann::json&);
};

class CrashRecovery {
public:
    // Fix the crash-dir paths (marker / snapshot / history). Does not touch the filesystem.
    explicit CrashRecovery(const std::string& crash_dir);

    // If a crash marker exists, reconstruct + persist the record (history + latest-crash.json),
    // remove the marker, and prune history to the cap. Returns the record (the prior crash), else
    // nullopt. Call ONCE at startup, before any operator runs.
    std::optional<CrashRecord> init();

    // Point crash_guard's handler at our marker + snapshot files. Call after init().
    void install_signal_paths() const;

    // Write a warm snapshot (node id ↔ operator type + runtime scalars) so the next crash can be
    // attributed to a node. Cheap; call throttled from the frame loop.
    void write_snapshot(const App& app) const;

    const std::string& crash_dir() const { return dir_; }

private:
    std::string dir_;
    std::string marker_path_;
    std::string snapshot_path_;
    std::string latest_path_;
};

}  // namespace vivid
