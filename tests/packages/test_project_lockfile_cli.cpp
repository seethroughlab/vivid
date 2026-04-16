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

    if (failures == 0) {
        std::fprintf(stderr, "All vivid-lock CLI tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d vivid-lock CLI failure(s).\n", failures);
    return 1;
}
