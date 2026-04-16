#include "runtime/core/quarantine.h"

#include "runtime/core/crash_recovery.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vivid {

namespace {

// Parse "YYYY-MM-DDTHH:MM:SSZ" into UTC time_t.  Returns true on success.
// Accepts only the exact format CrashRecoveryManager writes — no timezone
// offsets or fractional seconds.  Any mismatch is treated as a soft failure
// and the caller drops the record.
bool parse_iso8601_utc(const std::string& s, std::time_t& out) {
    if (s.empty()) return false;
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    char z = 0;
    const int matched = std::sscanf(s.c_str(),
                                    "%d-%d-%dT%d:%d:%d%c",
                                    &y, &mo, &d, &h, &mi, &se, &z);
    if (matched != 7 || z != 'Z')                    return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31)         return false;
    if (h  < 0 || h  > 23 || mi < 0 || mi > 59)       return false;
    if (se < 0 || se > 60)                            return false;  // allow leap second

    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon  = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min  = mi;
    tm.tm_sec  = se;
#if defined(_WIN32)
    out = _mkgmtime(&tm);
#else
    out = timegm(&tm);
#endif
    return out != static_cast<std::time_t>(-1);
}

bool is_reserved_filename(const std::string& name) {
    return name == "latest-crash.json"
        || name == "latest-snapshot.json"
        || name == "crash.marker";
}

// Hashable (type, pkg) key.
struct IdKey {
    std::string type_name;
    std::string pkg_name;
    bool operator==(const IdKey& o) const {
        return type_name == o.type_name && pkg_name == o.pkg_name;
    }
};

struct IdKeyHash {
    size_t operator()(const IdKey& k) const noexcept {
        // Good-enough hash: mix the two string hashes.
        const std::hash<std::string> h;
        return h(k.type_name) * 1315423911u ^ h(k.pkg_name);
    }
};

struct Bucket {
    size_t      count = 0;
    std::string last_seen;   // newest ISO 8601 timestamp string observed
    std::time_t last_seen_t = 0;
};

} // namespace

// ---------------------------------------------------------------------------

std::vector<QuarantineEntry> scan_quarantine_at(const std::string& crash_dir,
                                                std::time_t now_utc) {
    namespace fs = std::filesystem;
    std::vector<QuarantineEntry> result;

    std::error_code ec;
    if (!fs::exists(crash_dir, ec) || !fs::is_directory(crash_dir, ec)) {
        return result;
    }

    std::unordered_map<IdKey, Bucket, IdKeyHash> buckets;

    for (const auto& entry : fs::directory_iterator(crash_dir, ec)) {
        if (!entry.is_regular_file(ec))                     continue;
        const std::string name = entry.path().filename().string();
        if (is_reserved_filename(name))                     continue;
        if (entry.path().extension() != ".json")            continue;

        // Parse record.
        nlohmann::json j;
        {
            std::ifstream ifs(entry.path());
            if (!ifs) continue;
            try { ifs >> j; } catch (...) { continue; }
        }
        CrashRecord rec = CrashRecord::from_json(j);

        // Identity: prefer node_type, fall back to operator_name (matches
        // the Phase 2 SafeModeConfig fallback).  Skip records with neither.
        std::string type = !rec.node_type.empty() ? rec.node_type : rec.operator_name;
        if (type.empty()) continue;

        std::time_t t = 0;
        if (!parse_iso8601_utc(rec.timestamp, t)) continue;
        if (now_utc - t > kQuarantineWindowSeconds) continue;
        if (t > now_utc + 60) continue;  // clock skew guard: reject > 1 min in the future

        IdKey key{std::move(type), rec.pkg_name};
        auto& b = buckets[key];
        b.count += 1;
        if (t >= b.last_seen_t) {
            b.last_seen_t = t;
            b.last_seen   = rec.timestamp;
        }
    }

    result.reserve(buckets.size());
    for (auto& kv : buckets) {
        if (kv.second.count < kQuarantineThreshold) continue;
        QuarantineEntry e;
        e.identity.type_name   = kv.first.type_name;
        e.identity.pkg_name    = kv.first.pkg_name;
        e.crash_count          = kv.second.count;
        e.last_seen_timestamp  = std::move(kv.second.last_seen);
        result.push_back(std::move(e));
    }
    return result;
}

std::vector<QuarantineEntry> scan_quarantine(const std::string& crash_dir) {
    return scan_quarantine_at(crash_dir, std::time(nullptr));
}

} // namespace vivid
