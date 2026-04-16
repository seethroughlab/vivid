#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace vivid {

class AudioEngine;
class ControlServer;
class Graph;
class RuntimeAPI;

// ---------------------------------------------------------------------------
// CrashRecord — fully-merged attribution of a previous-run fatal crash.
//
// Produced at startup by CrashRecoveryManager::init() when it detects a marker
// written by the signal handler.  Persisted to {config_dir}/crashes/.
//
// Fields flagged "reserved" are not populated in Phase 1 — later phases fill
// them in as the underlying state becomes easy to surface.
// ---------------------------------------------------------------------------
struct CrashRecord {
    std::string timestamp;          // ISO 8601, UTC
    int         signal = 0;
    std::string signal_name;        // "SIGSEGV", "SIGBUS", ...
    int         pid = 0;
    std::string vivid_version;
    std::string platform;           // "darwin", "linux", "windows", "unknown"
    std::string graph_path;
    bool        graph_dirty = false;
    std::string operator_name;      // from g_current_operator, via marker
    std::string node_id;
    std::string node_type;
    std::string pkg_name;
    std::string pkg_version;
    uint64_t    reload_serial = 0;
    uint32_t    audio_buffer_size = 0;
    uint32_t    audio_sample_rate = 0;
    int         control_server_port = 0;
    bool        mcp_attached = false;
    std::string audio_device;       // reserved
    std::string gpu_adapter;        // reserved
    std::string last_mutation;      // reserved

    nlohmann::json to_json() const;
    static CrashRecord from_json(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------
// CrashRecoveryManager — owns the crash-recovery lifecycle:
//   - periodic snapshot of runtime state to disk (warm path)
//   - marker-path registration with crash_guard (cold path)
//   - startup marker expansion into a structured CrashRecord
//   - history rotation (capped at 20 newest)
//
// All disk I/O happens on the main thread.  The signal handler only writes
// the marker via async-signal-safe calls in crash_guard.h.
// ---------------------------------------------------------------------------
class CrashRecoveryManager {
public:
    explicit CrashRecoveryManager(std::string crash_dir);

    // Expand a prior-run marker, if present, into latest-crash.json + a
    // timestamped history entry.  Returns the recovered record or nullopt.
    // Also prunes history to kHistoryKeep newest files.
    std::optional<CrashRecord> init();

    // Copy marker and snapshot paths into crash_guard's async-safe globals.
    // Call after init() and before any operator starts processing.
    void install_signal_paths();

    // Called every frame from the main loop.  Internally rate-limits to
    // every kSnapshotEveryFrames frames OR whenever reload_serial changes.
    void tick(uint64_t          frame_count,
              const Graph&      graph,
              const RuntimeAPI& api,
              const AudioEngine& audio,
              const ControlServer* server);

    // Force a snapshot write right now (bypasses rate limit).  Used by tests
    // and by callers who know runtime state just changed significantly.
    void force_snapshot(const Graph&      graph,
                        const RuntimeAPI& api,
                        const AudioEngine& audio,
                        const ControlServer* server);

    // Remove latest-crash.json.  Used by later phases (user-dismissed dialog,
    // MCP clear_last_crash endpoint); no-op if the file does not exist.
    void clear_latest();

    const std::string& crash_dir() const        { return crash_dir_; }
    const std::string& marker_path() const      { return marker_path_; }
    const std::string& snapshot_path() const    { return snapshot_path_; }
    const std::string& latest_crash_path() const { return latest_crash_path_; }

    // Prune the history directory to `keep` newest files.  Exposed for tests.
    void prune_history(size_t keep);

    // Test-only: parse a marker file and merge it with a snapshot file into a
    // CrashRecord, without touching disk beyond reading.  Missing snapshot is
    // tolerated — snapshot-sourced fields default-initialized.
    CrashRecord merge_for_test(const std::string& marker_path,
                               const std::string& snapshot_path) const;

    static constexpr size_t   kHistoryKeep         = 20;
    static constexpr uint64_t kSnapshotEveryFrames = 60;

private:
    std::string crash_dir_;
    std::string marker_path_;
    std::string snapshot_path_;
    std::string latest_crash_path_;
    uint64_t    last_snapshot_frame_ = 0;
    uint64_t    last_reload_serial_ = UINT64_MAX;
    bool        first_tick_ = true;

    nlohmann::json build_snapshot_json(const Graph&         graph,
                                       const RuntimeAPI&    api,
                                       const AudioEngine&   audio,
                                       const ControlServer* server) const;
    CrashRecord   merge_marker_and_snapshot(const std::string&   marker_text,
                                            const nlohmann::json& snapshot) const;
    void          write_json_atomic(const std::string& path,
                                    const nlohmann::json& j) const;
};

} // namespace vivid
