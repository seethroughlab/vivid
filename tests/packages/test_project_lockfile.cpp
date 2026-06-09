// test_project_lockfile.cpp — ProjectLockfile JSON round-trip and validation
#include "runtime/packages/project_lockfile.h"

#include "common/hash_util.h"
#include "operator_api/types.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/control_server_internal.h"
#include "runtime/control/runtime_api.h"
#include "runtime/core/build_console.h"
#include "runtime/core/runtime_core.h"
#include "runtime/core/settings.h"
#include "runtime/core/tool_discovery.h"
#include "runtime/core/workspace_manager.h"
#include "runtime/assets/asset_library.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/graph_snapshot_builder.h"
#include "runtime/operators/builtin_operators.h"
#include "runtime/operators/operator_info_cache.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/packages/package_manager.h"
#include "runtime/packages/package_manager_internal.h"
#include "runtime/platform/process_runner.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <string>

#include "test_helpers.h"

using namespace vivid;

namespace {

ProjectLockfile make_full_fixture() {
    ProjectLockfile lf;
    lf.lockfile_version = 1;
    lf.generated_at     = "2026-04-16T14:32:00Z";

    lf.graph.path            = "demo.json";
    lf.graph.schema_version  = 4;
    lf.graph.content_hash    = "sha256:abc123";

    lf.vivid_core.version       = "0.1.0";
    lf.vivid_core.commit        = "dev-commit";
    lf.vivid_core.operator_abi  = 1;

    LockfilePackage pkg;
    pkg.name         = "vivid-wavetable";
    pkg.version      = "1.2.0";
    pkg.vivid_core   = ">=0.1.0 <0.2.0";
    pkg.source.kind   = "git";
    pkg.source.url    = "https://github.com/example/vivid-wavetable";
    pkg.source.commit = "deadbeef";
    pkg.linked       = false;
    pkg.linked_path  = "";
    lf.packages.push_back(pkg);

    LockfileOperator op;
    op.type             = "WavetableSynth";
    op.package          = "vivid-wavetable";
    op.package_version  = "1.2.0";
    op.descriptor_hash  = "sha256:desc";
    op.operator_abi     = 1;
    lf.operators.push_back(op);

    LockfileAsset asset;
    asset.asset_id     = "workspace:wavetable:foo";
    asset.kind         = "wavetable";
    asset.path         = "assets/wavetables/foo.wav";
    asset.content_hash = "sha256:asset";
    lf.assets.push_back(asset);

    return lf;
}

void write_file(const std::filesystem::path& p, const std::string& contents) {
    std::ofstream out(p);
    out << contents;
}

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::string s((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
    return s;
}

void test_round_trip_full() {
    ScopedTempDir dir;
    auto path = dir / "vivid.lock";

    auto original = make_full_fixture();
    auto save_err = save_lockfile(path, original);
    check(save_err.ok(), "round-trip full: save succeeds");

    auto result = load_lockfile(path);
    check(result.ok(), "round-trip full: load succeeds");
    check(result.lockfile == original, "round-trip full: structs equal");
}

void test_round_trip_minimal() {
    ScopedTempDir dir;
    auto path = dir / "vivid.lock";

    ProjectLockfile minimal;  // defaults: version 1, empty everything
    auto save_err = save_lockfile(path, minimal);
    check(save_err.ok(), "round-trip minimal: save succeeds");

    auto result = load_lockfile(path);
    check(result.ok(), "round-trip minimal: load succeeds");
    check(result.lockfile.lockfile_version == 1, "round-trip minimal: version preserved");
    check(result.lockfile.packages.empty(), "round-trip minimal: empty packages");
    check(result.lockfile.operators.empty(), "round-trip minimal: empty operators");
    check(result.lockfile.assets.empty(), "round-trip minimal: empty assets");
    check(result.lockfile == minimal, "round-trip minimal: struct equality");
}

void test_canonical_key_order() {
    ScopedTempDir dir;
    auto path = dir / "vivid.lock";

    auto lf = make_full_fixture();
    auto save_err = save_lockfile(path, lf);
    check(save_err.ok(), "canonical order: save succeeds");

    auto text = read_file(path);
    const char* keys[] = {
        "lockfile_version",
        "generated_at",
        "graph",
        "vivid_core",
        "packages",
        "operators",
        "assets",
    };
    size_t prev = 0;
    bool ordered = true;
    for (const char* k : keys) {
        size_t pos = text.find(std::string("\"") + k + "\"");
        if (pos == std::string::npos || pos < prev) {
            ordered = false;
            break;
        }
        prev = pos;
    }
    check(ordered, "canonical order: top-level keys appear in fixed order");
}

void test_forward_version_rejected() {
    ScopedTempDir dir;
    auto path = dir / "vivid.lock";
    write_file(path, R"({"lockfile_version": 999})");

    auto result = load_lockfile(path);
    check(!result.ok(), "forward version: load fails");
    check(result.error.kind == LockfileError::Kind::UnsupportedVersion,
          "forward version: UnsupportedVersion reported");
}

void test_malformed_json_parse_error() {
    ScopedTempDir dir;
    auto path = dir / "vivid.lock";
    write_file(path, "not json at all {{{");

    auto result = load_lockfile(path);
    check(!result.ok(), "malformed JSON: load fails");
    check(result.error.kind == LockfileError::Kind::ParseError,
          "malformed JSON: ParseError reported");
}

void test_missing_lockfile_version() {
    ScopedTempDir dir;
    auto path = dir / "vivid.lock";
    write_file(path, "{}");

    auto result = load_lockfile(path);
    check(!result.ok(), "missing version: load fails");
    check(result.error.kind == LockfileError::Kind::InvalidShape,
          "missing version: InvalidShape reported");
}

void test_missing_optional_fields_load_defaults() {
    ScopedTempDir dir;
    auto path = dir / "vivid.lock";
    write_file(path, R"({"lockfile_version": 1, "packages": []})");

    auto result = load_lockfile(path);
    check(result.ok(), "missing optionals: load succeeds");
    check(result.lockfile.generated_at.empty(), "missing optionals: generated_at default empty");
    check(result.lockfile.graph.path.empty(),    "missing optionals: graph.path default empty");
    check(result.lockfile.vivid_core.version.empty(),
          "missing optionals: core.version default empty");
    check(result.lockfile.operators.empty(), "missing optionals: operators default empty");
    check(result.lockfile.assets.empty(),    "missing optionals: assets default empty");
}

void test_unknown_top_level_keys_ignored() {
    ScopedTempDir dir;
    auto path = dir / "vivid.lock";
    write_file(path,
               R"({"lockfile_version": 1, "notes": "hi", "future_field": {"x": 1}})");

    auto result = load_lockfile(path);
    check(result.ok(), "unknown keys: load succeeds");
    check(result.lockfile.lockfile_version == 1, "unknown keys: version preserved");
}

void test_io_error_on_missing_file() {
    ScopedTempDir dir;
    auto path = dir / "does_not_exist.lock";

    auto result = load_lockfile(path);
    check(!result.ok(), "missing file: load fails");
    check(result.error.kind == LockfileError::Kind::IoError,
          "missing file: IoError reported");
}

void test_zero_version_rejected() {
    ScopedTempDir dir;
    auto path = dir / "vivid.lock";
    write_file(path, R"({"lockfile_version": 0})");

    auto result = load_lockfile(path);
    check(!result.ok(), "zero version: load fails");
    check(result.error.kind == LockfileError::Kind::InvalidShape,
          "zero version: InvalidShape reported");
}

void test_sha256_hex_abc() {
    // FIPS 180-4 / standard NIST test vector for sha256("abc").
    check(sha256_hex("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha256_hex(\"abc\") matches NIST test vector");
}

void test_sha256_hex_length() {
    // All SHA-256 digests are 64 hex characters.
    check(sha256_hex("").size() == 64,        "sha256_hex(empty) is 64 chars");
    check(sha256_hex("abc").size() == 64,      "sha256_hex(abc) is 64 chars");
    check(sha256_hex(std::string(1000, 'a')).size() == 64,
          "sha256_hex(1000 * 'a') is 64 chars");
}

void test_sha256_hex_multi_block() {
    // FIPS 180-4 appendix test vector — two-block message.
    // sha256 of "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
    check(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "sha256_hex two-block NIST vector");
}

// --- Phase 2: generation + canonicalize_graph_hash ------------------------

// Minimal fixture that holds a PackageCompiler + OperatorRegistry + PackageManager.
// Uses throwaway tmp dirs so pm.list() discovers nothing by default.
struct LockfileGenFixture {
    ScopedTempDir workspace;
    OperatorRegistry registry;
    PackageCompiler compiler{workspace.str(), workspace.str()};
    PackageManager  pm{compiler, registry};
};

void test_build_lockfile_empty_graph() {
    LockfileGenFixture fx;
    Graph g;

    auto lf = build_lockfile_for_graph(g, fx.pm, fx.registry);

    check(lf.lockfile_version == LOCKFILE_VERSION,
          "empty graph: lockfile_version populated");
    check(!lf.generated_at.empty(),
          "empty graph: generated_at populated");
    check(lf.vivid_core.version == VIVID_CORE_VERSION,
          "empty graph: vivid_core.version populated");
    check(lf.vivid_core.operator_abi ==
              static_cast<int>(VIVID_OPERATOR_ABI_VERSION),
          "empty graph: vivid_core.operator_abi populated");
    // Finalization pass populates vivid_core.commit from VIVID_CORE_COMMIT
    // (git sha at configure time). Non-git builds still see an empty
    // string; both shapes are valid here — the stronger 40-char assertion
    // lives in test_build_lockfile_vivid_core_commit.
    check(lf.vivid_core.commit.empty() || lf.vivid_core.commit.size() == 40,
          "empty graph: vivid_core.commit is either empty or a 40-char sha");
    check(lf.packages.empty(),  "empty graph: packages empty");
    check(lf.operators.empty(), "empty graph: operators empty");
    check(lf.assets.empty(),    "empty graph: assets empty");
}

void test_build_lockfile_vivid_core_commit() {
    // Finalization pass: cmake/git_version.cmake should define
    // VIVID_CORE_COMMIT at configure time. When built from a git worktree,
    // lf.vivid_core.commit is a 40-char sha. Non-git builds leave it empty
    // (still valid — tarball users don't get a commit pin).
    LockfileGenFixture fx;
    Graph g;
    auto lf = build_lockfile_for_graph(g, fx.pm, fx.registry);

    const std::string& commit = lf.vivid_core.commit;
    if (commit.empty()) {
        std::fprintf(stderr, "  SKIP: VIVID_CORE_COMMIT unset (non-git build)\n");
        return;
    }
    check(commit.size() == 40,
          "build_lockfile: vivid_core.commit is a 40-char sha when built from git");
    for (char c : commit) {
        const bool is_hex = (c >= '0' && c <= '9') ||
                            (c >= 'a' && c <= 'f') ||
                            (c >= 'A' && c <= 'F');
        check(is_hex,
              "build_lockfile: vivid_core.commit contains only hex characters");
        if (!is_hex) break;
    }
}

void test_build_lockfile_graph_hash_populated() {
    LockfileGenFixture fx;
    Graph g;

    auto lf = build_lockfile_for_graph(g, fx.pm, fx.registry);
    check(lf.graph.content_hash.rfind("sha256:", 0) == 0,
          "build_lockfile: graph.content_hash starts with sha256:");
    check(lf.graph.content_hash.size() == 7 + 64,
          "build_lockfile: graph.content_hash is sha256: + 64 hex");
}

void test_build_lockfile_operators_unregistered_types() {
    LockfileGenFixture fx;
    Graph g;
    g.add_node("n1", "ZetaOp");
    g.add_node("n2", "AlphaOp");
    g.add_node("n3", "ZetaOp");  // duplicate type

    auto lf = build_lockfile_for_graph(g, fx.pm, fx.registry);

    check(lf.operators.size() == 2,
          "build_lockfile: dedupes operator types across nodes");
    check(lf.operators[0].type == "AlphaOp",
          "build_lockfile: operators sorted (AlphaOp first)");
    check(lf.operators[1].type == "ZetaOp",
          "build_lockfile: operators sorted (ZetaOp second)");
    check(lf.operators[0].package.empty(),
          "build_lockfile: unregistered type has empty package");
    check(lf.operators[0].descriptor_hash.empty(),
          "build_lockfile: descriptor_hash empty for unregistered type");
}

void test_build_lockfile_sort_stability_by_insertion_order() {
    // Two graphs with the same set of node types in different insertion
    // orders must produce the same operators[] sequence.
    LockfileGenFixture fx;

    Graph g1;
    g1.add_node("a", "Foo");
    g1.add_node("b", "Bar");
    g1.add_node("c", "Baz");

    Graph g2;
    g2.add_node("x", "Baz");
    g2.add_node("y", "Foo");
    g2.add_node("z", "Bar");

    auto lf1 = build_lockfile_for_graph(g1, fx.pm, fx.registry);
    auto lf2 = build_lockfile_for_graph(g2, fx.pm, fx.registry);

    check(lf1.operators.size() == 3, "sort stability: 3 unique operator types");
    bool same_order = lf1.operators.size() == lf2.operators.size();
    for (size_t i = 0; same_order && i < lf1.operators.size(); ++i) {
        if (lf1.operators[i].type != lf2.operators[i].type) same_order = false;
    }
    check(same_order,
          "sort stability: operator order independent of node insertion order");
}

void test_canonicalize_graph_hash_stable() {
    Graph g;
    g.add_node("a", "Foo");
    g.add_node("b", "Bar");
    auto h1 = canonicalize_graph_hash(g);
    auto h2 = canonicalize_graph_hash(g);
    check(h1 == h2, "canonicalize_graph_hash: stable across calls");
}

void test_canonicalize_graph_hash_sensitive_to_nodes() {
    Graph g1;
    g1.add_node("a", "Foo");

    Graph g2;
    g2.add_node("a", "Foo");
    g2.add_node("b", "Bar");

    check(canonicalize_graph_hash(g1) != canonicalize_graph_hash(g2),
          "canonicalize_graph_hash: changes when a node is added");
}

void test_canonicalize_graph_hash_prefix_and_length() {
    Graph g;
    auto h = canonicalize_graph_hash(g);
    check(h.rfind("sha256:", 0) == 0, "canonicalize_graph_hash: has sha256: prefix");
    check(h.size() == 7 + 64, "canonicalize_graph_hash: 7 + 64 chars total");
}

// --- Phase 3: verify_lockfile classifications -----------------------------

static const LockfileFinding* find_finding(const LockfileStatus& status,
                                           const char* id) {
    for (const auto& f : status.findings) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

void test_verify_empty_lockfile_empty_graph() {
    LockfileGenFixture fx;
    Graph g;
    ProjectLockfile lf;  // defaults: version 1, no packages, no operators

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    check(status.overall == LockfileOverall::Match,
          "verify empty/empty: overall = Match");
    check(status.findings.empty(),
          "verify empty/empty: no findings");
}

void test_verify_missing_package() {
    LockfileGenFixture fx;
    Graph g;
    ProjectLockfile lf;

    LockfilePackage p;
    p.name    = "nonexistent-test-package";
    p.version = "1.0.0";
    lf.packages.push_back(p);

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    const auto* f = find_finding(status, lockfile_finding::kMissingPackage);
    check(f != nullptr, "verify missing package: finding emitted");
    if (f) {
        check(f->severity == LockfileSeverity::Critical,
              "verify missing package: severity Critical");
        check(f->subject == "nonexistent-test-package",
              "verify missing package: subject is package name");
    }
    check(status.overall == LockfileOverall::Mismatch,
          "verify missing package: overall = Mismatch");
}

void test_verify_missing_operator() {
    LockfileGenFixture fx;
    Graph g;
    ProjectLockfile lf;

    LockfileOperator o;
    o.type = "NonexistentTestOp";
    lf.operators.push_back(o);

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    const auto* f = find_finding(status, lockfile_finding::kMissingOperator);
    check(f != nullptr, "verify missing operator: finding emitted");
    if (f) check(f->severity == LockfileSeverity::Critical,
                 "verify missing operator: severity Critical");
    check(status.overall == LockfileOverall::Mismatch,
          "verify missing operator: overall = Mismatch");
}

void test_verify_vivid_core_version_mismatch() {
    LockfileGenFixture fx;
    Graph g;
    ProjectLockfile lf;
    lf.vivid_core.version = "0.0.1-doesnt-match";

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    const auto* f = find_finding(status, lockfile_finding::kVividCoreVersionMismatch);
    check(f != nullptr, "verify core mismatch: finding emitted");
    if (f) check(f->severity == LockfileSeverity::Warning,
                 "verify core mismatch: severity Warning");
    check(status.overall == LockfileOverall::CompatibleDrift,
          "verify core mismatch: overall = CompatibleDrift");
}

void test_verify_core_abi_mismatch() {
    LockfileGenFixture fx;
    Graph g;
    ProjectLockfile lf;
    lf.vivid_core.operator_abi = 999;  // well outside the real ABI

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    const auto* f = find_finding(status, lockfile_finding::kAbiMismatch);
    check(f != nullptr, "verify core ABI mismatch: finding emitted");
    if (f) check(f->severity == LockfileSeverity::Critical,
                 "verify core ABI mismatch: severity Critical");
    check(status.overall == LockfileOverall::Mismatch,
          "verify core ABI mismatch: overall = Mismatch");
}

void test_verify_graph_content_drift() {
    LockfileGenFixture fx;
    Graph g;
    g.add_node("a", "Foo");

    ProjectLockfile lf;
    lf.graph.content_hash = "sha256:deadbeef0000000000000000000000000000000000000000000000000000dead";

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    const auto* f = find_finding(status, lockfile_finding::kGraphContentDrift);
    check(f != nullptr, "verify graph drift: finding emitted");
    if (f) check(f->severity == LockfileSeverity::Info,
                 "verify graph drift: severity Info");
    check(status.overall == LockfileOverall::CompatibleDrift,
          "verify graph drift: overall = CompatibleDrift");
}

void test_verify_overall_precedence_critical_wins() {
    // A graph drift (Info) + a missing package (Critical) = Mismatch.
    LockfileGenFixture fx;
    Graph g;
    ProjectLockfile lf;
    lf.graph.content_hash = "sha256:deadbeef0000000000000000000000000000000000000000000000000000dead";
    LockfilePackage p;
    p.name = "some-other-missing-pkg";
    p.version = "1.0.0";
    lf.packages.push_back(p);

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    check(status.overall == LockfileOverall::Mismatch,
          "verify precedence: critical outranks info");
}

void test_verify_generate_then_verify_round_trip() {
    LockfileGenFixture fx;
    Graph g;
    g.add_node("n1", "SomeType");

    auto lf = build_lockfile_for_graph(g, fx.pm, fx.registry);
    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);

    // SomeType isn't in the registry, so it shows up as missing_operator.
    // That's still the expected behavior of verify against the generated lock.
    // For a true match, the type must be registered. Assert behavior shape:
    check(!status.findings.empty() || status.overall == LockfileOverall::Match,
          "verify generate-then-verify: status is well-formed");
    // Regardless of registered types, the generated lock's own packages list
    // should verify clean (empty, since no package backs the operator).
    bool any_missing_package = false;
    for (const auto& f : status.findings)
        if (f.id == lockfile_finding::kMissingPackage) any_missing_package = true;
    check(!any_missing_package,
          "verify generate-then-verify: no missing_package findings (none were locked)");
}

void test_verify_status_to_json_shape() {
    LockfileStatus s;
    s.overall = LockfileOverall::Mismatch;
    s.findings.push_back({"missing_package", LockfileSeverity::Critical,
                          "pkg-a", "locked 1.0.0", "install pkg-a@1.0.0"});

    auto text = lockfile_status_to_json(s);
    auto root = nlohmann::json::parse(text);

    check(root["overall"].get<std::string>() == "mismatch",
          "status JSON: overall serialized");
    check(root["findings"].is_array() && root["findings"].size() == 1,
          "status JSON: findings is a 1-entry array");
    const auto& f = root["findings"][0];
    check(f["id"].get<std::string>() == "missing_package",
          "status JSON: finding id serialized");
    check(f["severity"].get<std::string>() == "critical",
          "status JSON: finding severity serialized");
    check(f["subject"].get<std::string>() == "pkg-a",
          "status JSON: finding subject serialized");
    check(f["suggestion"].get<std::string>() == "install pkg-a@1.0.0",
          "status JSON: finding suggestion serialized");
}

// --- Phase 2: RuntimeAPI::write_project_lockfile round-trip ---------------

void test_runtime_api_write_project_lockfile_round_trip() {
    LockfileGenFixture fx;

    // Write a small graph to disk.
    ScopedTempDir dir;
    Graph source_graph;
    source_graph.add_node("n1", "Foo");
    source_graph.add_node("n2", "Bar");
    auto graph_path = dir.file_str("demo.json");
    check(source_graph.save(graph_path.c_str()),
          "runtime_api round-trip: source graph saves");

    // Construct a minimal RuntimeAPI (graph, core, audio_engine, registry).
    Graph api_graph;
    RuntimeCore runtime;
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    // Default output path: sibling vivid.lock.
    auto result = api.write_project_lockfile(fx.pm, graph_path, "");
    check(result.ok, "runtime_api round-trip: write_project_lockfile succeeds");

    auto expected_out = (dir.path / "vivid.lock").string();
    check(result.message == expected_out,
          "runtime_api round-trip: returned path is sibling vivid.lock");

    // Load it back and verify shape.
    auto load_result = load_lockfile(expected_out);
    check(load_result.ok(), "runtime_api round-trip: lockfile re-loads");

    const auto& lf = load_result.lockfile;
    check(lf.lockfile_version == LOCKFILE_VERSION,
          "round-trip: lockfile_version preserved");
    check(lf.graph.path == graph_path,
          "round-trip: graph.path matches input");
    check(lf.operators.size() == 2,
          "round-trip: 2 operator types listed");
    check(lf.operators[0].type == "Bar",
          "round-trip: operators[0] = Bar (sorted)");
    check(lf.operators[1].type == "Foo",
          "round-trip: operators[1] = Foo (sorted)");
}

void test_runtime_api_write_project_lockfile_explicit_output() {
    LockfileGenFixture fx;
    ScopedTempDir dir;

    Graph source_graph;
    source_graph.add_node("n1", "Foo");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());

    Graph api_graph;
    RuntimeCore runtime;
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    auto explicit_out = dir.file_str("custom.lock");
    auto result = api.write_project_lockfile(fx.pm, graph_path, explicit_out);
    check(result.ok, "runtime_api explicit output: succeeds");
    check(result.message == explicit_out,
          "runtime_api explicit output: returns given path");
    check(std::filesystem::exists(explicit_out),
          "runtime_api explicit output: file exists");
}

void test_runtime_api_write_project_lockfile_missing_graph() {
    LockfileGenFixture fx;
    Graph api_graph;
    RuntimeCore runtime;
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    auto result = api.write_project_lockfile(fx.pm, "/does/not/exist.json", "");
    check(!result.ok, "runtime_api missing graph: fails");
    check(result.message.find("failed to load graph") != std::string::npos,
          "runtime_api missing graph: message mentions load failure");
}

void test_runtime_api_write_project_lockfile_empty_graph_path() {
    LockfileGenFixture fx;
    Graph api_graph;
    RuntimeCore runtime;
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    auto result = api.write_project_lockfile(fx.pm, "", "");
    check(!result.ok, "runtime_api empty graph_path: fails");
}

// --- Phase 3: RuntimeAPI verify + dependency status -----------------------

void test_runtime_api_verify_project_lockfile_round_trip() {
    LockfileGenFixture fx;
    ScopedTempDir dir;

    Graph source_graph;
    source_graph.add_node("n1", "Foo");
    auto graph_path = dir.file_str("demo.json");
    check(source_graph.save(graph_path.c_str()),
          "verify round-trip: source graph saves");

    Graph api_graph;
    RuntimeCore runtime;
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    // Write a lockfile.
    auto write_result = api.write_project_lockfile(fx.pm, graph_path, "");
    check(write_result.ok, "verify round-trip: write succeeded");
    const std::string lockfile_path = write_result.message;

    // Verify it.
    auto verify_result = api.verify_project_lockfile(fx.pm, graph_path, lockfile_path);
    check(verify_result.ok, "verify round-trip: verify call succeeded");

    auto payload = nlohmann::json::parse(verify_result.message);
    // No packages were locked (Foo isn't from a package), so no missing_package.
    // Foo is also not in the registry, so we expect a missing_operator finding.
    // The operator came from the generator's "operators" list, so verify against
    // the same environment should still classify it as missing_operator.
    // Assert the JSON shape regardless of operator resolution:
    check(payload.contains("overall") && payload["overall"].is_string(),
          "verify round-trip: overall present");
    check(payload.contains("findings") && payload["findings"].is_array(),
          "verify round-trip: findings array present");
}

void test_runtime_api_verify_project_lockfile_missing_lockfile() {
    LockfileGenFixture fx;
    ScopedTempDir dir;

    Graph source_graph;
    source_graph.add_node("n1", "Foo");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());

    Graph api_graph;
    RuntimeCore runtime;
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    auto result = api.verify_project_lockfile(fx.pm, graph_path,
                                              dir.file_str("not-there.lock"));
    check(!result.ok, "verify missing lockfile: call fails");
    check(result.message.find("failed to load lockfile") != std::string::npos,
          "verify missing lockfile: message mentions load failure");
}

void test_runtime_api_get_project_dependency_status_no_lockfile() {
    LockfileGenFixture fx;
    ScopedTempDir dir;

    Graph source_graph;
    source_graph.add_node("n1", "Foo");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());

