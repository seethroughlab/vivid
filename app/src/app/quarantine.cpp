#include "app/quarantine.h"

#include "app/crash_recovery.h"   // CrashRecord::from_json

#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>

namespace vivid {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
// The reserved files in the crash dir that are NOT history records.
bool is_reserved(const std::string& name) {
    return name == "latest-snapshot.json" || name == "latest-crash.json" || name == "crash.marker";
}
}  // namespace

std::vector<QuarantineEntry> scan_quarantine_at(const std::string& crash_dir, long long now_unix) {
    std::vector<QuarantineEntry> out;
    std::error_code ec;
    if (!fs::is_directory(crash_dir, ec)) return out;

    std::map<std::string, QuarantineEntry> buckets;   // type_name -> tally
    for (const auto& e : fs::directory_iterator(crash_dir, ec)) {
        if (ec) break;
        if (e.path().extension() != ".json" || is_reserved(e.path().filename().string())) continue;
        std::ifstream f(e.path());
        json j = json::parse(f, nullptr, false);
        if (!j.is_object()) continue;
        const CrashRecord r = CrashRecord::from_json(j);
        if (r.operator_name.empty()) continue;
        // Inside the 24h window, with a small grace for a future-dated (clock-skew) record.
        const long long dt = now_unix - r.unix_time;
        if (r.unix_time == 0 || dt > kQuarantineWindowSeconds || dt < -60) continue;
        auto& b = buckets[r.operator_name];
        b.type_name = r.operator_name;
        ++b.crash_count;
        if (r.timestamp > b.last_seen) b.last_seen = r.timestamp;
    }
    for (auto& [_, b] : buckets)
        if (b.crash_count >= kQuarantineThreshold) out.push_back(b);
    return out;
}

std::vector<QuarantineEntry> scan_quarantine(const std::string& crash_dir) {
    return scan_quarantine_at(crash_dir, static_cast<long long>(std::time(nullptr)));
}

std::set<std::string> quarantined_types(const std::string& crash_dir) {
    std::set<std::string> s;
    for (const auto& q : scan_quarantine(crash_dir)) s.insert(q.type_name);
    return s;
}

int clear_crash_history(const std::string& crash_dir, const std::string& type_name) {
    int removed = 0;
    std::error_code ec;
    if (!fs::is_directory(crash_dir, ec)) return 0;
    for (const auto& e : fs::directory_iterator(crash_dir, ec)) {
        if (ec) break;
        if (e.path().extension() != ".json" || is_reserved(e.path().filename().string())) continue;
        std::ifstream f(e.path());
        json j = json::parse(f, nullptr, false);
        f.close();
        if (j.is_object() && CrashRecord::from_json(j).operator_name == type_name) {
            if (fs::remove(e.path(), ec)) ++removed;
        }
    }
    return removed;
}

}  // namespace vivid
