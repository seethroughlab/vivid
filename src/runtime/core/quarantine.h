#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace vivid {

// ---------------------------------------------------------------------------
// Quarantine — scan the crash-history directory for repeat-offender operators.
//
// A crash is counted toward an operator's quarantine score by identity
// (type_name, pkg_name).  An identity that accumulates kQuarantineThreshold
// or more crashes within kQuarantineWindowSeconds is "quarantined".
//
// Phase 4 consumes the result in two places:
//   - main.cpp logs a warning per quarantined identity in normal mode.
//   - In safe mode, main.cpp merges quarantined type names into
//     SafeModeConfig::quarantined_types, which the graph compiler uses to
//     placeholder-ize matching nodes with reason "quarantined".
//
// Stateless — recomputed on every launch from {config_dir}/crashes/*.json.
// No separate quarantine.json file to migrate or corrupt.
// ---------------------------------------------------------------------------

constexpr size_t   kQuarantineThreshold     = 3;
constexpr int64_t  kQuarantineWindowSeconds = 24 * 3600;  // 24 hours

struct QuarantineIdentity {
    std::string type_name;
    std::string pkg_name;  // empty for core operators
};

struct QuarantineEntry {
    QuarantineIdentity identity;
    size_t             crash_count = 0;            // crashes within the window
    std::string        last_seen_timestamp;        // most recent ISO 8601 seen
};

// Scan `{crash_dir}/*.json` (skipping reserved files) and return the set of
// operator identities that crashed at least kQuarantineThreshold times within
// the last kQuarantineWindowSeconds.  Uses time(nullptr) as "now".  Silently
// skips files that fail to parse or are missing required fields.
std::vector<QuarantineEntry> scan_quarantine(const std::string& crash_dir);

// Testing seam: same as scan_quarantine() but takes an explicit "now" in
// UTC seconds so tests can use deterministic timestamps.
std::vector<QuarantineEntry> scan_quarantine_at(const std::string& crash_dir,
                                                std::time_t now_utc);

} // namespace vivid