    Graph api_graph;
    RuntimeCore runtime;
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    auto result = api.get_project_dependency_status(fx.pm, graph_path);
    check(result.ok, "dep status no-lockfile: call succeeds");

    auto payload = nlohmann::json::parse(result.message);
    check(payload["overall"].get<std::string>() == "no_lockfile",
          "dep status no-lockfile: overall = no_lockfile");
    check(payload["findings"].is_array() && payload["findings"].empty(),
          "dep status no-lockfile: findings empty");
}

// --- Phase 0: git-metadata capture ---------------------------------------

// Initialize a throwaway git repo at `path` with one commit. Returns true on
// success. Writes a vivid-package.json manifest if requested. Safe to call
// only in temp dirs — runs real git commands.
static bool init_git_repo_at(const std::filesystem::path& path,
                             const std::string& origin_url,
                             bool make_commit = true) {
    const std::string git_exe = find_tool("git");
    if (git_exe.empty()) return false;

    auto run = [&](std::vector<std::string> args,
                    const std::filesystem::path& cwd = {}) {
        ProcessRunOptions opts;
        opts.argv.reserve(args.size() + 1);
        opts.argv.push_back(git_exe);
        for (auto& a : args) opts.argv.push_back(std::move(a));
        opts.working_directory = cwd.empty() ? path.string() : cwd.string();
        opts.timeout_ms = 10000;
        opts.env_overrides.push_back({"GIT_AUTHOR_NAME",  "Test"});
        opts.env_overrides.push_back({"GIT_AUTHOR_EMAIL", "test@example.com"});
        opts.env_overrides.push_back({"GIT_COMMITTER_NAME",  "Test"});
        opts.env_overrides.push_back({"GIT_COMMITTER_EMAIL", "test@example.com"});
        return run_process(opts);
    };

    auto r = run({"init", "-q"});
    if (!r.launched || r.exit_code != 0) return false;
    r = run({"config", "commit.gpgsign", "false"});
    if (!r.launched || r.exit_code != 0) return false;
    if (!origin_url.empty()) {
        r = run({"remote", "add", "origin", origin_url});
        if (!r.launched || r.exit_code != 0) return false;
    }
    if (make_commit) {
        r = run({"add", "-A"});
        if (!r.launched || r.exit_code != 0) return false;
        r = run({"commit", "--allow-empty", "-q", "-m", "init"});
        if (!r.launched || r.exit_code != 0) return false;
    }
    return true;
}

