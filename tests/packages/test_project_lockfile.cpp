// test_project_lockfile.cpp — ProjectLockfile JSON round-trip and validation
#include "runtime/packages/project_lockfile.h"

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

    if (failures == 0) {
        std::fprintf(stderr, "All project_lockfile tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d project_lockfile test failure(s).\n", failures);
    return 1;
}
