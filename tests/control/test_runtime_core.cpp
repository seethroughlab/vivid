#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <string>
#include "test_helpers.h"

// Helper: find node index by id in a runtime
static int find_idx(const vivid::RuntimeCore& runtime, const std::string& id) {
    const auto& nodes = runtime.compiled_graph()->nodes;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].node_id == id) return static_cast<int>(i);
    }
    return -1;
}

int main() {
    // --- Set up shared registry ---
    std::string staging = "./.test_sched_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file("test_op_v1.dylib", staging + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file("control_pass_op.dylib", staging + "/control_pass_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file("lane_source_op.dylib", staging + "/lane_source_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file("audio_test_op.dylib", staging + "/audio_test_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("TestOp") != nullptr, "TestOp registered");
    check(registry.find("ControlPassOp") != nullptr, "ControlPassOp registered");
    check(registry.find("LaneSourceOp") != nullptr, "LaneSourceOp registered");
    check(registry.find("AudioTestOp") != nullptr, "AudioTestOp registered");

    // =====================================================================
    // Test 1: Linear chain  TestOp(scale=1) → ControlPassOp(gain=2) → ControlPassOp(gain=3)
    // TestOp: output = scale * 2.0 = 1*2 = 2.0
    // ControlPassOp(gain=2): output = 2.0 * 2 = 4.0
    // ControlPassOp(gain=3): output = 4.0 * 3 = 12.0
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Linear chain ===\n");
        vivid::Graph g;
        g.add_node("a", "TestOp", {{"scale", 1.0f}});
        g.add_node("b", "ControlPassOp", {{"gain", 2.0f}});
        g.add_node("c", "ControlPassOp", {{"gain", 3.0f}});
        g.add_connection("a", "out", "b", "in");
        g.add_connection("b", "out", "c", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");
        runtime.tick(0.0, 0.016, 0);

        auto* na = runtime.compiled_graph()->find_node("a");
        auto* nb = runtime.compiled_graph()->find_node("b");
        auto* nc = runtime.compiled_graph()->find_node("c");
        check(na && nb && nc, "all nodes found");
        check_float(na->output_values[0], 2.0f, "a output = 2.0");
        check_float(nb->output_values[0], 4.0f, "b output = 4.0");
        check_float(nc->output_values[0], 12.0f, "c output = 12.0");
        runtime.shutdown();
    }

    // =====================================================================
    // Test 1b: prepare_build/adopt_prepared_build
    // Prepared builds should compile without mutating live runtime state and
    // then adopt into RuntimeCore with the same execution results as build().
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 1b: Prepared build adoption ===\n");
        vivid::Graph g;
        g.add_node("a", "TestOp", {{"scale", 2.0f}});
        g.add_node("b", "ControlPassOp", {{"gain", 4.0f}});
        g.add_connection("a", "out", "b", "in");

        vivid::RuntimeCore runtime;
        vivid::RuntimeCore::PreparedBuild prepared;
        check(runtime.prepare_build(g, registry, prepared), "prepare_build succeeds");
        check(prepared.compiled_graph != nullptr, "prepare_build returns compiled graph");
        check(runtime.compiled_graph() == nullptr, "prepare_build leaves live runtime untouched");

        runtime.adopt_prepared_build(std::move(prepared));
        runtime.tick(0.0, 0.016, 0);

        auto* na = runtime.compiled_graph()->find_node("a");
        auto* nb = runtime.compiled_graph()->find_node("b");
        check(na && nb, "prepared-build nodes found");
        check_float(na->output_values[0], 4.0f, "prepared a output = 4.0");
        check_float(nb->output_values[0], 16.0f, "prepared b output = 16.0");
        runtime.shutdown();
    }

    // =====================================================================
    // Test 1c: adopt_prepared_build with a null PreparedBuild is a guarded no-op
    // (audit 03-R2-F4/F9) — must not crash and must leave the live graph intact.
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 1c: Null prepared-build adoption is a no-op ===\n");
        vivid::Graph g;
        g.add_node("a", "TestOp", {{"scale", 1.0f}});

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");
        const auto* live_before = runtime.compiled_graph();
        check(live_before != nullptr, "live graph present after build");

        // Default-constructed PreparedBuild carries a null compiled_graph.
        runtime.adopt_prepared_build(vivid::RuntimeCore::PreparedBuild{});

        check(runtime.compiled_graph() == live_before,
              "null adopt left the live compiled graph unchanged");
        runtime.tick(0.0, 0.016, 0);  // still ticks without crashing
        check(runtime.compiled_graph()->find_node("a") != nullptr,
              "live graph still usable after null adopt");
        runtime.shutdown();
    }

    // =====================================================================
    // Test 2: Diamond topology
    // a(TestOp,scale=3) → b(gain=2), a → c(gain=5), b→d/in, c→d/gain
    // a: output = 3*2 = 6
    // b: output = 6*2 = 12
    // c: output = 6*5 = 30
    // d: input=12, gain overridden to 30, output = 12*30 = 360
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Diamond topology ===\n");
        vivid::Graph g;
        g.add_node("a", "TestOp", {{"scale", 3.0f}});
        g.add_node("b", "ControlPassOp", {{"gain", 2.0f}});
        g.add_node("c", "ControlPassOp", {{"gain", 5.0f}});
        g.add_node("d", "ControlPassOp", {{"gain", 1.0f}});
        g.add_connection("a", "out", "b", "in");
        g.add_connection("a", "out", "c", "in");
        g.add_connection("b", "out", "d", "in");
        g.add_connection("c", "out", "d", "gain");  // param wire

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        // Verify evaluation order: a before b,c; b,c before d
        int ia = find_idx(runtime, "a");
        int ib = find_idx(runtime, "b");
        int ic = find_idx(runtime, "c");
        int id = find_idx(runtime, "d");
        check(ia >= 0 && ib >= 0 && ic >= 0 && id >= 0, "all nodes have indices");
        check(ia < ib && ia < ic, "a before b and c");
        check(ib < id && ic < id, "b and c before d");

        runtime.tick(0.0, 0.016, 0);

        auto* na = runtime.compiled_graph()->find_node("a");
        auto* nb = runtime.compiled_graph()->find_node("b");
        auto* nc = runtime.compiled_graph()->find_node("c");
        auto* nd = runtime.compiled_graph()->find_node("d");
        check_float(na->output_values[0], 6.0f, "a output = 6.0");
        check_float(nb->output_values[0], 12.0f, "b output = 12.0");
        check_float(nc->output_values[0], 30.0f, "c output = 30.0");
        check_float(nd->output_values[0], 360.0f, "d output = 360.0");
        runtime.shutdown();
    }

    // =====================================================================
    // Test 3: Cycle detection
    // a(ControlPassOp) → b → a  (cycle)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: Cycle detection ===\n");
        vivid::Graph g;
        g.add_node("a", "ControlPassOp", {});
        g.add_node("b", "ControlPassOp", {});
        g.add_connection("a", "out", "b", "in");
        g.add_connection("b", "out", "a", "in");

        vivid::RuntimeCore runtime;
        check(!runtime.build(g, registry), "build returns false for cycle");
    }

    // =====================================================================
    // Test 4: Generation tracking
    // a→b→c chain, 3 ticks
    // Tick 1: all gens bump (first eval, outputs change from 0)
    // Tick 2 (unchanged): gens stable (outputs same as tick 1)
    // Tick 3 (a/scale changed): all gens bump again
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: Generation tracking ===\n");
        vivid::Graph g;
        g.add_node("a", "TestOp", {{"scale", 1.0f}});
        g.add_node("b", "ControlPassOp", {{"gain", 2.0f}});
        g.add_node("c", "ControlPassOp", {{"gain", 3.0f}});
        g.add_connection("a", "out", "b", "in");
        g.add_connection("b", "out", "c", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        // Tick 1: first evaluation, all outputs compute from defaults
        runtime.tick(0.0, 0.016, 0);
        auto* na = runtime.compiled_graph()->find_node("a");
        auto* nb = runtime.compiled_graph()->find_node("b");
        auto* nc = runtime.compiled_graph()->find_node("c");

        check(na->processed_this_tick, "tick 1: a processed");
        check(nb->processed_this_tick, "tick 1: b processed");
        check(nc->processed_this_tick, "tick 1: c processed");

        // Tick 2: nothing changed — nodes should still process (time-dependent or root)
        runtime.tick(0.0, 0.016, 1);

        // Tick 3: change a's scale param → mark dirty, all downstream should reprocess
        na->param_values[0] = 5.0f;  // scale = 5
        na->dirty = true;
        runtime.tick(0.0, 0.016, 2);

        // Verify new values: a=5*2=10, b=10*2=20, c=20*3=60
        check_float(na->output_values[0], 10.0f, "tick 3: a output = 10.0");
        check_float(nb->output_values[0], 20.0f, "tick 3: b output = 20.0");
        check_float(nc->output_values[0], 60.0f, "tick 3: c output = 60.0");
        runtime.shutdown();
    }

    // =====================================================================
    // Test 5: Param wire
    // src(TestOp,scale=2) → dst/in, mod(TestOp,scale=3) → dst/gain
    // src: output = 2*2 = 4.0
    // mod: output = 3*2 = 6.0
    // dst: input=4.0, gain overridden to 6.0, output = 4.0 * 6.0 = 24.0
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: Param wire ===\n");
        vivid::Graph g;
        g.add_node("src", "TestOp", {{"scale", 2.0f}});
        g.add_node("mod", "TestOp", {{"scale", 3.0f}});
        g.add_node("dst", "ControlPassOp", {{"gain", 1.0f}});
        g.add_connection("src", "out", "dst", "in");
        g.add_connection("mod", "out", "dst", "gain");  // param wire

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");
        runtime.tick(0.0, 0.016, 0);

        auto* nsrc = runtime.compiled_graph()->find_node("src");
        auto* nmod = runtime.compiled_graph()->find_node("mod");
        auto* ndst = runtime.compiled_graph()->find_node("dst");
        check_float(nsrc->output_values[0], 4.0f, "src output = 4.0");
        check_float(nmod->output_values[0], 6.0f, "mod output = 6.0");
        check_float(ndst->input_values[0], 4.0f, "dst input = 4.0");
        check_float(ndst->param_values[0], 6.0f, "dst gain overridden to 6.0");
        check_float(ndst->output_values[0], 24.0f, "dst output = 24.0");
        runtime.shutdown();
    }

    // =====================================================================
    // Test 6: Lane-array propagation
    // LaneSourceOp(base=1,count=4) → ControlPassOp(gain=2)
    // Source: scalar=1, lane_array=[1,2,3,4]
    // Pass: scalar=1*2=2, lane_array=[2,4,6,8]
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: Lane-array propagation ===\n");
        vivid::Graph g;
        g.add_node("src", "LaneSourceOp", {{"base", 1.0f}, {"count", 4.0f}});
        g.add_node("pass", "ControlPassOp", {{"gain", 2.0f}});
        g.add_connection("src", "out", "pass", "in");

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");
        runtime.tick(0.0, 0.016, 0);

        auto* nsrc = runtime.compiled_graph()->find_node("src");
        auto* npass = runtime.compiled_graph()->find_node("pass");

        check_float(nsrc->output_values[0], 1.0f, "src scalar = 1.0");
        check(nsrc->output_lanes[0].size() == 4, "src lane array has 4 elements");

        // After lane propagation, scalar fallback = lane_array[0] = 1.0, * gain 2 = 2.0
        check_float(npass->output_values[0], 2.0f, "pass scalar = 2.0");
        check(npass->output_lanes[0].size() == 4, "pass lane array has 4 elements");
        if (npass->output_lanes[0].size() == 4) {
            check_float(npass->output_lanes[0][0], 2.0f, "lane_array[0] = 2.0");
            check_float(npass->output_lanes[0][1], 4.0f, "lane_array[1] = 4.0");
            check_float(npass->output_lanes[0][2], 6.0f, "lane_array[2] = 6.0");
            check_float(npass->output_lanes[0][3], 8.0f, "lane_array[3] = 8.0");
        }
        runtime.shutdown();
    }

    // =====================================================================
    // Test 7: Audio node skip + param wire
    // ctrl(TestOp,scale=0.8) → audio(AudioTestOp)/level
    // Audio nodes are skipped in tick loop but param wires are propagated
    // audio.output_values[0] = 0 (skipped), audio.param_values[level] = 1.6
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: Audio node skip + param wire ===\n");
        vivid::Graph g;
        g.add_node("ctrl", "TestOp", {{"scale", 0.8f}});
        g.add_node("audio", "AudioTestOp", {{"level", 0.5f}});
        g.add_connection("ctrl", "out", "audio", "level");  // param wire
        g.set_connection_bridge("ctrl", "out", "audio", "level", "hold");

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        // AudioTestOp declares 3 analysis ports via append_analysis_ports()
        auto* naudio = runtime.compiled_graph()->find_node("audio");
        check(naudio != nullptr, "audio node found");
        check(naudio->output_port_count == 4, "audio has 4 output ports (1 declared + 3 analysis)");
        check(naudio->active_cadence == vivid::Cadence::Audio, "audio node flagged as audio");
        check(naudio->output_port_indices.count("rms") == 1, "rms in output_port_indices");
        check(naudio->output_port_indices.count("peak") == 1, "peak in output_port_indices");
        check(naudio->output_port_indices.count("waveform") == 1, "waveform in output_port_indices");
        check(naudio->audio->analysis_output_port_indices.count("rms") == 1, "rms in analysis map");
        check(naudio->audio->analysis_output_port_indices.count("peak") == 1, "peak in analysis map");
        check(naudio->audio->analysis_output_port_indices.count("waveform") == 1, "waveform in analysis map");

        runtime.tick(0.0, 0.016, 0);

        // Audio skipped in main loop → output stays 0
        check_float(naudio->output_values[0], 0.0f, "audio output = 0 (skipped)");

        // Post-loop param propagation: ctrl output = 0.8*2 = 1.6 → audio level
        check_float(naudio->param_values[0], 1.6f, "audio level param = 1.6 (from ctrl)");
        runtime.shutdown();
    }

    // =====================================================================
    // Test 8: Utility methods (renumbered from Test 9)
    // Mixed graph (TestOp + AudioTestOp)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 9: Utility methods ===\n");
        vivid::Graph g;
        g.add_node("ctrl", "TestOp", {{"scale", 1.0f}});
        g.add_node("audio", "AudioTestOp", {{"level", 0.5f}});

        vivid::RuntimeCore runtime;
        check(runtime.build(g, registry), "build succeeds");

        check(!runtime.has_gpu_operators(), "has_gpu = false");
        check(runtime.has_audio_operators(), "has_audio = true");

        // type_name
        int ctrl_idx = find_idx(runtime, "ctrl");
        int audio_idx = find_idx(runtime, "audio");
        check(ctrl_idx >= 0 && audio_idx >= 0, "node indices found");
        check(runtime.type_name(static_cast<uint32_t>(ctrl_idx)) == "TestOp", "type_name(ctrl) = TestOp");
        check(runtime.type_name(static_cast<uint32_t>(audio_idx)) == "AudioTestOp", "type_name(audio) = AudioTestOp");

        // find_node_mut
        check(runtime.compiled_graph()->find_node("ctrl") != nullptr, "find_node_mut(ctrl) works");
        check(runtime.compiled_graph()->find_node("nonexistent") == nullptr, "find_node_mut(nonexistent) = nullptr");

        // is_audio_type
        check(runtime.has_audio_cadence_type("AudioTestOp"), "is_audio_type(AudioTestOp) = true");
        check(!runtime.has_audio_cadence_type("TestOp"), "is_audio_type(TestOp) = false");

        runtime.shutdown();
    }

    // =====================================================================
    // Test 10: Audio sample-rate change → rebuild applies the new rate
    // (audit 03-F10) Covers the RuntimeCore side of the live device-switch
    // recompile path: main.cpp consumes a pending session rate, calls
    // set_audio_sample_rate(), then rebuilds from the Graph. A rebuild must
    // pick up the new rate (CompiledGraph::audio_sample_rate).
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 10: Audio sample-rate change → rebuild ===\n");
        vivid::Graph g;
        g.add_node("ctrl", "TestOp", {{"scale", 1.0f}});
        g.add_node("audio", "AudioTestOp", {{"level", 0.5f}});

        vivid::RuntimeCore runtime;
        check(runtime.audio_sample_rate() == 48000, "default audio sample rate = 48000");

        check(runtime.build(g, registry), "initial build succeeds");
        check(runtime.compiled_graph()->audio_sample_rate == 48000,
              "compiled graph defaults to 48000");

        // Simulate a device switch to 44100: set the rate, then rebuild.
        runtime.set_audio_sample_rate(44100);
        check(runtime.audio_sample_rate() == 44100, "set_audio_sample_rate(44100) sticks");
        check(runtime.build(g, registry), "rebuild after rate change succeeds");
        check(runtime.compiled_graph()->audio_sample_rate == 44100,
              "rebuild applies new rate (44100) to compiled graph");

        // A second device switch (96000) must also propagate on rebuild.
        runtime.set_audio_sample_rate(96000);
        check(runtime.build(g, registry), "rebuild at 96000 succeeds");
        check(runtime.compiled_graph()->audio_sample_rate == 96000,
              "second rate change (96000) applies on rebuild");

        runtime.shutdown();
    }

    // --- Cleanup ---
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
