// Unit tests for scan_quarantine — pure filesystem + JSON, no runtime.
//
// Each test writes N fake CrashRecord JSON files into a temp dir and calls
// scan_quarantine_at() with a deterministic "now" so the 24h window logic is
// testable without time travel.

#include "runtime/core/quarantine.h"

#include "test_helpers.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// A fixed "now" far enough from Unix epoch for tests to subtract freely.
// 2026-04-16T12:00:00Z.
constexpr std::time_t kNow = 1776297600;

std::string iso8601_from_utc(std::time_t t) {
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &t);
#else
    gmtime_r(&t, &tm_utc);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
    return buf;
}

// Sanitize an ISO8601 timestamp for use as a filename (replace colons with '-').
std::string iso8601_filename(const std::string& ts) {
    std::string s = ts;
    for (char& c : s) if (c == ':') c = '-';
    return s;
}

struct RecordSpec {
    std::string node_type;       // primary identity
    std::string operator_name;   // fallback when node_type is empty
    std::string pkg_name;
    std::time_t t = kNow;
    std::string timestamp_override;  // if set, used verbatim (for malformed tests)
    bool        omit_timestamp = false;  // if true, no "timestamp" field at all
};

fs::path write_record(const fs::path& dir, int idx, const RecordSpec& s) {
    nlohmann::json j;
    j["node_type"]     = s.node_type;
    j["operator_name"] = s.operator_name;
    j["pkg_name"]      = s.pkg_name;
    std::string ts;
    if (!s.omit_timestamp) {
        ts = s.timestamp_override.empty()
            ? iso8601_from_utc(s.t)
            : s.timestamp_override;
        j["timestamp"] = ts;
    }
    // Unique filename; the scan is agnostic to filename content.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d_%s.json", idx, iso8601_filename(ts).c_str());
    fs::path p = dir / buf;
    std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
    ofs << j.dump();
    return p;
}

const vivid::QuarantineEntry* find_entry(
    const std::vector<vivid::QuarantineEntry>& v,
    const std::string& type, const std::string& pkg)
{
    for (const auto& e : v)
        if (e.identity.type_name == type && e.identity.pkg_name == pkg) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_empty_directory() {
    std::fprintf(stderr, "\n--- quarantine: empty directory ---\n");
    ScopedTempDir dir("vivid_q_empty");
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.empty(), "empty dir → no entries");
}

void test_missing_directory() {
    std::fprintf(stderr, "\n--- quarantine: missing directory ---\n");
    auto v = vivid::scan_quarantine_at("/nonexistent/vivid_q_nope", kNow);
    check(v.empty(), "nonexistent dir → no entries");
}

void test_below_threshold() {
    std::fprintf(stderr, "\n--- quarantine: below threshold ---\n");
    ScopedTempDir dir("vivid_q_below");
    write_record(dir.path, 0, {.node_type = "blur", .pkg_name = "fx", .t = kNow - 60});
    write_record(dir.path, 1, {.node_type = "blur", .pkg_name = "fx", .t = kNow - 120});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.empty(), "2 crashes → not quarantined (threshold is 3)");
}

void test_at_threshold() {
    std::fprintf(stderr, "\n--- quarantine: exactly at threshold ---\n");
    ScopedTempDir dir("vivid_q_at");
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i, {.node_type = "blur", .pkg_name = "fx", .t = kNow - i * 60});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.size() == 1, "3 crashes → 1 quarantined entry");
    if (v.size() == 1) {
        check(v[0].identity.type_name == "blur", "identity.type_name = blur");
        check(v[0].identity.pkg_name  == "fx",   "identity.pkg_name = fx");
        check(v[0].crash_count        == 3,      "count = 3");
        check(!v[0].last_seen_timestamp.empty(), "last_seen_timestamp populated");
    }
}

void test_above_threshold_single_entry() {
    std::fprintf(stderr, "\n--- quarantine: above threshold yields ONE entry ---\n");
    ScopedTempDir dir("vivid_q_above");
    for (int i = 0; i < 5; ++i)
        write_record(dir.path, i, {.node_type = "blur", .pkg_name = "fx", .t = kNow - i * 60});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.size() == 1, "5 crashes → 1 quarantined entry (not per-record)");
    if (v.size() == 1)
        check(v[0].crash_count == 5, "count = 5");
}

void test_outside_window() {
    std::fprintf(stderr, "\n--- quarantine: outside 24h window ---\n");
    ScopedTempDir dir("vivid_q_outside");
    // 48 hours ago
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i,
                     {.node_type = "blur", .pkg_name = "fx",
                      .t = kNow - (48 * 3600) - i * 60});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.empty(), "all crashes outside window → not quarantined");
}

void test_mixed_window() {
    std::fprintf(stderr, "\n--- quarantine: mixed window ---\n");
    ScopedTempDir dir("vivid_q_mixed");
    write_record(dir.path, 0, {.node_type = "blur", .pkg_name = "fx", .t = kNow - 60});
    write_record(dir.path, 1, {.node_type = "blur", .pkg_name = "fx",
                                .t = kNow - 48 * 3600});
    write_record(dir.path, 2, {.node_type = "blur", .pkg_name = "fx",
                                .t = kNow - 72 * 3600});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.empty(), "1 recent + 2 old → not quarantined");
}

