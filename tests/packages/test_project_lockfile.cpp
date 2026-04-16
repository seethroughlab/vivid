// test_project_lockfile.cpp — ProjectLockfile JSON round-trip and validation
#include "runtime/packages/project_lockfile.h"

#include "common/hash_util.h"
#include "operator_api/types.h"
#include "runtime/core/build_console.h"
#include "runtime/core/workspace_manager.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/packages/package_manager.h"

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
    lf.vivid_core.operator_abi  = 15;

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
    op.operator_abi     = 15;
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
    check(lf.vivid_core.commit.empty(),
          "empty graph: vivid_core.commit is empty (Phase 0)");
    check(lf.packages.empty(),  "empty graph: packages empty");
    check(lf.operators.empty(), "empty graph: operators empty");
    check(lf.assets.empty(),    "empty graph: assets empty");
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
          "build_lockfile: descriptor_hash empty (Phase 0)");
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
    test_build_lockfile_graph_hash_populated();
    test_build_lockfile_operators_unregistered_types();
    test_build_lockfile_sort_stability_by_insertion_order();
    test_canonicalize_graph_hash_stable();
    test_canonicalize_graph_hash_sensitive_to_nodes();
    test_canonicalize_graph_hash_prefix_and_length();

    if (failures == 0) {
        std::fprintf(stderr, "All project_lockfile tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d project_lockfile test failure(s).\n", failures);
    return 1;
}
