// Unit tests for CrashRecoveryManager — pure filesystem + JSON, no GPU / audio / window.
//
// These tests exercise serialization round-trips, marker→snapshot merging,
// startup expansion into latest-crash.json plus a history entry, the
// history-pruning invariant, and the atomic-write helper indirectly via
// init().  End-to-end signal-handler → marker → expansion is covered by a
// subprocess integration test (deferred, per Phase 1 plan).

#include "runtime/core/crash_recovery.h"

#include "test_helpers.h"

#include <nlohmann/json.hpp>

#include <signal.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs << content;
}

nlohmann::json read_json(const fs::path& path) {
    std::ifstream ifs(path);
    nlohmann::json j;
    ifs >> j;
    return j;
}

bool file_exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

vivid::CrashRecord make_full_record() {
    vivid::CrashRecord r;
    r.timestamp           = "2026-04-16T12:34:56Z";
    r.signal              = 11;
    r.signal_name         = "SIGSEGV";
    r.pid                 = 4321;
    r.vivid_version       = "0.9.9";
    r.platform            = "darwin";
    r.graph_path          = "/tmp/show.json";
    r.graph_dirty         = true;
    r.operator_name       = "lfo";
    r.node_id             = "lfo1";
    r.node_type           = "lfo";
    r.pkg_name            = "core";
    r.pkg_version         = "1.2.3";
    r.reload_serial       = 42;
    r.audio_buffer_size   = 256;
    r.audio_sample_rate   = 48000;
    r.control_server_port = 9876;
    r.mcp_attached        = true;
    r.audio_device        = "BuiltInSpeaker";
    r.gpu_adapter         = "Apple M-series";
    r.last_mutation       = "set_param lfo1.rate=1.5";
    return r;
}

void test_serialize_round_trip() {
    std::fprintf(stderr, "[test] serialize round-trip\n");
    vivid::CrashRecord original = make_full_record();
    nlohmann::json j = original.to_json();
    vivid::CrashRecord recovered = vivid::CrashRecord::from_json(j);
    check(recovered.timestamp           == original.timestamp,           "timestamp");
    check(recovered.signal              == original.signal,              "signal");
    check(recovered.signal_name         == original.signal_name,         "signal_name");
    check(recovered.pid                 == original.pid,                 "pid");
    check(recovered.vivid_version       == original.vivid_version,       "vivid_version");
    check(recovered.platform            == original.platform,            "platform");
    check(recovered.graph_path          == original.graph_path,          "graph_path");
    check(recovered.graph_dirty         == original.graph_dirty,         "graph_dirty");
    check(recovered.operator_name       == original.operator_name,       "operator_name");
    check(recovered.node_id             == original.node_id,             "node_id");
    check(recovered.node_type           == original.node_type,           "node_type");
    check(recovered.pkg_name            == original.pkg_name,            "pkg_name");
    check(recovered.pkg_version         == original.pkg_version,         "pkg_version");
    check(recovered.reload_serial       == original.reload_serial,       "reload_serial");
    check(recovered.audio_buffer_size   == original.audio_buffer_size,   "audio_buffer_size");
    check(recovered.audio_sample_rate   == original.audio_sample_rate,   "audio_sample_rate");
    check(recovered.control_server_port == original.control_server_port, "control_server_port");
    check(recovered.mcp_attached        == original.mcp_attached,        "mcp_attached");
    check(recovered.audio_device        == original.audio_device,        "audio_device");
    check(recovered.gpu_adapter         == original.gpu_adapter,         "gpu_adapter");
    check(recovered.last_mutation       == original.last_mutation,       "last_mutation");
}

void test_from_json_tolerates_missing_fields() {
    std::fprintf(stderr, "[test] from_json tolerates missing fields\n");
    nlohmann::json j = {{"signal", 6}, {"operator_name", "sine"}};
    vivid::CrashRecord r = vivid::CrashRecord::from_json(j);
    check(r.signal == 6,                           "partial: signal");
    check(r.operator_name == "sine",               "partial: operator_name");
    check(r.pid == 0,                              "partial: pid defaults to 0");
    check(r.graph_path.empty(),                    "partial: graph_path defaults to empty");
    check(r.graph_dirty == false,                  "partial: graph_dirty defaults to false");
}

void test_init_with_no_marker_returns_nullopt() {
    std::fprintf(stderr, "[test] init with no marker returns nullopt\n");
    ScopedTempDir dir("vivid_crash_nomarker");
    vivid::CrashRecoveryManager mgr(dir.str());
    auto rec = mgr.init();
    check(!rec.has_value(), "no marker → init returns nullopt");
    check(!file_exists(fs::path(dir.str()) / "latest-crash.json"),
          "no marker → latest-crash.json not created");
}