void test_multiple_identities() {
    std::fprintf(stderr, "\n--- quarantine: multiple identities ---\n");
    ScopedTempDir dir("vivid_q_multi");
    // type A: hits threshold
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i, {.node_type = "a", .pkg_name = "p", .t = kNow - i * 60});
    // type B: below threshold
    for (int i = 0; i < 2; ++i)
        write_record(dir.path, 100 + i, {.node_type = "b", .pkg_name = "p", .t = kNow - i * 60});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.size() == 1,                    "only A quarantined");
    check(find_entry(v, "a", "p") != nullptr, "A present");
    check(find_entry(v, "b", "p") == nullptr, "B absent");
}

void test_package_separates_identity() {
    std::fprintf(stderr, "\n--- quarantine: (type, pkg) separates identity ---\n");
    ScopedTempDir dir("vivid_q_pkg");
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i,        {.node_type = "blur", .pkg_name = "a", .t = kNow - i * 60});
    for (int i = 0; i < 2; ++i)
        write_record(dir.path, 100 + i,  {.node_type = "blur", .pkg_name = "b", .t = kNow - i * 60});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.size() == 1, "(blur, a) quarantined; (blur, b) not");
    check(find_entry(v, "blur", "a") != nullptr, "(blur, a) present");
    check(find_entry(v, "blur", "b") == nullptr, "(blur, b) absent");
}

void test_core_operator_empty_pkg() {
    std::fprintf(stderr, "\n--- quarantine: core operator has empty pkg_name ---\n");
    ScopedTempDir dir("vivid_q_core");
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i, {.node_type = "lfo", .pkg_name = "", .t = kNow - i * 60});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.size() == 1,                       "core operator quarantined");
    check(find_entry(v, "lfo", "") != nullptr, "empty pkg identity preserved");
}

void test_missing_timestamp_skipped() {
    std::fprintf(stderr, "\n--- quarantine: missing timestamp → skipped ---\n");
    ScopedTempDir dir("vivid_q_nots");
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i,
                     {.node_type = "blur", .pkg_name = "fx", .omit_timestamp = true});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.empty(), "records without timestamp → skipped");
}

void test_malformed_timestamp_skipped() {
    std::fprintf(stderr, "\n--- quarantine: malformed timestamp → skipped ---\n");
    ScopedTempDir dir("vivid_q_bad_ts");
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i,
                     {.node_type = "blur", .pkg_name = "fx",
                      .timestamp_override = "not-a-date"});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.empty(), "malformed timestamps → skipped");
}

void test_missing_type_skipped() {
    std::fprintf(stderr, "\n--- quarantine: missing type AND operator_name → skipped ---\n");
    ScopedTempDir dir("vivid_q_notype");
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i, {.node_type = "", .operator_name = "", .pkg_name = "p",
                                    .t = kNow - i * 60});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.empty(), "records without identity → skipped");
}

void test_operator_name_fallback() {
    std::fprintf(stderr, "\n--- quarantine: operator_name fallback ---\n");
    ScopedTempDir dir("vivid_q_fallback");
    // node_type empty; use operator_name instead
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i, {.node_type = "", .operator_name = "sine",
                                    .pkg_name = "", .t = kNow - i * 60});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.size() == 1,                          "quarantined under operator_name");
    check(find_entry(v, "sine", "") != nullptr,   "identity type = sine");
}

void test_reserved_files_ignored() {
    std::fprintf(stderr, "\n--- quarantine: reserved files ignored ---\n");
    ScopedTempDir dir("vivid_q_reserved");
    // 3 legitimate crash records
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i, {.node_type = "blur", .pkg_name = "fx", .t = kNow - i * 60});

    // Same-shaped content but reserved filenames — must be skipped.
    nlohmann::json j = {
        {"node_type", "reserved_should_ignore"},
        {"pkg_name", ""},
        {"timestamp", iso8601_from_utc(kNow - 60)},
    };
    for (const auto* name : {"latest-crash.json", "latest-snapshot.json", "crash.marker"}) {
        std::ofstream ofs(dir.path / name, std::ios::binary | std::ios::trunc);
        ofs << j.dump();
    }

    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.size() == 1,                               "only blur quarantined");
    check(find_entry(v, "blur", "fx") != nullptr,       "blur present");
    check(find_entry(v, "reserved_should_ignore", "") == nullptr,
          "reserved filenames excluded even with matching content");
}

void test_future_timestamp_skipped() {
    std::fprintf(stderr, "\n--- quarantine: far-future timestamps skipped ---\n");
    ScopedTempDir dir("vivid_q_future");
    // Three crashes timestamped 2 hours in the future — clock skew should not
    // allow these to bypass the window check.
    for (int i = 0; i < 3; ++i)
        write_record(dir.path, i, {.node_type = "blur", .pkg_name = "fx",
                                    .t = kNow + 7200 + i * 60});
    auto v = vivid::scan_quarantine_at(dir.str(), kNow);
    check(v.empty(), "far-future timestamps treated as skew and dropped");
}

} // namespace

int main(int, char**) {
    test_empty_directory();
    test_missing_directory();
    test_below_threshold();
    test_at_threshold();
    test_above_threshold_single_entry();
    test_outside_window();
    test_mixed_window();
    test_multiple_identities();
    test_package_separates_identity();
    test_core_operator_empty_pkg();
    test_missing_timestamp_skipped();
    test_malformed_timestamp_skipped();
    test_missing_type_skipped();
    test_operator_name_fallback();
    test_reserved_files_ignored();
    test_future_timestamp_skipped();

    std::fprintf(stderr, "\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