static void write_minimal_manifest(const std::filesystem::path& dir,
                                   const std::string& name,
                                   const std::string& version) {
    std::ofstream ofs(dir / "vivid-package.json");
    ofs << "{\n"
           "  \"name\": \"" << name << "\",\n"
           "  \"version\": \"" << version << "\",\n"
           "  \"vivid_core\": \">=0.1.0 <2.0.0\",\n"
           "  \"operators\": []\n"
           "}\n";
}

void test_capture_git_metadata_reads_commit_and_url() {
    ScopedTempDir repo_dir("vivid_gitcap");
    write_minimal_manifest(repo_dir.path, "gitcap-pkg", "0.1.0");
    const std::string origin = "https://example.test/gitcap-pkg.git";
    if (!init_git_repo_at(repo_dir.path, origin)) {
        std::fprintf(stderr, "  SKIP: git not available, skipping git-metadata tests\n");
        return;
    }

    PackageInfo info;
    package_manager_internal::capture_git_metadata(repo_dir.path.string(), info);

    check(info.git_commit.size() == 40,
          "capture: git_commit is a 40-char sha");
    check(info.source_url == origin,
          "capture: source_url matches remote.origin.url");
    check(!info.dirty,
          "capture: freshly-committed repo is not dirty");
}

void test_capture_git_metadata_detects_dirty() {
    ScopedTempDir repo_dir("vivid_gitdirty");
    write_minimal_manifest(repo_dir.path, "gitdirty-pkg", "0.1.0");
    if (!init_git_repo_at(repo_dir.path, "")) {
        std::fprintf(stderr, "  SKIP: git not available\n");
        return;
    }
    // Modify a tracked file after the commit.
    {
        std::ofstream ofs(repo_dir.path / "vivid-package.json", std::ios::app);
        ofs << "\n// dirty\n";
    }

    PackageInfo info;
    package_manager_internal::capture_git_metadata(repo_dir.path.string(), info);
    check(info.dirty, "capture: uncommitted changes flagged dirty");
}

