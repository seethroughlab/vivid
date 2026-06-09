// Test: hot-reload descriptor classification (audit 01-F2 / 01-F7).
//
// Verifies classify_hot_reload() — the gate that decides whether a dylib swap
// during hot reload is safe in place, needs a full recompile, or must be
// rejected. Regression guard for the fix that added strategy_independent (and
// reclassified multiplicity_behavior) as RecompileRequired rather than a silent no-op /
// stale-metadata hazard.

#include "runtime/operators/operator_loader.h"
#include "operator_api/types.h"
#include <cstdio>
#include "test_helpers.h"

using namespace vivid;

namespace {

// A minimal, layout-empty descriptor: no params, no ports. Tests vary single
// fields off this baseline.
VividOperatorDescriptor base_desc() {
    VividOperatorDescriptor d{};
    d.name = "TestOp";
    d.param_count = 0;
    d.params = nullptr;
    d.port_count = 0;
    d.ports = nullptr;
    d.has_process_gpu = 0;
    d.multiplicity_behavior = VIVID_MULTIPLICITY_MAP;
    d.strategy_independent = 0;
    return d;
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== test_hot_reload_classify ===\n\n");

    // Identical → Compatible (in-place reload fine).
    {
        VividOperatorDescriptor a = base_desc(), b = base_desc();
        check(classify_hot_reload(&a, &b) == HotReloadCompat::Compatible,
              "identical descriptors → Compatible");
    }

    // strategy_independent flipped → RecompileRequired (the 01-F7 hole).
    {
        VividOperatorDescriptor a = base_desc(), b = base_desc();
        b.strategy_independent = 1;
        check(classify_hot_reload(&a, &b) == HotReloadCompat::RecompileRequired,
              "strategy_independent change → RecompileRequired");
    }

    // multiplicity_behavior changed → RecompileRequired (now applies live via recompile).
    {
        VividOperatorDescriptor a = base_desc(), b = base_desc();
        b.multiplicity_behavior = VIVID_MULTIPLICITY_GENERATE;
        check(classify_hot_reload(&a, &b) == HotReloadCompat::RecompileRequired,
              "multiplicity_behavior change → RecompileRequired");
    }

    // has_process_gpu changed → Incompatible (hard reject).
    {
        VividOperatorDescriptor a = base_desc(), b = base_desc();
        b.has_process_gpu = 1;
        check(classify_hot_reload(&a, &b) == HotReloadCompat::Incompatible,
              "has_process_gpu change → Incompatible");
    }

    // Param layout changed (name mismatch) → Incompatible.
    {
        VividParamDescriptor pa{}; pa.name = "a"; pa.type = VIVID_PARAM_FLOAT;
        VividParamDescriptor pb{}; pb.name = "b"; pb.type = VIVID_PARAM_FLOAT;
        VividOperatorDescriptor a = base_desc(), b = base_desc();
        a.param_count = 1; a.params = &pa;
        b.param_count = 1; b.params = &pb;
        check(classify_hot_reload(&a, &b) == HotReloadCompat::Incompatible,
              "param layout change → Incompatible");
    }

    // Port count changed → Incompatible.
    {
        VividOperatorDescriptor a = base_desc(), b = base_desc();
        a.port_count = 1; // count mismatch is rejected before any ports deref
        check(classify_hot_reload(&a, &b) == HotReloadCompat::Incompatible,
              "port count change → Incompatible");
    }

    // Null descriptors → Compatible (defensive: treated as no-op).
    {
        VividOperatorDescriptor a = base_desc();
        check(classify_hot_reload(nullptr, &a) == HotReloadCompat::Compatible,
              "null old descriptor → Compatible");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
