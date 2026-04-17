// test_export_strict.cpp — Phase 7 strict-mode gate in ExportPipeline::run.
//
// Focuses on the early-exit strict gate: we don't drive the full pipeline
// (that's tests/media/test_export_pipeline.cpp). When strict_verify_failed()
// is populated the pipeline returned false before any expensive work. Other
// tests drive the pipeline past the gate and assert the gate itself was clean,
// regardless of whether later steps succeed (they generally won't in this
// lightweight setup).
#include "export/export_pipeline.h"
#include "operator_api/types.h"
#include "runtime/core/workspace_manager.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/builtin_operators.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/packages/package_manager.h"
#include "runtime/packages/project_lockfile.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "test_helpers.h"

using namespace vivid;

namespace {

struct ExportFixture {
    ScopedTempDir workspace;
    OperatorRegistry registry;
    PackageCompiler compiler{workspace.str(), workspace.str()};
    PackageManager pm{compiler, registry};
    ExportPipeline pipeline{workspace.str(), workspace.str()};
};

std::string save_minimal_graph(const std::filesystem::path& dir) {
    Graph g;
    g.add_node("a", "audio_out");
    auto path = (dir / "demo.json").string();
    g.save(path.c_str());
    return path;
}

// Hand-author a lockfile that triggers a descriptor_hash_mismatch on
// audio_out — Phase 6a tests used the same approach.
std::string save_stale_lockfile(const std::filesystem::path& dir,
                                const std::string& op_type,
                                const std::filesystem::path& path = {}) {
    ProjectLockfile lf;
    lf.lockfile_version       = LOCKFILE_VERSION;
    lf.vivid_core.version     = VIVID_CORE_VERSION;
    lf.vivid_core.operator_abi = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);
    LockfileOperator o;
    o.type            = op_type;
    o.operator_abi    = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);
    o.descriptor_hash = "sha256:deadbeef0000000000000000000000000000000000000000000000000000dead";
    lf.operators.push_back(o);

    auto out = path.empty() ? (dir / "vivid.lock") : path;
    save_lockfile(out, lf);
    return out.string();
}

// Lockfile that matches the current env (empty, defaults). verify against an
// empty graph returns Match / zero findings.
std::string save_matching_lockfile(const std::filesystem::path& dir,
                                   const std::filesystem::path& path = {}) {
    ProjectLockfile lf;
    lf.lockfile_version        = LOCKFILE_VERSION;
    lf.vivid_core.version      = VIVID_CORE_VERSION;
    lf.vivid_core.operator_abi = static_cast<int>(VIVID_OPERATOR_ABI_VERSION);
    auto out = path.empty() ? (dir / "vivid.lock") : path;
    save_lockfile(out, lf);
    return out.string();
}

ExportOptions base_opts(const std::string& graph_path, bool strict,
                        const std::string& lockfile_path = {}) {
    ExportOptions opts;
    opts.graph_path      = graph_path;
    opts.output_name     = "strict_test";
    opts.output_path     = "strict_test";
    opts.output_dir      = "strict_test_export";
    opts.headless        = true;
    opts.control_server  = false;
    opts.strict          = strict;
    opts.lockfile_path   = lockfile_path;
    return opts;
}

void test_strict_without_pm_fails_fast() {
    ExportFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_strict_nopm");
    auto gp = save_minimal_graph(dir.path);
    save_matching_lockfile(dir.path);

    auto opts = base_opts(gp, /*strict=*/true);
    const bool ok = fx.pipeline.run(opts, fx.registry, /*pm=*/nullptr);
    check(!ok, "strict no-pm: run returns false");
    check(fx.pipeline.strict_verify_failed(),
          "strict no-pm: strict_verify_failed is true");
    check(fx.pipeline.last_strict_verify_error_kind() == "no_pm",
          "strict no-pm: error kind = no_pm");
}

void test_strict_without_sibling_lockfile_fails() {
    ExportFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_strict_nosib");
    auto gp = save_minimal_graph(dir.path);
    // No sibling vivid.lock written.

    auto opts = base_opts(gp, /*strict=*/true);
    const bool ok = fx.pipeline.run(opts, fx.registry, &fx.pm);
    check(!ok, "strict no-sibling: run returns false");
    check(fx.pipeline.strict_verify_failed(),
          "strict no-sibling: strict_verify_failed is true");
    check(fx.pipeline.last_strict_verify_error_kind() == "no_lockfile",
          "strict no-sibling: error kind = no_lockfile");
}