void test_build_lockfile_populates_descriptor_hash() {
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    Graph g;
    g.add_node("a", "audio_out");

    auto lf = build_lockfile_for_graph(g, fx.pm, fx.registry);
    check(lf.operators.size() == 1, "descriptor hash wire: 1 operator listed");
    if (!lf.operators.empty()) {
        check(lf.operators[0].type == "audio_out",
              "descriptor hash wire: type is audio_out");
        check(lf.operators[0].descriptor_hash.rfind("sha256:", 0) == 0,
              "descriptor hash wire: descriptor_hash populated (sha256: prefix)");
        check(lf.operators[0].descriptor_hash.size() == 7 + 64,
              "descriptor hash wire: 7 + 64 chars total");
    }
}

void test_verify_emits_descriptor_hash_mismatch() {
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    Graph g;
    g.add_node("a", "audio_out");

    // Hand-build a lockfile with a stale descriptor_hash.
    ProjectLockfile lf;
    LockfileOperator o;
    o.type            = "audio_out";
    o.package         = "";
    o.operator_abi    = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);
    o.descriptor_hash = "sha256:deadbeef0000000000000000000000000000000000000000000000000000dead";
    lf.operators.push_back(o);

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    const auto* f = find_finding(status, lockfile_finding::kDescriptorHashMismatch);
    check(f != nullptr, "verify descriptor mismatch: finding emitted");
    if (f) check(f->severity == LockfileSeverity::Critical,
                 "verify descriptor mismatch: severity Critical");
    check(status.overall == LockfileOverall::Mismatch,
          "verify descriptor mismatch: overall = Mismatch");
}

void test_verify_skips_descriptor_hash_mismatch_when_empty() {
    // Old lockfiles without descriptor_hash must not trigger the check.
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    Graph g;
    g.add_node("a", "audio_out");

    ProjectLockfile lf;
    LockfileOperator o;
    o.type            = "audio_out";
    o.operator_abi    = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);
    o.descriptor_hash = "";  // pre-Phase-0 lockfile
    lf.operators.push_back(o);

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    const auto* f = find_finding(status, lockfile_finding::kDescriptorHashMismatch);
    check(f == nullptr,
          "verify descriptor skip: no finding when lockfile hash is empty");
}

void test_capture_git_metadata_non_git_dir_is_silent() {
    ScopedTempDir repo_dir("vivid_nongit");
    write_minimal_manifest(repo_dir.path, "nongit-pkg", "0.1.0");

    PackageInfo info;
    package_manager_internal::capture_git_metadata(repo_dir.path.string(), info);
    check(info.git_commit.empty(), "capture: non-git dir leaves commit empty");
    check(info.source_url.empty(), "capture: non-git dir leaves url empty");
    check(!info.dirty,             "capture: non-git dir not dirty");
}

// --- Phase 6a: load modes ------------------------------------------------

