// Frame lane-count normalization + positional alignment (lane-value Phase 8c).
//
// The clean break retired Pass 2.6's hard rejection of two different-provenance
// Many inputs at a MAP/combiner node — the value model is permissive: such inputs
// vectorize with POSITIONAL alignment, made well-defined by frame_executor's
// lane-count normalization (frame_executor.cpp:109-139): each shorter input ref
// is expanded to the max element count, repeating its LAST element. This asserts
// that behavior at runtime via a 2-input combiner (AddManyOp) — the output VALUES
// prove the last-element-repeat expansion happened.

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string staging = build_dir + "/.test_value_normalization_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("lane_source_op.dylib");
    stage("lane_sink_op.dylib");
    stage("add_many_op.dylib");

    std::fprintf(stderr, "\n=== test_value_normalization ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    // Build src_a(count_a) + src_b(count_b) → AddManyOp → sink; tick once; return
    // the combined output array (sink's materialized value scratch).
    auto run_case = [&](float base_a, int count_a, float base_b, int count_b,
                        std::vector<float>& out) {
        vivid::Graph g;
        g.add_node("a", "LaneSourceOp", {{"base", base_a}, {"count", (float)count_a}});
        g.add_node("b", "LaneSourceOp", {{"base", base_b}, {"count", (float)count_b}});
        g.add_node("add", "AddManyOp");
        g.add_node("sink", "LaneSinkOp");
        g.add_connection("a", "out", "add", "in_a");
        g.add_connection("b", "out", "add", "in_b");
        g.add_connection("add", "out", "sink", "in");
        vivid::RuntimeCore rt;
        if (!rt.build(g, registry)) return false;
        check_graph_clean(rt.compiled_graph(), "normalization graph");
        rt.tick(0.0, 1.0 / 60.0, 0);
        auto* sink = rt.compiled_graph()->find_node("sink");
        out.clear();
        if (sink && !sink->output_lanes.empty()) out = sink->output_lanes[0];
        return true;
    };

    // --- positional: [1,2,3,4] + [10,20] → [10,20,20,20] (last-repeat) → [11,22,23,24] ---
    std::fprintf(stderr, "\n--- positional (count 4 + count 2) ---\n");
    {
        std::vector<float> o;
        check(run_case(1.0f, 4, 10.0f, 2, o), "positional: builds + ticks");
        check(o.size() == 4, "positional: 4 elements out");
        if (o.size() == 4) {
            check_float(o[0], 11.0f, 0.01f, "positional o0 = 1+10 = 11");
            check_float(o[1], 22.0f, 0.01f, "positional o1 = 2+20 = 22");
            check_float(o[2], 23.0f, 0.01f, "positional o2 = 3+20 (last-repeat) = 23");
            check_float(o[3], 24.0f, 0.01f, "positional o3 = 4+20 (last-repeat) = 24");
        }
    }

    // --- broadcast: [1,2,3,4] + [10] → [10,10,10,10] → [11,12,13,14] ---
    std::fprintf(stderr, "\n--- broadcast (count 4 + count 1) ---\n");
    {
        std::vector<float> o;
        check(run_case(1.0f, 4, 10.0f, 1, o), "broadcast: builds + ticks");
        check(o.size() == 4, "broadcast: 4 elements out");
        if (o.size() == 4) {
            check_float(o[0], 11.0f, 0.01f, "broadcast o0 = 11");
            check_float(o[3], 14.0f, 0.01f, "broadcast o3 = 4+10 = 14");
        }
    }

    // --- all-≤1 (no normalization: max_lanes <= 1) → single element ---
    std::fprintf(stderr, "\n--- all-scalar (count 1 + count 1, no normalization) ---\n");
    {
        std::vector<float> o;
        check(run_case(5.0f, 1, 10.0f, 1, o), "all-scalar: builds + ticks");
        check(o.size() == 1, "all-scalar: single element out");
        if (o.size() == 1)
            check_float(o[0], 15.0f, 0.01f, "all-scalar o0 = 5+10 = 15");
    }

    std::filesystem::remove_all(staging);
    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