void test_init_expands_marker_and_snapshot() {
    std::fprintf(stderr, "[test] init expands marker + snapshot\n");
    ScopedTempDir dir("vivid_crash_expand");

    // Write a plausible snapshot JSON.
    nlohmann::json snapshot = {
        {"vivid_version",       "0.5.0"},
        {"platform",            "darwin"},
        {"pid",                 1001},
        {"graph_path",          "/home/user/demo.json"},
        {"graph_dirty",         true},
        {"reload_serial",       7},
        {"audio_buffer_size",   512},
        {"audio_sample_rate",   48000},
        {"control_server_port", 9876},
        {"mcp_attached",        true},
        {"nodes", nlohmann::json::array({
            {{"node_id", "lfo1"}, {"type_name", "lfo"},
             {"pkg_name", ""}, {"pkg_version", ""}},
            {{"node_id", "blur1"}, {"type_name", "blur"},
             {"pkg_name", "fx-pack"}, {"pkg_version", "2.0.1"}},
        })},
    };
    write_file(fs::path(dir.str()) / "latest-snapshot.json",
               snapshot.dump(2));

    // Marker points at the (exact) snapshot path; in practice the signal
    // handler writes the same absolute path.
    const std::string snapshot_path =
        (fs::path(dir.str()) / "latest-snapshot.json").string();
    const std::string marker_text =
        "signal=11\noperator=blur\nsnapshot=" + snapshot_path + "\n";
    write_file(fs::path(dir.str()) / "crash.marker", marker_text);

    vivid::CrashRecoveryManager mgr(dir.str());
    auto rec = mgr.init();

    check(rec.has_value(), "init returned a record");
    if (!rec) return;
    check(rec->signal == 11,                    "signal parsed from marker");
    check(rec->signal_name == "SIGSEGV",        "signal_name mapped from signal");
    check(rec->operator_name == "blur",         "operator_name from marker");
    check(rec->node_id == "blur1",              "node_id resolved from snapshot");
    check(rec->node_type == "blur",             "node_type resolved from snapshot");
    check(rec->pkg_name == "fx-pack",           "pkg_name resolved from snapshot");
    check(rec->pkg_version == "2.0.1",          "pkg_version resolved from snapshot");
    check(rec->graph_path == "/home/user/demo.json", "graph_path from snapshot");
    check(rec->graph_dirty,                     "graph_dirty from snapshot");
    check(rec->reload_serial == 7,              "reload_serial from snapshot");
    check(rec->audio_buffer_size == 512,        "audio_buffer_size from snapshot");
    check(rec->control_server_port == 9876,     "control_server_port from snapshot");
    check(rec->mcp_attached,                    "mcp_attached from snapshot");
    check(!rec->timestamp.empty(),              "timestamp is populated (now)");

    // Persisted artifacts.
    check(file_exists(fs::path(dir.str()) / "latest-crash.json"),
          "latest-crash.json written");
    check(!file_exists(fs::path(dir.str()) / "crash.marker"),
          "marker cleaned up");
    nlohmann::json latest = read_json(fs::path(dir.str()) / "latest-crash.json");
    check(latest["operator_name"] == "blur",    "latest-crash.json: operator_name");
    check(latest["node_id"] == "blur1",         "latest-crash.json: node_id");

    // Exactly one history entry should exist.
    int history_count = 0;
    for (const auto& e : fs::directory_iterator(dir.str())) {
        const auto name = e.path().filename().string();
        if (name == "latest-crash.json")    continue;
        if (name == "latest-snapshot.json") continue;
        if (name == "crash.marker")         continue;
        if (e.path().extension() == ".json") ++history_count;
    }
    check(history_count == 1, "exactly one history entry created");
}

void test_init_handles_missing_snapshot() {
    std::fprintf(stderr, "[test] init tolerates missing snapshot\n");
    ScopedTempDir dir("vivid_crash_no_snapshot");

    const std::string marker_text =
        "signal=6\noperator=sine\nsnapshot=/nonexistent/snapshot.json\n";
    write_file(fs::path(dir.str()) / "crash.marker", marker_text);

    vivid::CrashRecoveryManager mgr(dir.str());
    auto rec = mgr.init();

    check(rec.has_value(),                "init returned a record even w/o snapshot");
    if (!rec) return;
    check(rec->signal == 6,               "marker signal parsed");
    check(rec->signal_name == "SIGABRT",  "SIGABRT mapped");
    check(rec->operator_name == "sine",   "marker operator parsed");
    check(rec->node_id.empty(),           "node_id blank (snapshot missing)");
    check(rec->graph_path.empty(),        "graph_path blank (snapshot missing)");
    check(!file_exists(fs::path(dir.str()) / "crash.marker"),
          "marker cleaned up");
    check(file_exists(fs::path(dir.str()) / "latest-crash.json"),
          "latest-crash.json written");
}