void test_parse_lockfile_load_mode_known_values() {
    check(parse_lockfile_load_mode("studio")   == LockfileLoadMode::Studio,
          "parse_lockfile_load_mode: studio");
    check(parse_lockfile_load_mode("strict")   == LockfileLoadMode::Strict,
          "parse_lockfile_load_mode: strict");
    check(parse_lockfile_load_mode("recovery") == LockfileLoadMode::Recovery,
          "parse_lockfile_load_mode: recovery");
}

void test_parse_lockfile_load_mode_unknown_defaults_to_studio() {
    check(parse_lockfile_load_mode("")       == LockfileLoadMode::Studio,
          "parse_lockfile_load_mode: empty -> Studio");
    check(parse_lockfile_load_mode("bogus")  == LockfileLoadMode::Studio,
          "parse_lockfile_load_mode: bogus -> Studio");
    check(parse_lockfile_load_mode("STRICT") == LockfileLoadMode::Studio,
          "parse_lockfile_load_mode: case-sensitive (STRICT != strict)");
}

void test_lockfile_load_mode_to_string_round_trips() {
    for (auto m : {LockfileLoadMode::Studio, LockfileLoadMode::Strict,
                    LockfileLoadMode::Recovery}) {
        check(parse_lockfile_load_mode(to_string(m)) == m,
              "lockfile_load_mode: to_string/parse round-trip");
    }
}

void test_runtime_core_lockfile_status_default() {
    RuntimeCore core;
    check(core.lockfile_status().overall == LockfileOverall::Match,
          "RuntimeCore: default lockfile_status overall is Match");
    check(core.lockfile_status().findings.empty(),
          "RuntimeCore: default lockfile_status has no findings");
    check(core.package_manager() == nullptr,
          "RuntimeCore: default package_manager is null");
}

void test_runtime_core_set_lockfile_status() {
    RuntimeCore core;
    LockfileStatus s;
    s.overall = LockfileOverall::Mismatch;
    s.findings.push_back({"missing_package", LockfileSeverity::Critical,
                          "pkg-a", "msg", ""});
    core.set_lockfile_status(s);
    check(core.lockfile_status().overall == LockfileOverall::Mismatch,
          "RuntimeCore: set_lockfile_status updates overall");
    check(core.lockfile_status().findings.size() == 1,
          "RuntimeCore: set_lockfile_status preserves findings");
}

// --- Phase 4: dispatch helper (unwrap_status_to_json) --------------------

void test_unwrap_status_to_json_inlines_valid_status() {
    // Simulates a CommandResult from verify_project_lockfile:
    // message is a JSON-serialized LockfileStatus.
    LockfileStatus st;
    st.overall = LockfileOverall::Match;
    CommandResult cmd;
    cmd.ok      = true;
    cmd.message = lockfile_status_to_json(st);

    auto body = unwrap_status_to_json(cmd);
    auto root = nlohmann::json::parse(body);

    check(root["ok"].get<bool>() == true,
          "unwrap: ok preserved");
    check(root.contains("status") && root["status"].is_object(),
          "unwrap: status is an inline object (not stringified)");
    check(root["status"]["overall"].get<std::string>() == "match",
          "unwrap: status fields addressable without second parse");
}

void test_unwrap_status_to_json_preserves_error_message() {
    CommandResult cmd;
    cmd.ok      = false;
    cmd.message = "boom";

    auto body = unwrap_status_to_json(cmd);
    auto root = nlohmann::json::parse(body);

    check(root["ok"].get<bool>() == false, "unwrap error: ok false");
    check(root["error"].get<std::string>() == "boom",
          "unwrap error: error carries message verbatim");
    check(!root.contains("status"),
          "unwrap error: no status key on error");
}

void test_unwrap_status_to_json_non_json_message_fallback() {
    // Defensive: if message somehow isn't valid JSON, preserve as string.
    CommandResult cmd;
    cmd.ok      = true;
    cmd.message = "not-json";

    auto body = unwrap_status_to_json(cmd);
    auto root = nlohmann::json::parse(body);

    check(root["ok"].get<bool>() == true,
          "unwrap non-json: ok preserved");
    check(root["status"].is_string() &&
              root["status"].get<std::string>() == "not-json",
          "unwrap non-json: status falls through to string");
}

void test_runtime_api_get_project_dependency_status_happy_path() {
    LockfileGenFixture fx;
    ScopedTempDir dir;

    Graph source_graph;
    source_graph.add_node("n1", "Foo");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());

    Graph api_graph;
    RuntimeCore runtime;
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    // Write the sibling lockfile first.
    auto write_result = api.write_project_lockfile(fx.pm, graph_path, "");
    check(write_result.ok, "dep status happy: write succeeded");

    auto result = api.get_project_dependency_status(fx.pm, graph_path);
    check(result.ok, "dep status happy: call succeeds");

    auto payload = nlohmann::json::parse(result.message);
    check(payload.contains("overall") && payload["overall"].is_string(),
          "dep status happy: overall present");
    // overall may be Match or CompatibleDrift depending on whether Foo resolves
    // in the registry. Both are acceptable; what matters is it's NOT
    // "no_lockfile" (the sibling exists).
    const auto overall = payload["overall"].get<std::string>();
    check(overall != "no_lockfile",
          "dep status happy: overall is not no_lockfile (sibling exists)");
}

// --- Phase 6a: load_graph integration with lockfile verify ---------------

// Build a hand-authored lockfile that triggers a descriptor_hash mismatch on
// the given operator type. Returns the lockfile path.
//
// NOTE: built-in operators have OperatorMapEntry.abi_version == 0 (the ABI is
// only populated for probed dylibs), so we can't use an ABI mismatch finding
// for these tests. descriptor_hash_mismatch works for builtins because Phase 0
// computes it from the in-memory descriptor, which is always available.
static std::string write_descriptor_mismatch_lockfile(
    const std::filesystem::path& dir,
    const std::string& op_type) {

    ProjectLockfile lf;
    lf.lockfile_version   = LOCKFILE_VERSION;
    lf.vivid_core.version = VIVID_CORE_VERSION;
    lf.vivid_core.operator_abi = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);
    LockfileOperator o;
    o.type            = op_type;
    o.operator_abi    = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);
    o.descriptor_hash = "sha256:deadbeef0000000000000000000000000000000000000000000000000000dead";
    lf.operators.push_back(o);

    auto path = (dir / "vivid.lock").string();
    save_lockfile(path, lf);
    return path;
}

void test_load_graph_studio_mode_runs_verify_no_disabling() {
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_load_studio");
    Graph source_graph;
    source_graph.add_node("a", "audio_out");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());
    write_descriptor_mismatch_lockfile(dir.path, "audio_out");

    Graph api_graph;
    RuntimeCore runtime;
    runtime.set_package_manager(&fx.pm);
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    bool has_gpu = false;
    bool has_aud = false;
    auto r = api.load_graph(graph_path, has_gpu, has_aud, "studio");
    check(r.ok, "load studio: succeeds");
    check(runtime.lockfile_status().overall == LockfileOverall::Mismatch,
          "load studio: verify ran, overall is Mismatch (ABI mismatch finding)");

    // Studio mode never disables nodes.
    const auto* cn = runtime.compiled_graph()
                        ? runtime.compiled_graph()->find_node("a") : nullptr;
    check(cn != nullptr, "load studio: compiled node 'a' exists");
    if (cn) {
        check(cn->missing_operator_reason != "locked_unavailable",
              "load studio: compiled node NOT locked_unavailable");
    }
}

void test_load_graph_strict_mode_disables_affected_node() {
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_load_strict");
    Graph source_graph;
    source_graph.add_node("a", "audio_out");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());
    write_descriptor_mismatch_lockfile(dir.path, "audio_out");

    Graph api_graph;
    RuntimeCore runtime;
    runtime.set_package_manager(&fx.pm);
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    bool has_gpu = false;
    bool has_aud = false;
    auto r = api.load_graph(graph_path, has_gpu, has_aud, "strict");
    check(r.ok, "load strict: succeeds");

    const auto* cn = runtime.compiled_graph()
                        ? runtime.compiled_graph()->find_node("a") : nullptr;
    check(cn != nullptr, "load strict: compiled node 'a' exists");
    if (cn) {
        check(cn->missing_operator,
              "load strict: missing_operator = true on affected node");
        check(cn->missing_operator_reason == "locked_unavailable",
              "load strict: reason = locked_unavailable");
    }
}

