// Test: lane reshape operators (Repeat, Tile, Select).
//
// Verifies:
// 1. Repeat broadcasts a scalar to N lanes
// 2. Tile repeats a short pattern to fill a target length
// 3. Select picks one lane and reduces to scalar
// 4. Mismatch resolution: Select (Reduction) enables mixing different lane sets
// 5. Compiler metadata: provenance IDs on Repeat/Tile/Select
// 6. Kernel consumers can accept independent structural lane inputs
// 7. Tile mismatch rejection: independent Tile outputs still fail at pointwise nodes

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/graph_compiler.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/lane_types.h"
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <string>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    // Stage operators
    std::string staging = build_dir + "/.test_lane_reshape_staging";
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
    stage("repeat.dylib");
    stage("tile.dylib");
    stage("select.dylib");
    stage("math.dylib");
    stage("metaball.dylib");

    std::fprintf(stderr, "\n=== test_lane_reshape ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    // --- Test 1: Repeat ---
    std::fprintf(stderr, "\n--- Repeat: scalar → 8 lanes ---\n");
    {
        vivid::Graph graph;
        // LaneSourceOp(base=5, count=3) → lane array [5,10,15]
        // Signal wire to Repeat → input_values[0] = lane_array[0] = 5.0
        // Repeat(count=8) → [5,5,5,5,5,5,5,5]
        graph.add_node("src", "LaneSourceOp", {{"base", 5.0f}, {"count", 3.0f}});
        graph.add_node("rep", "Repeat", {{"count", 8.0f}});
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src", "out", "rep", "input");
        graph.add_connection("rep", "output", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");
        check_graph_clean(runtime.compiled_graph(), "Repeat scalar→8 lanes");

        runtime.tick(0.0, 1.0 / 60.0, 0);

        auto* sink = runtime.compiled_graph()->find_node("sink");
        check(sink != nullptr, "sink found");
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 8, "output has 8 elements");
            bool all_five = true;
            for (size_t i = 0; i < sp.size(); ++i) {
                if (std::fabs(sp[i] - 5.0f) > 0.01f) all_five = false;
            }
            check(all_five, "all elements = 5.0");
        }
    }

    // --- Test 2: Tile ---
    std::fprintf(stderr, "\n--- Tile: [10,20,30] → 9 elements ---\n");
    {
        vivid::Graph graph;
        // LaneSourceOp(base=10, count=3) → lane array [10,20,30]
        // Tile(count=9) → [10,20,30, 10,20,30, 10,20,30]
        graph.add_node("src", "LaneSourceOp", {{"base", 10.0f}, {"count", 3.0f}});
        graph.add_node("til", "Tile", {{"count", 9.0f}});
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src", "out", "til", "input");
        graph.add_connection("til", "output", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");
        check_graph_clean(runtime.compiled_graph(), "Tile [10,20,30]→9 elements");

        runtime.tick(0.0, 1.0 / 60.0, 0);

        auto* sink = runtime.compiled_graph()->find_node("sink");
        check(sink != nullptr, "sink found");
        if (sink && !sink->output_lanes.empty()) {
            const auto& sp = sink->output_lanes[0];
            check(sp.size() == 9, "output has 9 elements");
            if (sp.size() == 9) {
                float expected[] = {10, 20, 30, 10, 20, 30, 10, 20, 30};
                for (int i = 0; i < 9; ++i) {
                    char msg[64];
                    std::snprintf(msg, sizeof(msg), "tile[%d] = %.0f", i, expected[i]);
                    check_float(sp[i], expected[i], 0.01f, msg);
                }
            }
        }
    }

    // --- Test 3: Select ---
    std::fprintf(stderr, "\n--- Select: lane 2 from [1,2,3,4] ---\n");
    {
        vivid::Graph graph;
        // LaneSourceOp(base=1, count=4) → lane array [1,2,3,4]
        // Select(lane=2) → scalar 3.0
        graph.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 4.0f}});
        graph.add_node("sel", "Select", {{"lane", 2.0f}});
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src", "out", "sel", "input");
        graph.add_connection("sel", "output", "sink", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build()");
        check_graph_clean(runtime.compiled_graph(), "Select lane 2 from [1,2,3,4]");

        runtime.tick(0.0, 1.0 / 60.0, 0);

        auto* sink = runtime.compiled_graph()->find_node("sink");
        check(sink != nullptr, "sink found");
        if (sink) {
            // Select outputs scalar → sink sees input_values[0] = 3.0
            check_float(sink->output_values[0], 3.0f, 0.01f, "selected value = 3.0");
        }
    }

    // --- Test 4: Mismatch resolution ---
    // Two different lane sources → one through Select → pointwise Math → compiles
    std::fprintf(stderr, "\n--- mismatch resolution via Select ---\n");
    {
        vivid::Graph graph;
        // src_a(base=1, count=4) → lane array [1,2,3,4], provenance_group_id=X
        // src_b(base=10, count=3) → lane array [10,20,30], provenance_group_id=Y
        // Select(lane=0) on src_b → scalar 10.0 (Reduction → provenance_group_id=0)
        // Math(add): a=src_a, b=Select(src_b)
        // src_a is non-scalar (provenance_group_id=X), Select output is scalar → no mismatch
        graph.add_node("src_a", "LaneSourceOp", {{"base", 1.0f}, {"count", 4.0f}});
        graph.add_node("src_b", "LaneSourceOp", {{"base", 10.0f}, {"count", 3.0f}});
        graph.add_node("sel", "Select", {{"lane", 0.0f}});
        graph.add_node("add", "Math", {{"operation", 0.0f}});
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src_b", "out", "sel", "input");
        graph.add_connection("src_a", "out", "add", "a");
        graph.add_connection("sel", "output", "add", "b");
        graph.add_connection("add", "result", "sink", "in");

        vivid::RuntimeCore runtime;
        bool built = runtime.build(graph, registry);
        check(built, "compiles without lane mismatch (Select reduces to scalar)");

        if (built) {
            check_graph_clean(runtime.compiled_graph(), "mismatch resolution via Select");
            runtime.tick(0.0, 1.0 / 60.0, 0);

            auto* sink = runtime.compiled_graph()->find_node("sink");
            check(sink != nullptr, "sink found");
            if (sink) {
                // Math sees: a = input_values[0] = lane_array_a[0] = 1.0
                //            b = input_values[1] = select(lane_array_b) = 10.0
                // result = 1.0 + 10.0 = 11.0
                check_float(sink->output_values[0], 11.0f, 0.01f,
                            "Math(add) scalar result = 11.0 (1 + 10)");
            }
        }
    }

    // --- Test 5: Compiler metadata — provenance IDs ---
    std::fprintf(stderr, "\n--- compiler metadata: provenance_group_id provenance ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 4.0f}});
        graph.add_node("rep", "Repeat", {{"count", 8.0f}});
        graph.add_node("til", "Tile", {{"count", 9.0f}});
        graph.add_node("sel", "Select", {{"lane", 0.0f}});
        graph.add_connection("src", "out", "rep", "input");
        graph.add_connection("src", "out", "til", "input");
        graph.add_connection("src", "out", "sel", "input");

        vivid::GraphCompiler::Options opts;
        auto cg = vivid::GraphCompiler::compile(graph, registry, opts);
        check(cg != nullptr, "compiles");

        if (cg) {
            auto* rep = cg->find_node("rep");
            auto* til = cg->find_node("til");
            auto* sel = cg->find_node("sel");
            auto* src = cg->find_node("src");

            // Source is Structural → non-scalar provenance_group_id
            check(src != nullptr && !src->output_value_envelopes.empty(), "src found with output_value_envelopes");
            if (src && !src->output_value_envelopes.empty()) {
                check(src->output_value_envelopes[0].provenance_group_id > 0,
                      "src: provenance_group_id > 0 (structural)");
            }

            // Repeat is Structural → fresh provenance_group_id, different from src
            check(rep != nullptr && !rep->output_value_envelopes.empty(), "rep found with output_value_envelopes");
            if (rep && !rep->output_value_envelopes.empty() && src && !src->output_value_envelopes.empty()) {
                check(rep->output_value_envelopes[0].provenance_group_id > 0,
                      "Repeat: provenance_group_id > 0 (structural)");
                check(rep->output_value_envelopes[0].provenance_group_id != src->output_value_envelopes[0].provenance_group_id,
                      "Repeat: fresh provenance_group_id (differs from src)");
            }

            // Tile is Structural → fresh provenance_group_id, different from src and rep
            check(til != nullptr && !til->output_value_envelopes.empty(), "til found with output_value_envelopes");
            if (til && !til->output_value_envelopes.empty()) {
                check(til->output_value_envelopes[0].provenance_group_id > 0,
                      "Tile: provenance_group_id > 0 (structural)");
                if (rep && !rep->output_value_envelopes.empty()) {
                    check(til->output_value_envelopes[0].provenance_group_id != rep->output_value_envelopes[0].provenance_group_id,
                          "Tile: fresh provenance_group_id (differs from Repeat)");
                }
            }

            // Select is Reduction → scalar provenance_group_id = 0
            check(sel != nullptr && !sel->output_value_envelopes.empty(), "sel found with output_value_envelopes");
            if (sel && !sel->output_value_envelopes.empty()) {
                check(sel->output_value_envelopes[0].provenance_group_id == 0,
                      "Select: provenance_group_id = 0 (reduction → scalar)");
                check(sel->output_value_envelopes[0].is_scalar(),
                      "Select: is_scalar() = true");
            }
        }
    }

    // --- Test 6: Tile mismatch resolution ---
    // Two lane sources with different provenance_group_ids → each through Tile to
    // same length → feed pointwise Math → verify compilation succeeds.
    // This is the specific doc scenario: "mismatched lane sets → Tile → now legal"
    std::fprintf(stderr, "\n--- Tile mismatch resolution ---\n");
    {
        vivid::Graph graph;
        // Two independent lane sources → different provenance_group_ids
        graph.add_node("src_a", "LaneSourceOp", {{"base", 1.0f}, {"count", 3.0f}});
        graph.add_node("src_b", "LaneSourceOp", {{"base", 10.0f}, {"count", 4.0f}});
        // Tile both to the same length (12)
        graph.add_node("tile_a", "Tile", {{"count", 12.0f}});
        graph.add_node("tile_b", "Tile", {{"count", 12.0f}});
        // Pointwise Math(add) on both tiled outputs
        graph.add_node("add", "Math", {{"operation", 0.0f}});
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src_a", "out", "tile_a", "input");
        graph.add_connection("src_b", "out", "tile_b", "input");
        graph.add_connection("tile_a", "output", "add", "a");
        graph.add_connection("tile_b", "output", "add", "b");
        graph.add_connection("add", "result", "sink", "in");

        vivid::RuntimeCore runtime;
        bool built = runtime.build(graph, registry);
        // Value model (lane-value 7e.5b): the Pass-2.6 lane-set legality check — which
        // hard-rejected two different-provenance Many inputs at a Pointwise node — was
        // retired with Pass 2.6. The value model is permissive: two Many inputs to a MAP
        // operator vectorize with POSITIONAL alignment (lane i of A with lane i of B),
        // a well-defined behavior, so the graph now compiles cleanly.
        check(built, "two independent Tile outputs at MAP node → compiles (positional alignment)");
        if (built)
            check_graph_clean(runtime.compiled_graph(), "two-provenance Tile → MAP");
    }

    // --- Test 7: Kernel consumer accepts independent structural inputs ---
    std::fprintf(stderr, "\n--- Metaball kernel accepts independent Repeat lanes ---\n");
    {
        vivid::Graph graph;
        graph.add_node("repeat_x", "Repeat", {{"count", 8.0f}, {"mode", 1.0f}, {"spread", 0.35f}});
        graph.add_node("repeat_y", "Repeat", {{"count", 8.0f}, {"mode", 3.0f}});
        graph.add_node("metaball", "Metaball", {{"count", 8.0f}});
        graph.add_connection("repeat_x", "output", "metaball", "pos_x");
        graph.add_connection("repeat_y", "output", "metaball", "pos_y");

        vivid::GraphCompiler::Options opts;
        auto cg = vivid::GraphCompiler::compile(graph, registry, opts);
        check(cg != nullptr, "independent Repeat lanes compile into kernel Metaball");

        if (cg) {
            auto* meta = cg->find_node("metaball");
            check(meta != nullptr, "metaball found");
            if (meta) {
                check(meta->multiplicity_behavior == VIVID_MULTIPLICITY_KERNEL,
                      "Metaball declares Kernel multiplicity behavior");
            }
        }
    }

    // --- Test 8: Tile from same provenance is legal ---
    // Single lane source → Tile → alongside original source → pointwise Math
    // Both carry the SAME provenance (scalar broadcast), so this SHOULD compile.
    std::fprintf(stderr, "\n--- Tile same-provenance is legal ---\n");
    {
        vivid::Graph graph;
        graph.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 4.0f}});
        graph.add_node("til", "Tile", {{"count", 8.0f}});
        graph.add_node("sink", "LaneSinkOp");
        graph.add_connection("src", "out", "til", "input");
        graph.add_connection("til", "output", "sink", "in");

        vivid::RuntimeCore runtime;
        bool built = runtime.build(graph, registry);
        check(built, "Tile from single source compiles");

        if (built) {
            check_graph_clean(runtime.compiled_graph(), "Tile same-provenance");
            runtime.tick(0.0, 1.0 / 60.0, 0);
            auto* sink = runtime.compiled_graph()->find_node("sink");
            if (sink && !sink->output_lanes.empty()) {
                check(sink->output_lanes[0].size() == 8, "Tiled output has 8 elements");
            }
        }
    }

    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
