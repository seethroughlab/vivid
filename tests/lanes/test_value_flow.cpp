// Unit tests for Pass 2.7: value-flow inference (lane-value clean-break, Phase 2).
//
// The value-flow pass computes each edge/port's ValueEnvelope
// (type/multiplicity/identity/storage) from the operator's multiplicity_behavior,
// in parallel with the lane sets, and asserts equivalence to them. Here we verify
// the additive wiring + the scalar case + the machine-checkable equivalence count.
// Rich multi-lane behavior (Many from structural, channels != multiplicity) is
// asserted in test_lane_equivalence via cg.value_flow_mismatches == 0 over real
// structural/reduction/multi-channel graphs.

#include "runtime/graph/graph_compiler.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/lane_types.h"
#include <cstdio>
#include "test_helpers.h"

static std::unique_ptr<vivid::CompiledGraph> compile(vivid::Graph& g) {
    vivid::OperatorRegistry registry;
    vivid::GraphCompiler::Options opts;
    return vivid::GraphCompiler::compile(g, registry, opts);
}

int main() {
    std::fprintf(stderr, "--- test_value_flow ---\n");

    // Scalar chain: a -> b -> c. Every value is one Float; no Many anywhere.
    {
        std::fprintf(stderr, "\n=== scalar graph ===\n");
        vivid::Graph g;
        g.add_node("a", "UnknownA");
        g.add_node("b", "UnknownB");
        g.add_node("c", "UnknownC");
        g.add_connection("a", "out", "b", "in");
        g.add_connection("b", "out", "c", "in");

        auto cg = compile(g);
        check(cg != nullptr, "compiles");
        if (!cg) return 1;

        // The value-flow inference is fully equivalent to the lane sets.
        check(cg->value_flow_mismatches == 0, "value-flow equivalent to lane sets (0 mismatches)");

        // Every edge carries a populated, scalar Float envelope matching its lane set.
        bool all_scalar_float = true;
        for (const auto& e : cg->edges) {
            const auto& ve = e.value_envelope;
            if (ve.multiplicity != VIVID_MULTIPLICITY_SCALAR) all_scalar_float = false;
            if (ve.value_type   != VIVID_VALUE_FLOAT)         all_scalar_float = false;
            // Equivalence with the lane fields on this edge.
            const bool lane_scalar = (e.lane_set_id == 0 && e.lane_count <= 1);
            const bool env_scalar  = ve.is_scalar();
            if (lane_scalar != env_scalar) all_scalar_float = false;
        }
        check(all_scalar_float, "all edges: scalar Float envelope, equivalent to lane fields");

        // Envelopes are sized to the port counts (parallel to the lane sets).
        bool sized = true;
        for (const auto& cn : cg->nodes) {
            if (cn.output_value_envelopes.size() != cn.output_lane_sets.size()) sized = false;
            if (cn.input_value_envelopes.size()  != cn.input_lane_sets.size())  sized = false;
        }
        check(sized, "value envelope vectors sized to port counts");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