void test_load_graph_strict_mode_ignores_non_critical() {
    // Info/Warning findings must not disable nodes in Strict mode.
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_load_noncritical");
    Graph source_graph;
    source_graph.add_node("a", "audio_out");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());

    // Write a lockfile whose only issue is a fake content_hash (Info).
    ProjectLockfile lf;
    lf.lockfile_version        = LOCKFILE_VERSION;
    lf.vivid_core.version      = VIVID_CORE_VERSION;
    lf.vivid_core.operator_abi = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);
    lf.graph.content_hash =
        "sha256:deadbeef0000000000000000000000000000000000000000000000000000dead";
    save_lockfile(dir.path / "vivid.lock", lf);

    Graph api_graph;
    RuntimeCore runtime;
    runtime.set_package_manager(&fx.pm);
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    bool has_gpu = false;
    bool has_aud = false;
    auto r = api.load_graph(graph_path, has_gpu, has_aud, "strict");
    check(r.ok, "load strict non-critical: succeeds");
    check(runtime.lockfile_status().overall == LockfileOverall::CompatibleDrift,
          "load strict non-critical: overall is CompatibleDrift");

    const auto* cn = runtime.compiled_graph()
                        ? runtime.compiled_graph()->find_node("a") : nullptr;
    if (cn) {
        check(cn->missing_operator_reason != "locked_unavailable",
              "load strict non-critical: NOT locked_unavailable (Info finding only)");
    }
}

void test_load_graph_no_sibling_lockfile() {
    // Studio mode with no sibling lockfile stays quiet: opening a freshly
    // authored graph before running `vivid lock` must not show a banner.
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_load_nosib");
    Graph source_graph;
    source_graph.add_node("a", "audio_out");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());
    // No vivid.lock written.

    Graph api_graph;
    RuntimeCore runtime;
    runtime.set_package_manager(&fx.pm);
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    bool has_gpu = false;
    bool has_aud = false;
    auto r = api.load_graph(graph_path, has_gpu, has_aud, "studio");
    check(r.ok, "load no-sibling studio: succeeds");
    check(runtime.lockfile_status().overall == LockfileOverall::Match,
          "load no-sibling studio: overall is Match (default)");
    check(runtime.lockfile_status().findings.empty(),
          "load no-sibling studio: no findings");
}

void test_load_graph_strict_mode_missing_sibling_warns() {
    // Strict mode with no sibling vivid.lock must surface the
    // reproducibility gap as a Warning finding (so the UI banner shows),
    // but must NOT disable any node: opening a freshly authored graph
    // in strict mode before `vivid lock` runs is a legitimate workflow.
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_load_strict_nosib");
    Graph source_graph;
    source_graph.add_node("a", "audio_out");
    source_graph.add_node("b", "audio_out");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());
    // No vivid.lock written.

    Graph api_graph;
    RuntimeCore runtime;
    runtime.set_package_manager(&fx.pm);
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    bool has_gpu = false;
    bool has_aud = false;
    auto r = api.load_graph(graph_path, has_gpu, has_aud, "strict");
    check(r.ok, "load strict no-sibling: succeeds");

    const auto& status = runtime.lockfile_status();
    check(status.overall == LockfileOverall::NoLockfile,
          "load strict no-sibling: overall is NoLockfile");
    check(status.findings.size() == 1,
          "load strict no-sibling: exactly one finding emitted");

    bool saw_missing_warning = false;
    for (const auto& f : status.findings) {
        if (f.id == lockfile_finding::kLockfileMissing &&
            f.severity == LockfileSeverity::Warning) {
            saw_missing_warning = true;
            break;
        }
    }
    check(saw_missing_warning,
          "load strict no-sibling: Warning lockfile_missing finding emitted");

    const auto* cg = runtime.compiled_graph();
    check(cg != nullptr, "load strict no-sibling: compiled graph exists");
    if (cg) {
        for (const auto& cn : cg->nodes) {
            check(cn.missing_operator_reason != "locked_unavailable",
                  "load strict no-sibling: node NOT locked_unavailable");
        }
    }
}

void test_load_graph_strict_mode_malformed_sibling_locks_graph() {
    // A sibling vivid.lock that exists but fails to parse must not
    // silently bypass strict-mode enforcement. Instead the load path
    // synthesizes a Critical "lockfile_unreadable" finding and strict
    // mode disables every node because the environment is unverifiable.
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_load_bad_lf");
    Graph source_graph;
    source_graph.add_node("a", "audio_out");
    source_graph.add_node("b", "audio_out");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());

    // Write a malformed lockfile next to the graph.
    std::ofstream(dir.path / "vivid.lock") << "{ this is not valid json ";

    Graph api_graph;
    RuntimeCore runtime;
    runtime.set_package_manager(&fx.pm);
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    bool has_gpu = false;
    bool has_aud = false;
    auto r = api.load_graph(graph_path, has_gpu, has_aud, "strict");
    check(r.ok, "load strict bad-lf: succeeds");

    const auto& status = runtime.lockfile_status();
    check(status.overall == LockfileOverall::Mismatch,
          "load strict bad-lf: overall is Mismatch");
    bool saw_unreadable = false;
    for (const auto& f : status.findings) {
        if (f.id == lockfile_finding::kLockfileUnreadable &&
            f.severity == LockfileSeverity::Critical) {
            saw_unreadable = true;
            break;
        }
    }
    check(saw_unreadable,
          "load strict bad-lf: Critical lockfile_unreadable finding emitted");

    // Every node must be disabled with reason = locked_unavailable.
    const auto* cg = runtime.compiled_graph();
    check(cg != nullptr, "load strict bad-lf: compiled graph exists");
    if (cg) {
        size_t locked = 0;
        for (const auto& cn : cg->nodes) {
            if (cn.missing_operator &&
                cn.missing_operator_reason == "locked_unavailable") {
                ++locked;
            }
        }
        check(locked == cg->nodes.size(),
              "load strict bad-lf: every node marked locked_unavailable");
    }
}

void test_load_graph_studio_mode_malformed_sibling_records_finding_only() {
    // Studio mode records the Critical finding but MUST NOT disable nodes
    // - the strict-mode lockdown is gated on the caller's mode selection.
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_load_bad_lf_studio");
    Graph source_graph;
    source_graph.add_node("a", "audio_out");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());
    std::ofstream(dir.path / "vivid.lock") << "garbage";

    Graph api_graph;
    RuntimeCore runtime;
    runtime.set_package_manager(&fx.pm);
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    bool has_gpu = false;
    bool has_aud = false;
    auto r = api.load_graph(graph_path, has_gpu, has_aud, "studio");
    check(r.ok, "load studio bad-lf: succeeds");

    const auto& status = runtime.lockfile_status();
    check(status.overall == LockfileOverall::Mismatch,
          "load studio bad-lf: overall is Mismatch");

    const auto* cg = runtime.compiled_graph();
    if (cg && !cg->nodes.empty()) {
        const auto& cn = cg->nodes[0];
        check(cn.missing_operator_reason != "locked_unavailable",
              "load studio bad-lf: node NOT disabled (studio mode)");
    }
}

void test_graph_snapshot_carries_lockfile_status() {
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_snap_lf");
    Graph source_graph;
    source_graph.add_node("a", "audio_out");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());
    write_descriptor_mismatch_lockfile(dir.path, "audio_out");

    Graph api_graph;
    RuntimeCore runtime;
    runtime.set_package_manager(&fx.pm);
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    bool has_gpu = false;
    bool has_aud = false;
    auto r = api.load_graph(graph_path, has_gpu, has_aud, "studio");
    check(r.ok, "snapshot lockfile: load succeeds");

    OperatorInfoCache op_cache;
    auto snap = build_graph_snapshot(api_graph, runtime, nullptr,
                                      fx.registry, op_cache);
    check(snap.lockfile_status.overall == runtime.lockfile_status().overall,
          "snapshot lockfile: overall matches RuntimeCore::lockfile_status");
    check(snap.lockfile_status.findings.size() ==
              runtime.lockfile_status().findings.size(),
          "snapshot lockfile: findings count matches");
    check(snap.lockfile_status.overall == LockfileOverall::Mismatch,
          "snapshot lockfile: propagates Mismatch overall");
}