void test_history_pruning() {
    std::fprintf(stderr, "[test] history pruning\n");
    ScopedTempDir dir("vivid_crash_prune");

    // Seed 5 dated history files with filenames that sort lexicographically
    // (== chronologically for ISO-8601-style names).
    const std::vector<std::string> names = {
        "2026-01-01T00-00-00Z.json",
        "2026-01-02T00-00-00Z.json",
        "2026-01-03T00-00-00Z.json",
        "2026-01-04T00-00-00Z.json",
        "2026-01-05T00-00-00Z.json",
    };
    for (const auto& n : names) {
        write_file(fs::path(dir.str()) / n, "{}");
    }
    // Sanity: reserved files should NOT be pruned.
    write_file(fs::path(dir.str()) / "latest-crash.json",    "{}");
    write_file(fs::path(dir.str()) / "latest-snapshot.json", "{}");

    vivid::CrashRecoveryManager mgr(dir.str());
    mgr.prune_history(3);

    check(!file_exists(fs::path(dir.str()) / names[0]),
          "oldest history file pruned");
    check(!file_exists(fs::path(dir.str()) / names[1]),
          "second-oldest history file pruned");
    check(file_exists(fs::path(dir.str()) / names[2]),
          "third-newest history file kept");
    check(file_exists(fs::path(dir.str()) / names[3]),
          "second-newest history file kept");
    check(file_exists(fs::path(dir.str()) / names[4]),
          "newest history file kept");
    check(file_exists(fs::path(dir.str()) / "latest-crash.json"),
          "reserved latest-crash.json not pruned");
    check(file_exists(fs::path(dir.str()) / "latest-snapshot.json"),
          "reserved latest-snapshot.json not pruned");
}

void test_clear_latest_is_idempotent() {
    std::fprintf(stderr, "[test] clear_latest is idempotent\n");
    ScopedTempDir dir("vivid_crash_clear");
    vivid::CrashRecoveryManager mgr(dir.str());
    // No file → no throw.
    mgr.clear_latest();
    check(!file_exists(fs::path(dir.str()) / "latest-crash.json"),
          "clear_latest with no file is a no-op");
    // Create, clear, verify gone.
    write_file(fs::path(dir.str()) / "latest-crash.json", "{}");
    mgr.clear_latest();
    check(!file_exists(fs::path(dir.str()) / "latest-crash.json"),
          "clear_latest removes latest-crash.json");
}

void test_merge_for_test_parser() {
    std::fprintf(stderr, "[test] merge_for_test (direct parser)\n");
    ScopedTempDir dir("vivid_crash_parser");
    const std::string marker_path =
        (fs::path(dir.str()) / "m.txt").string();
    const std::string snapshot_path =
        (fs::path(dir.str()) / "s.json").string();
    // SIGBUS is 10 on macOS/BSD, 7 on Linux — use the actual constant.
    const std::string marker_text =
        "signal=" + std::to_string(SIGBUS) + "\n"
        "operator=render3d\n"
        "snapshot=/whatever\n";
    write_file(marker_path, marker_text);
    nlohmann::json s = {
        {"nodes", nlohmann::json::array({
            {{"node_id", "r3d0"}, {"type_name", "render3d"},
             {"pkg_name", "vivid-3d"}, {"pkg_version", "0.4.0"}},
        })},
        {"graph_path", "/a/b/c.json"},
    };
    write_file(snapshot_path, s.dump());

    vivid::CrashRecoveryManager mgr(dir.str());
    vivid::CrashRecord rec = mgr.merge_for_test(marker_path, snapshot_path);
    check(rec.signal == SIGBUS,              "parser: signal");
    check(rec.signal_name == "SIGBUS",       "parser: SIGBUS mapped");
    check(rec.operator_name == "render3d",   "parser: operator_name");
    check(rec.node_id == "r3d0",             "parser: node_id resolved");
    check(rec.pkg_name == "vivid-3d",        "parser: pkg_name resolved");
    check(rec.pkg_version == "0.4.0",        "parser: pkg_version resolved");
    check(rec.graph_path == "/a/b/c.json",   "parser: graph_path from snapshot");
}

} // namespace

int main(int, char**) {
    test_serialize_round_trip();
    test_from_json_tolerates_missing_fields();
    test_init_with_no_marker_returns_nullopt();
    test_init_expands_marker_and_snapshot();
    test_init_handles_missing_snapshot();
    test_history_pruning();
    test_clear_latest_is_idempotent();
    test_merge_for_test_parser();

    std::fprintf(stderr, "\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
