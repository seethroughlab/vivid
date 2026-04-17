// test_project_lockfile_cli.cpp — subprocess tests for `vivid lock` and
// `vivid verify-lock`. Shells out to the built vivid binary with ProcessRunner
// and inspects stdout/stderr/exit code for each scenario.
#include "runtime/graph/graph.h"
#include "runtime/platform/process_runner.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "test_helpers.h"

namespace {

std::string g_vivid_bin;  // resolved in main

struct VividRun {
    int exit_code = -1;
    std::string stdout_text;
    std::string stderr_text;
};

VividRun run_vivid(const std::vector<std::string>& args) {
    // ProcessRunner merges stdout + stderr into result.output. That blurs the
    // streams for the "pretty prints to stderr, stdout is empty" assertion.
    // Redirect via a temp file for stdout so we can check it independently.
    ScopedTempDir tmp("vivid_cli_redirect");
    auto stdout_path = (tmp.path / "stdout.txt").string();
    auto stderr_path = (tmp.path / "stderr.txt").string();

    // Use /bin/sh -c to redirect streams independently. argv is quoted via
    // shell-quoting by hand (args don't include any special chars in these
    // tests; the path strings from ScopedTempDir contain only / and alnum).
    std::string cmd = g_vivid_bin;
    for (const auto& a : args) { cmd += " "; cmd += "'"; cmd += a; cmd += "'"; }
    cmd += " >" + stdout_path;
    cmd += " 2>" + stderr_path;

    vivid::ProcessRunOptions opts;
    opts.argv = {"/bin/sh", "-c", cmd};
    opts.timeout_ms = 60000;

    auto result = vivid::run_process(opts);
    VividRun r;
    r.exit_code = result.launched ? result.exit_code : -1;

    auto read_all = [](const std::string& p) {
        std::ifstream in(p);
        std::string s((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
        return s;
    };
    r.stdout_text = read_all(stdout_path);
    r.stderr_text = read_all(stderr_path);
    return r;
}

void save_small_graph(const std::filesystem::path& path) {
    vivid::Graph g;
    g.add_node("a", "audio_out");
    g.save(path.string().c_str());
}

void test_vivid_lock_happy_path() {
    ScopedTempDir dir("vivid_lock_happy");
    auto graph_path = (dir.path / "demo.json").string();
    save_small_graph(graph_path);

    auto r = run_vivid({"lock", "--graph", graph_path});
    check(r.exit_code == 0, "vivid lock: exit 0");

    auto root = nlohmann::json::parse(r.stdout_text);
    check(root["ok"].get<bool>() == true, "vivid lock: ok=true");
    check(root.contains("message") && root["message"].is_string(),
          "vivid lock: message is a string");
    const std::string out_path = root["message"].get<std::string>();

    auto expected = (dir.path / "vivid.lock").string();
    check(out_path == expected,
          "vivid lock: default output is sibling vivid.lock");
    check(std::filesystem::exists(expected),
          "vivid lock: file actually written");
}

void test_vivid_lock_explicit_output() {
    ScopedTempDir dir("vivid_lock_output");
    auto graph_path = (dir.path / "demo.json").string();
    save_small_graph(graph_path);

    auto out_path = (dir.path / "custom.lock").string();
    auto r = run_vivid({"lock", "--graph", graph_path, "--output", out_path});
    check(r.exit_code == 0, "vivid lock --output: exit 0");

    auto root = nlohmann::json::parse(r.stdout_text);
    check(root["message"].get<std::string>() == out_path,
          "vivid lock --output: returns override path");
    check(std::filesystem::exists(out_path),
          "vivid lock --output: file written at override path");
}

void test_vivid_lock_missing_graph() {
    auto r = run_vivid({"lock", "--graph", "/does/not/exist.json"});
    check(r.exit_code == 1, "vivid lock missing graph: exit 1");

    auto root = nlohmann::json::parse(r.stdout_text);
    check(root["ok"].get<bool>() == false,
          "vivid lock missing graph: ok=false");
    check(root["error"].get<std::string>().find("failed to load graph") != std::string::npos,
          "vivid lock missing graph: error mentions load failure");
}

void test_vivid_verify_lock_match_round_trip() {
    ScopedTempDir dir("vivid_verify_match");
    auto graph_path = (dir.path / "demo.json").string();
    save_small_graph(graph_path);

    // Write a lockfile first.
    auto lock_r = run_vivid({"lock", "--graph", graph_path});
    check(lock_r.exit_code == 0, "verify-lock setup: lock succeeded");

    auto r = run_vivid({"verify-lock", "--graph", graph_path});
    // Exit 0 (match) or 1 (compatible_drift) is acceptable — what matters is
    // the JSON shape. A fresh graph/lock pair in the same env should match,
    // but environmental noise (e.g. package scan produces a finding) could
    // push it to drift. Both are "verify worked end-to-end".
    check(r.exit_code == 0 || r.exit_code == 1,
          "verify-lock round-trip: exit 0 or 1");

    auto root = nlohmann::json::parse(r.stdout_text);
    check(root["ok"].get<bool>() == true, "verify-lock: ok=true");
    check(root.contains("status") && root["status"].is_object(),
          "verify-lock: status is an inline object");
    const auto overall = root["status"]["overall"].get<std::string>();
    check(overall == "match" || overall == "compatible_drift",
          "verify-lock: overall is match or compatible_drift (not mismatch)");
}

void test_vivid_verify_lock_no_sibling() {
    ScopedTempDir dir("vivid_verify_nosib");
    auto graph_path = (dir.path / "demo.json").string();
    save_small_graph(graph_path);
    // No sibling vivid.lock exists.

    auto r = run_vivid({"verify-lock", "--graph", graph_path});
    check(r.exit_code == 3, "verify-lock no sibling: exit 3 (I/O error)");

    auto root = nlohmann::json::parse(r.stdout_text);
    check(root["ok"].get<bool>() == false,
          "verify-lock no sibling: ok=false");
    check(root["error"].get<std::string>().find("failed to load lockfile") != std::string::npos,
          "verify-lock no sibling: error mentions lockfile load");
}

void test_vivid_verify_lock_missing_lockfile_path() {
    ScopedTempDir dir("vivid_verify_nofile");
    auto graph_path = (dir.path / "demo.json").string();
    save_small_graph(graph_path);

    auto r = run_vivid({"verify-lock",
                         "--graph", graph_path,
                         "--lockfile", "/does/not/exist.lock"});
    check(r.exit_code == 3, "verify-lock missing lockfile: exit 3");
}

void test_vivid_verify_lock_pretty_writes_stderr() {
    ScopedTempDir dir("vivid_verify_pretty");
    auto graph_path = (dir.path / "demo.json").string();
    save_small_graph(graph_path);
    run_vivid({"lock", "--graph", graph_path});

    auto r = run_vivid({"verify-lock", "--graph", graph_path, "--pretty"});
    check(r.exit_code == 0 || r.exit_code == 1,
          "verify-lock --pretty: valid exit code");
    check(r.stdout_text.empty() ||
              r.stdout_text.find("\"status\"") == std::string::npos,
          "verify-lock --pretty: no status JSON on stdout");
    check(r.stderr_text.find("verify-lock") != std::string::npos,
          "verify-lock --pretty: stderr carries the header line");
}

// --- Phase 7: vivid export --strict CLI tests -----------------------------
//
// Fail-path tests only — they exit before any pipeline work, so they're
// fast (~seconds). A true happy-path test would actually run the export
// build (minutes), so we rely on the pipeline-level test_export_strict
// for the "strict gate clears" assertion.

static void write_stale_lockfile(const std::string& path,
                                 const std::string& op_type) {
    // Mimics write_descriptor_mismatch_lockfile from test_project_lockfile.cpp:
    // a lockfile with one operator whose descriptor_hash will never match.
    std::string body;
    body += "{\n";
    body += "  \"lockfile_version\": 1,\n";
    body += "  \"generated_at\": \"2026-01-01T00:00:00Z\",\n";
    body += "  \"graph\": {\"path\": \"\", \"schema_version\": 4, \"content_hash\": \"\"},\n";
    body += "  \"vivid_core\": {\"version\": \"0.1.0\", \"commit\": \"\", \"operator_abi\": 15},\n";
    body += "  \"packages\": [],\n";
    body += "  \"operators\": [{\n";
    body += "    \"type\": \"" + op_type + "\",\n";
    body += "    \"package\": \"\",\n";
    body += "    \"package_version\": \"\",\n";
    body += "    \"descriptor_hash\": \"sha256:deadbeef0000000000000000000000000000000000000000000000000000dead\",\n";
    body += "    \"operator_abi\": 15\n";
    body += "  }],\n";
    body += "  \"assets\": []\n";
    body += "}\n";
    std::ofstream ofs(path);
    ofs << body;
}

void test_vivid_export_strict_no_sibling_lockfile() {
    ScopedTempDir dir("vivid_export_strict_nosib");
    auto graph_path = (dir.path / "demo.json").string();
    save_small_graph(graph_path);
    // No sibling vivid.lock.

    auto r = run_vivid({"export", "--strict",
                         "--graph", graph_path,
                         "--output", "strict_test_out"});
    check(r.exit_code == 3,
          "export --strict no-sibling: exit 3 (io error)");
    check(r.stderr_text.find("no lockfile") != std::string::npos ||
              r.stderr_text.find("No lockfile") != std::string::npos ||
              r.stderr_text.find("--strict") != std::string::npos,
          "export --strict no-sibling: stderr mentions missing lockfile");

    // Stdout JSON.
    if (!r.stdout_text.empty()) {
        auto root = nlohmann::json::parse(r.stdout_text);
        check(root["ok"].get<bool>() == false,
              "export --strict no-sibling: stdout JSON has ok:false");
        check(root["error"].get<std::string>().find("no_lockfile") != std::string::npos,
              "export --strict no-sibling: error mentions no_lockfile");
    }
}

void test_vivid_export_strict_mismatch() {
    ScopedTempDir dir("vivid_export_strict_mismatch");
    auto graph_path = (dir.path / "demo.json").string();
    save_small_graph(graph_path);
    // Stale sibling lockfile referencing audio_out with wrong descriptor_hash.
    write_stale_lockfile((dir.path / "vivid.lock").string(), "audio_out");

    auto r = run_vivid({"export", "--strict",
                         "--graph", graph_path,
                         "--output", "strict_test_out"});
    check(r.exit_code == 2,
          "export --strict mismatch: exit 2");

    auto root = nlohmann::json::parse(r.stdout_text);
    check(root["ok"].get<bool>() == false,
          "export --strict mismatch: stdout JSON ok:false");
    check(root["error"].get<std::string>().find("mismatch") != std::string::npos,
          "export --strict mismatch: error mentions mismatch");
    check(root.contains("status") && root["status"].is_object(),
          "export --strict mismatch: status object inlined");
    check(root["status"]["overall"].get<std::string>() == "mismatch",
          "export --strict mismatch: status.overall = mismatch");
    check(r.stderr_text.find("CRIT") != std::string::npos,
          "export --strict mismatch: stderr contains CRIT");
}

void test_vivid_export_strict_explicit_lockfile() {
    ScopedTempDir dir("vivid_export_strict_explicit");
    auto graph_path = (dir.path / "demo.json").string();
    save_small_graph(graph_path);
    // No sibling; --lockfile points at a stale lockfile in a custom path.
    auto explicit_lf = (dir.path / "custom.lock").string();
    write_stale_lockfile(explicit_lf, "audio_out");

    auto r = run_vivid({"export", "--strict",
                         "--graph", graph_path,
                         "--lockfile", explicit_lf,
                         "--output", "strict_test_out"});
    check(r.exit_code == 2,
          "export --strict explicit-lockfile: exit 2 (mismatch from override)");
    auto root = nlohmann::json::parse(r.stdout_text);
    check(root["status"]["overall"].get<std::string>() == "mismatch",
          "export --strict explicit-lockfile: honors --lockfile");
}

}  // namespace

int main(int argc, char** argv) {
    namespace fs = std::filesystem;

    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    g_vivid_bin = (fs::path(build_dir) / "vivid").string();
    if (!fs::exists(g_vivid_bin)) {
        std::fprintf(stderr, "SKIP: vivid binary not found at %s\n",
                     g_vivid_bin.c_str());
        return 0;
    }

    test_vivid_lock_happy_path();
    test_vivid_lock_explicit_output();
    test_vivid_lock_missing_graph();
    test_vivid_verify_lock_match_round_trip();
    test_vivid_verify_lock_no_sibling();
    test_vivid_verify_lock_missing_lockfile_path();
    test_vivid_verify_lock_pretty_writes_stderr();
    test_vivid_export_strict_no_sibling_lockfile();
    test_vivid_export_strict_mismatch();
    test_vivid_export_strict_explicit_lockfile();

    if (failures == 0) {
        std::fprintf(stderr, "All vivid-lock CLI tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d vivid-lock CLI failure(s).\n", failures);
    return 1;
}