void test_load_graph_skips_verify_without_package_manager() {
    // RuntimeCore::set_package_manager is optional; without it, verify is
    // skipped silently (pre-Phase-6a behavior).
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_load_nopm");
    Graph source_graph;
    source_graph.add_node("a", "audio_out");
    auto graph_path = dir.file_str("demo.json");
    source_graph.save(graph_path.c_str());
    write_descriptor_mismatch_lockfile(dir.path, "audio_out");

    Graph api_graph;
    RuntimeCore runtime;
    // NOTE: not calling runtime.set_package_manager
    AudioEngine audio_engine;
    RuntimeAPI api(api_graph, runtime, audio_engine, fx.registry);

    bool has_gpu = false;
    bool has_aud = false;
    auto r = api.load_graph(graph_path, has_gpu, has_aud, "strict");
    check(r.ok, "load no-pm: succeeds");
    check(runtime.lockfile_status().overall == LockfileOverall::Match,
          "load no-pm: overall is Match (verify skipped)");
    check(runtime.lockfile_status().findings.empty(),
          "load no-pm: no findings");
}

// --- Phase 8: asset content hashing --------------------------------------
//
// Tests use a hand-built test operator with one asset-bearing param (emulates
// wavetable_osc's "wavetable" input). Registered via register_builtin so
// probe_descriptor returns the descriptor. Data is file-local; never touches
// real operator dylibs.

namespace asset_fixture {

static const VividParamDescriptor kAssetOpParams[] = {
    { "wavetable", VIVID_PARAM_FILE, 0, 0, 0,
      nullptr, 0, "",
      "", VIVID_DISPLAY_DEFAULT, 0, 0,
      "", "", "", "", "",
      "wavetable",  // asset_kind — this is the field we're testing against
      "", VIVID_PARAM_VIS_ALWAYS, nullptr, 0,
      "", 0, "", 0 },
    { "gain", VIVID_PARAM_FLOAT, 0.5f, 0.0f, 1.0f,
      nullptr, 0, "",
      "", VIVID_DISPLAY_DEFAULT, 0, 0,
      "", "", "", "", "",
      nullptr,  // no asset_kind — this param must be ignored by enumeration
      "", VIVID_PARAM_VIS_ALWAYS, nullptr, 0,
      "", 0, "", 0 },
    { "extra_file", VIVID_PARAM_FILE, 0, 0, 0,
      nullptr, 0, "",
      "", VIVID_DISPLAY_DEFAULT, 0, 0,
      "", "", "", "", "",
      nullptr,  // file param without asset_kind — also ignored
      "", VIVID_PARAM_VIS_ALWAYS, nullptr, 0,
      "", 0, "", 0 },
};

static const VividPortDescriptor kAssetOpPorts[] = {
    { "out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT },
};

static const VividOperatorDescriptor kAssetOpDesc = {
    "AssetTestOp",
    3,
    kAssetOpParams,
    1,
    kAssetOpPorts,
    0, 0, 0, 1,
    0,
};

static const VividOperatorDescriptor* asset_op_descriptor() { return &kAssetOpDesc; }
static void* asset_op_create()  { return new char; }
static void  asset_op_destroy(void* p) { delete static_cast<char*>(p); }
static void  asset_op_process(void*, VividFrameContext*) {}

void register_asset_test_op(OperatorRegistry& reg) {
    reg.register_builtin("AssetTestOp", asset_op_descriptor, asset_op_create,
                          asset_op_destroy, asset_op_process);
}

}  // namespace asset_fixture

// Write `contents` to `path` in text mode.
static void write_text(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream ofs(path);
    ofs << contents;
}

void test_sha256_file_matches_sha256_hex() {
    ScopedTempDir dir("vivid_sha256_file");
    auto path = dir.path / "bytes.bin";
    const std::string contents = "hello lockfile assets!";
    write_text(path, contents);

    check(sha256_file(path) == sha256_hex(contents),
          "sha256_file: matches sha256_hex on same bytes");
    check(sha256_file(path).size() == 64,
          "sha256_file: returns 64 hex chars");
}

void test_sha256_file_missing_returns_empty() {
    check(sha256_file("/does/not/exist.bin").empty(),
          "sha256_file: returns empty on missing file");
}

void test_build_lockfile_assets_empty_without_asset_library() {
    LockfileGenFixture fx;
    asset_fixture::register_asset_test_op(fx.registry);

    ScopedTempDir dir("vivid_assets_no_al");
    auto asset_path = (dir.path / "sample.wav").string();
    write_text(asset_path, "pretend-wav");

    Graph g;
    g.add_node("n1", "AssetTestOp");

    auto lf = build_lockfile_for_graph(g, fx.pm, fx.registry);
    check(lf.assets.empty(),
          "no AssetLibrary: assets[] stays empty");
}

void test_build_lockfile_assets_populated_with_content_hash() {
    LockfileGenFixture fx;
    asset_fixture::register_asset_test_op(fx.registry);

    AssetLibrary al;
    fx.pm.set_asset_library(&al);

    ScopedTempDir dir("vivid_assets_lock");
    auto asset_path = (dir.path / "wt.wav").string();
    const std::string contents = "wavetable-bytes";
    write_text(asset_path, contents);

    Graph g;
    g.add_node("n1", "AssetTestOp",
               {},
               {{"wavetable", asset_path}, {"extra_file", "/some/other.cfg"}});

    auto lf = build_lockfile_for_graph(g, fx.pm, fx.registry);
    check(lf.assets.size() == 1,
          "asset enum: exactly one asset entry for the asset_kind param");
    if (!lf.assets.empty()) {
        check(lf.assets[0].kind == "wavetable",
              "asset enum: kind carried through from descriptor");
        check(lf.assets[0].path.find("wt.wav") != std::string::npos,
              "asset enum: path includes the filename");
        check(lf.assets[0].content_hash == "sha256:" + sha256_hex(contents),
              "asset enum: content_hash matches file bytes");
    }
}

void test_build_lockfile_assets_missing_file_produces_empty_hash() {
    LockfileGenFixture fx;
    asset_fixture::register_asset_test_op(fx.registry);

    AssetLibrary al;
    fx.pm.set_asset_library(&al);

    Graph g;
    g.add_node("n1", "AssetTestOp", {}, {{"wavetable", "/does/not/exist.wav"}});

    auto lf = build_lockfile_for_graph(g, fx.pm, fx.registry);
    check(lf.assets.size() == 1,
          "asset enum missing file: still produces an entry");
    if (!lf.assets.empty()) {
        check(lf.assets[0].content_hash.empty(),
              "asset enum missing file: content_hash stays empty");
    }
}

void test_build_lockfile_assets_dedup_across_nodes() {
    LockfileGenFixture fx;
    asset_fixture::register_asset_test_op(fx.registry);

    AssetLibrary al;
    fx.pm.set_asset_library(&al);

    ScopedTempDir dir("vivid_assets_dedup");
    auto asset_path = (dir.path / "shared.wav").string();
    write_text(asset_path, "shared");

    Graph g;
    g.add_node("n1", "AssetTestOp", {}, {{"wavetable", asset_path}});
    g.add_node("n2", "AssetTestOp", {}, {{"wavetable", asset_path}});
    g.add_node("n3", "AssetTestOp", {}, {{"wavetable", asset_path}});

    auto lf = build_lockfile_for_graph(g, fx.pm, fx.registry);
    check(lf.assets.size() == 1,
          "asset enum dedup: one shared asset referenced by three nodes → one entry");
}

// --- Phase 8: verify_lockfile asset findings -----------------------------

void test_verify_asset_missing_emits_critical() {
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);
    AssetLibrary al;
    fx.pm.set_asset_library(&al);

    Graph g;
    ProjectLockfile lf;
    LockfileAsset a;
    a.kind         = "wavetable";
    a.path         = "/does/not/exist/sample.wav";
    a.content_hash = "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    lf.assets.push_back(a);

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    const auto* f = find_finding(status, lockfile_finding::kAssetMissing);
    check(f != nullptr, "verify asset missing: finding emitted");
    if (f) check(f->severity == LockfileSeverity::Critical,
                 "verify asset missing: severity Critical");
    check(status.overall == LockfileOverall::Mismatch,
          "verify asset missing: overall = Mismatch");
}