void test_strict_on_mismatch_fails_with_status() {
    ExportFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_strict_mismatch");
    auto gp = save_minimal_graph(dir.path);
    save_stale_lockfile(dir.path, "audio_out");

    auto opts = base_opts(gp, /*strict=*/true);
    const bool ok = fx.pipeline.run(opts, fx.registry, &fx.pm);
    check(!ok, "strict mismatch: run returns false");
    check(fx.pipeline.strict_verify_failed(),
          "strict mismatch: strict_verify_failed is true");
    check(fx.pipeline.last_strict_verify_error_kind() == "mismatch",
          "strict mismatch: error kind = mismatch");
    check(fx.pipeline.last_strict_verify_status().overall ==
              LockfileOverall::Mismatch,
          "strict mismatch: status overall = Mismatch");
    bool found_descriptor_mismatch = false;
    for (const auto& f : fx.pipeline.last_strict_verify_status().findings) {
        if (f.id == lockfile_finding::kDescriptorHashMismatch) {
            found_descriptor_mismatch = true;
        }
    }
    check(found_descriptor_mismatch,
          "strict mismatch: descriptor_hash_mismatch finding present");
}

void test_strict_honors_explicit_lockfile_path() {
    ExportFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_strict_explicit");
    auto gp = save_minimal_graph(dir.path);
    // Sibling is CLEAN; the explicit --lockfile is STALE. Strict should fail
    // because the explicit path overrides the sibling.
    save_matching_lockfile(dir.path);
    auto explicit_lf = (dir.path / "custom.lock").string();
    save_stale_lockfile(dir.path, "audio_out", explicit_lf);

    auto opts = base_opts(gp, /*strict=*/true, explicit_lf);
    const bool ok = fx.pipeline.run(opts, fx.registry, &fx.pm);
    check(!ok, "strict explicit: run returns false");
    check(fx.pipeline.last_strict_verify_error_kind() == "mismatch",
          "strict explicit: honors --lockfile override (mismatch from override)");
}

void test_strict_gate_clean_on_matching_lockfile() {
    ExportFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_strict_match");
    // Empty graph -> the matching lockfile (no operators/packages) verifies
    // cleanly against an empty environment.
    Graph g;
    auto gp = (dir.path / "empty.json").string();
    g.save(gp.c_str());
    save_matching_lockfile(dir.path);

    auto opts = base_opts(gp, /*strict=*/true);
    // run() likely fails later stages due to the minimal test fixture, but
    // the strict gate itself should pass cleanly.
    (void)fx.pipeline.run(opts, fx.registry, &fx.pm);
    check(!fx.pipeline.strict_verify_failed(),
          "strict match: gate does not flag failure on matching lockfile");
    check(fx.pipeline.last_strict_verify_error_kind().empty(),
          "strict match: error kind empty");
}

void test_non_strict_ignores_lockfile() {
    // Without opts.strict, even a bogus lockfile is ignored.
    ExportFixture fx;
    register_builtin_operators(fx.registry);

    ScopedTempDir dir("vivid_nonstrict");
    auto gp = save_minimal_graph(dir.path);
    save_stale_lockfile(dir.path, "audio_out");

    auto opts = base_opts(gp, /*strict=*/false);
    (void)fx.pipeline.run(opts, fx.registry, &fx.pm);
    check(!fx.pipeline.strict_verify_failed(),
          "non-strict: strict_verify_failed stays false even with stale lockfile");
    check(fx.pipeline.last_strict_verify_error_kind().empty(),
          "non-strict: error kind stays empty");
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    test_strict_without_pm_fails_fast();
    test_strict_without_sibling_lockfile_fails();
    test_strict_on_mismatch_fails_with_status();
    test_strict_honors_explicit_lockfile_path();
    test_strict_gate_clean_on_matching_lockfile();
    test_non_strict_ignores_lockfile();

    if (failures == 0) {
        std::fprintf(stderr, "All export-strict tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d export-strict test failure(s).\n", failures);
    return 1;
}
