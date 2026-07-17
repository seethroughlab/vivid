// Headless test for ADR-0018 R3: the stateless "3 crashes in 24h" quarantine computed from the
// crash-history JSON files, and un-quarantine by clearing an operator's history.
#include "app/crash_recovery.h"   // CrashRecord (to_json)
#include "app/quarantine.h"
#include "test_helpers.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

static void write_record(const std::string& dir, const std::string& fname,
                         const std::string& op, long long unix_time) {
    vivid::CrashRecord r;
    r.operator_name = op; r.node_type = op; r.unix_time = unix_time;
    r.signal = 11; r.signal_name = "SIGSEGV"; r.timestamp = std::to_string(unix_time);
    std::ofstream(( fs::path(dir) / fname).string(), std::ios::trunc) << r.to_json().dump();
}

int main() {
    using namespace vivid;
    const fs::path dir = fs::temp_directory_path() / ("vivid_quar_test_" + std::to_string(::getpid()));
    fs::remove_all(dir); fs::create_directories(dir);
    const long long now = 1'800'000'000;   // fixed "now" for determinism

    // Two crashes for BadOp within the window: below the threshold → not quarantined.
    write_record(dir.string(), "a.json", "BadOp", now - 100);
    write_record(dir.string(), "b.json", "BadOp", now - 200);
    CHECK(scan_quarantine_at(dir.string(), now).empty());

    // A third within the window → quarantined; a record older than 24h does NOT count.
    write_record(dir.string(), "c.json", "BadOp", now - 300);
    write_record(dir.string(), "old.json", "BadOp", now - (kQuarantineWindowSeconds + 5000));  // outside window
    {
        auto q = scan_quarantine_at(dir.string(), now);
        CHECK(q.size() == 1);
        CHECK(!q.empty() && q[0].type_name == "BadOp");
        CHECK(!q.empty() && q[0].crash_count == 3);   // the old one is excluded
    }

    // A different operator with one crash doesn't reach the threshold.
    write_record(dir.string(), "d.json", "OkOp", now - 50);
    CHECK(scan_quarantine_at(dir.string(), now).size() == 1);   // still just BadOp

    // Reserved files are ignored (a latest-crash.json must not double-count).
    write_record(dir.string(), "latest-crash.json", "BadOp", now - 10);
    CHECK(scan_quarantine_at(dir.string(), now)[0].crash_count == 3);

    // Un-quarantine: clearing BadOp's history drops it below the threshold.
    const int removed = clear_crash_history(dir.string(), "BadOp");
    CHECK(removed == 4);   // a.json, b.json, c.json, old.json (NOT the reserved latest-crash.json)
    CHECK(scan_quarantine_at(dir.string(), now).empty());

    fs::remove_all(dir);
    return vivid::test::summary("test_quarantine");
}