void test_verify_asset_changed_emits_warning() {
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);
    AssetLibrary al;
    fx.pm.set_asset_library(&al);

    ScopedTempDir dir("vivid_verify_asset_changed");
    auto asset_path = (dir.path / "w.wav").string();
    write_text(asset_path, "CURRENT CONTENTS");

    Graph g;
    ProjectLockfile lf;
    LockfileAsset a;
    a.kind         = "wavetable";
    a.path         = asset_path;
    // deliberately wrong hash (sha of a different string)
    a.content_hash = "sha256:" + sha256_hex("OLD CONTENTS");
    lf.assets.push_back(a);

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    const auto* f = find_finding(status, lockfile_finding::kAssetChanged);
    check(f != nullptr, "verify asset changed: finding emitted");
    if (f) check(f->severity == LockfileSeverity::Warning,
                 "verify asset changed: severity Warning");
    // Warning → CompatibleDrift; strict-export (Phase 7) would still pass.
    check(status.overall == LockfileOverall::CompatibleDrift,
          "verify asset changed: overall = CompatibleDrift");
}

void test_verify_asset_untouched_no_finding() {
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);
    AssetLibrary al;
    fx.pm.set_asset_library(&al);

    ScopedTempDir dir("vivid_verify_asset_clean");
    auto asset_path = (dir.path / "clean.wav").string();
    const std::string contents = "untouched";
    write_text(asset_path, contents);

    Graph g;
    ProjectLockfile lf;
    LockfileAsset a;
    a.kind         = "wavetable";
    a.path         = asset_path;
    a.content_hash = "sha256:" + sha256_hex(contents);
    lf.assets.push_back(a);

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    check(find_finding(status, lockfile_finding::kAssetMissing) == nullptr,
          "verify asset untouched: no missing finding");
    check(find_finding(status, lockfile_finding::kAssetChanged) == nullptr,
          "verify asset untouched: no changed finding");
}

void test_verify_asset_empty_content_hash_skips_check() {
    // Pre-Phase-8 lockfile has content_hash == "". We must NOT emit
    // asset_changed just because current content differs from empty.
    LockfileGenFixture fx;
    register_builtin_operators(fx.registry);
    AssetLibrary al;
    fx.pm.set_asset_library(&al);

    ScopedTempDir dir("vivid_verify_asset_prephase8");
    auto asset_path = (dir.path / "w.wav").string();
    write_text(asset_path, "anything");

    Graph g;
    ProjectLockfile lf;
    LockfileAsset a;
    a.kind         = "wavetable";
    a.path         = asset_path;
    a.content_hash = "";  // pre-Phase-8
    lf.assets.push_back(a);

    auto status = verify_lockfile(lf, g, fx.pm, fx.registry);
    check(find_finding(status, lockfile_finding::kAssetChanged) == nullptr,
          "verify empty-hash skip: no asset_changed finding");
}

void test_build_lockfile_assets_sort_stability() {
    LockfileGenFixture fx;
    asset_fixture::register_asset_test_op(fx.registry);

    AssetLibrary al;
    fx.pm.set_asset_library(&al);

    ScopedTempDir dir("vivid_assets_sort");
    auto p_a = (dir.path / "a.wav").string();
    auto p_b = (dir.path / "b.wav").string();
    auto p_c = (dir.path / "c.wav").string();
    write_text(p_a, "a");
    write_text(p_b, "b");
    write_text(p_c, "c");

    Graph g1;
    g1.add_node("n1", "AssetTestOp", {}, {{"wavetable", p_c}});
    g1.add_node("n2", "AssetTestOp", {}, {{"wavetable", p_a}});
    g1.add_node("n3", "AssetTestOp", {}, {{"wavetable", p_b}});

    Graph g2;
    g2.add_node("n1", "AssetTestOp", {}, {{"wavetable", p_a}});
    g2.add_node("n2", "AssetTestOp", {}, {{"wavetable", p_b}});
    g2.add_node("n3", "AssetTestOp", {}, {{"wavetable", p_c}});

    auto lf1 = build_lockfile_for_graph(g1, fx.pm, fx.registry);
    auto lf2 = build_lockfile_for_graph(g2, fx.pm, fx.registry);
    check(lf1.assets.size() == 3 && lf2.assets.size() == 3,
          "asset sort: both graphs produce 3 entries");
    bool same = lf1.assets.size() == lf2.assets.size();
    for (size_t i = 0; same && i < lf1.assets.size(); ++i) {
        if (lf1.assets[i].path != lf2.assets[i].path) same = false;
    }
    check(same, "asset sort: order independent of node insertion order");
}

}  // namespace

int main() {
    test_round_trip_full();
    test_round_trip_minimal();
    test_canonical_key_order();
    test_forward_version_rejected();
    test_malformed_json_parse_error();
    test_missing_lockfile_version();
    test_missing_optional_fields_load_defaults();
    test_unknown_top_level_keys_ignored();
    test_io_error_on_missing_file();
    test_zero_version_rejected();
    test_sha256_hex_abc();
    test_sha256_hex_length();
    test_sha256_hex_multi_block();
    test_build_lockfile_empty_graph();
    test_build_lockfile_vivid_core_commit();
    test_build_lockfile_graph_hash_populated();
    test_build_lockfile_operators_unregistered_types();
    test_build_lockfile_sort_stability_by_insertion_order();
    test_canonicalize_graph_hash_stable();
    test_canonicalize_graph_hash_sensitive_to_nodes();
    test_canonicalize_graph_hash_prefix_and_length();
    test_verify_empty_lockfile_empty_graph();
    test_verify_missing_package();
    test_verify_missing_operator();
    test_verify_vivid_core_version_mismatch();
    test_verify_core_abi_mismatch();
    test_verify_graph_content_drift();
    test_verify_overall_precedence_critical_wins();
    test_verify_generate_then_verify_round_trip();
    test_verify_status_to_json_shape();
    test_runtime_api_write_project_lockfile_round_trip();
    test_runtime_api_write_project_lockfile_explicit_output();
    test_runtime_api_write_project_lockfile_missing_graph();
    test_runtime_api_write_project_lockfile_empty_graph_path();
    test_runtime_api_verify_project_lockfile_round_trip();
    test_runtime_api_verify_project_lockfile_missing_lockfile();
    test_runtime_api_get_project_dependency_status_no_lockfile();
    test_runtime_api_get_project_dependency_status_happy_path();
    test_parse_lockfile_load_mode_known_values();
    test_parse_lockfile_load_mode_unknown_defaults_to_studio();
    test_lockfile_load_mode_to_string_round_trips();
    test_runtime_core_lockfile_status_default();
    test_runtime_core_set_lockfile_status();
    test_load_graph_studio_mode_runs_verify_no_disabling();
    test_load_graph_strict_mode_disables_affected_node();
    test_load_graph_strict_mode_ignores_non_critical();
    test_load_graph_no_sibling_lockfile();
    test_load_graph_strict_mode_missing_sibling_warns();
    test_load_graph_strict_mode_malformed_sibling_locks_graph();
    test_load_graph_studio_mode_malformed_sibling_records_finding_only();
    test_load_graph_skips_verify_without_package_manager();
    test_graph_snapshot_carries_lockfile_status();
    test_sha256_file_matches_sha256_hex();
    test_sha256_file_missing_returns_empty();
    test_build_lockfile_assets_empty_without_asset_library();
    test_build_lockfile_assets_populated_with_content_hash();
    test_build_lockfile_assets_missing_file_produces_empty_hash();
    test_build_lockfile_assets_dedup_across_nodes();
    test_build_lockfile_assets_sort_stability();
    test_verify_asset_missing_emits_critical();
    test_verify_asset_changed_emits_warning();
    test_verify_asset_untouched_no_finding();
    test_verify_asset_empty_content_hash_skips_check();
    test_unwrap_status_to_json_inlines_valid_status();
    test_unwrap_status_to_json_preserves_error_message();
    test_unwrap_status_to_json_non_json_message_fallback();
    test_capture_git_metadata_reads_commit_and_url();
    test_capture_git_metadata_detects_dirty();
    test_capture_git_metadata_non_git_dir_is_silent();
    test_build_lockfile_populates_descriptor_hash();
    test_verify_emits_descriptor_hash_mismatch();
    test_verify_skips_descriptor_hash_mismatch_when_empty();

    if (failures == 0) {
        std::fprintf(stderr, "All project_lockfile tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d project_lockfile test failure(s).\n", failures);
    return 1;
}
