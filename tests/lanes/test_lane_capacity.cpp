// Test: LoopBased lane-capacity limit is configurable and enforced.
//
// Verifies:
// 1. Buffers are pre-allocated to max_loop_lanes (not hardcoded 16)
// 2. max_loop_lanes is stored on CompiledGraph for runtime use
// 3. Default value remains 16 for backward compatibility

#include "runtime/graph/graph_compiler.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/lane_types.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include "test_helpers.h"

// Build a LaneSourceOp -> LaneSlewOp graph, compile with the given max_loop_lanes,
// and return the compiled graph for inspection.
static std::unique_ptr<vivid::CompiledGraph> compile_with_limit(
    const std::string& staging, uint32_t max_loop_lanes)
{
    vivid::OperatorRegistry registry;
    registry.scan_deferred(staging.c_str());

    vivid::Graph g;
    g.add_node("src", "LaneSourceOp");
    g.add_node("slew", "LaneSlewOp");
    g.add_connection("src", "out", "slew", "input");
    g.set_connection_bridge("src", "out", "slew", "input", "snapshot");

    vivid::GraphCompiler::Options opts;
    opts.max_loop_lanes = max_loop_lanes;
    return vivid::GraphCompiler::compile(g, registry, opts);
}

static void test_custom_limit(const std::string& staging) {
    std::fprintf(stderr, "\n--- custom max_loop_lanes=8 ---\n");

    auto cg = compile_with_limit(staging, 8);
    check(cg != nullptr, "compiles");
    if (!cg) return;

    check(cg->max_loop_lanes == 8, "CompiledGraph stores max_loop_lanes=8");

    auto* slew = cg->find_node("slew");
    check(slew != nullptr, "LaneSlewOp node found");
    if (!slew || !slew->audio) return;

    check(slew->audio->execution_strategy == vivid::LaneExecutionStrategy::LoopBased,
          "LaneSlewOp assigned LoopBased strategy");

    // Buffer size should be max_loop_lanes * audio_buffer_size (default 256)
    uint32_t expected_buf_size = 8 * 256;
    check(!slew->audio->buffers_in.empty(), "has input buffers");
    if (!slew->audio->buffers_in.empty()) {
        check(slew->audio->buffers_in[0].size() == expected_buf_size,
              "input buffer sized to 8 * 256 = 2048");
    }
    check(!slew->audio->buffers_out.empty(), "has output buffers");
    if (!slew->audio->buffers_out.empty()) {
        check(slew->audio->buffers_out[0].size() == expected_buf_size,
              "output buffer sized to 8 * 256 = 2048");
    }
}

static void test_large_limit(const std::string& staging) {
    std::fprintf(stderr, "\n--- large max_loop_lanes=32 ---\n");

    auto cg = compile_with_limit(staging, 32);
    check(cg != nullptr, "compiles");
    if (!cg) return;

    check(cg->max_loop_lanes == 32, "CompiledGraph stores max_loop_lanes=32");

    auto* slew = cg->find_node("slew");
    if (!slew || !slew->audio) return;

    uint32_t expected_buf_size = 32 * 256;
    if (!slew->audio->buffers_in.empty()) {
        check(slew->audio->buffers_in[0].size() == expected_buf_size,
              "input buffer sized to 32 * 256 = 8192");
    }
    if (!slew->audio->buffers_out.empty()) {
        check(slew->audio->buffers_out[0].size() == expected_buf_size,
              "output buffer sized to 32 * 256 = 8192");
    }
}

static void test_default_limit(const std::string& staging) {
    std::fprintf(stderr, "\n--- default max_loop_lanes (backward compat) ---\n");

    vivid::OperatorRegistry registry;
    registry.scan_deferred(staging.c_str());

    vivid::Graph g;
    g.add_node("src", "LaneSourceOp");
    g.add_node("slew", "LaneSlewOp");
    g.add_connection("src", "out", "slew", "input");
    g.set_connection_bridge("src", "out", "slew", "input", "snapshot");

    vivid::GraphCompiler::Options opts;  // default: max_loop_lanes = 16
    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "compiles");
    if (!cg) return;

    check(cg->max_loop_lanes == 16, "default max_loop_lanes is 16");

    auto* slew = cg->find_node("slew");
    if (!slew || !slew->audio) return;

    uint32_t expected_buf_size = 16 * 256;
    if (!slew->audio->buffers_in.empty()) {
        check(slew->audio->buffers_in[0].size() == expected_buf_size,
              "default input buffer sized to 16 * 256 = 4096");
    }
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    // Stage operators
    const std::string staging = build_dir + "/.test_lane_capacity_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
    };
    stage("lane_source_op.dylib");
    stage("lane_slew_op.dylib");

    std::fprintf(stderr, "=== test_lane_capacity ===\n");

    test_custom_limit(staging);
    test_large_limit(staging);
    test_default_limit(staging);

    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
