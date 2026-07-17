#pragma once

#include <set>
#include <string>
#include <vector>

// ADR-0018 (R3): a repeat-offender operator is disabled by default. STATELESS — recomputed on every
// launch from the crash history (crash_recovery.*), so there is no quarantine file to corrupt or
// migrate. An operator with ≥3 crashes in the last 24h is quarantined; at load time it is skipped
// (its nodes then load as op_missing(), ADR-0019), the chooser greys it with a reason, and it can be
// un-quarantined by clearing its crash history.
//
// Trimmed from vivid-classic: identity is the op's `type_name` alone (trunk has no compiled-in visual
// built-ins and op type names are unique, so pkg_name isn't needed).
namespace vivid {

constexpr int       kQuarantineThreshold     = 3;
constexpr long long kQuarantineWindowSeconds = 24 * 3600;

struct QuarantineEntry {
    std::string type_name;
    int         crash_count = 0;   // crashes within the window
    std::string last_seen;         // most recent ISO 8601 timestamp
};

// Scan `{crash_dir}/*.json` (skipping the reserved snapshot/latest files) and return the operators
// that crashed ≥ kQuarantineThreshold times within kQuarantineWindowSeconds of `now_unix`. Silently
// skips unparsable / out-of-window records (and future-dated ones beyond a small clock-skew grace).
std::vector<QuarantineEntry> scan_quarantine_at(const std::string& crash_dir, long long now_unix);

// scan_quarantine_at with now = std::time(nullptr).
std::vector<QuarantineEntry> scan_quarantine(const std::string& crash_dir);

// Just the quarantined type names (what the op-load skip + chooser consume).
std::set<std::string> quarantined_types(const std::string& crash_dir);

// Un-quarantine: delete the crash-history records attributed to `type_name` so it drops below the
// threshold on the next launch (visible + reversible per the ADR). Returns how many were removed.
int clear_crash_history(const std::string& crash_dir, const std::string& type_name);

}  // namespace vivid
