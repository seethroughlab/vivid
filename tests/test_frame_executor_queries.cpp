// Unit tests for FrameExecutor query methods (solo, GPU sink detection).
// Tests the pure-logic query paths — no GPU device or operator loading required.

#include "runtime/frame_executor.h"
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

// Build a minimal CompiledGraph with the given node configurations.
// Each node gets an entry in frame_order.
struct NodeSpec {
    std::string id;
    bool is_gpu;
    bool is_sink;
    bool has_tex_output;
};

static vivid::CompiledGraph make_graph(const std::vector<NodeSpec>& specs) {
    vivid::CompiledGraph cg;
    for (uint32_t i = 0; i < specs.size(); ++i) {
        vivid::CompiledNode cn;
        cn.node_id = specs[i].id;
        cn.type_name = specs[i].id;
        if (specs[i].is_gpu) {
            cn.gpu = std::make_unique<vivid::GpuNodeState>();
            cn.gpu->is_sink = specs[i].is_sink;
            cn.gpu->has_texture_output = specs[i].has_tex_output;
        }
        cg.nodes.push_back(std::move(cn));
        cg.node_id_to_index[specs[i].id] = i;
        cg.frame_order.push_back(i);
    }
    return cg;
}

// ---------------------------------------------------------------------------
// Solo tests
// ---------------------------------------------------------------------------

static void test_solo_defaults() {
    std::fprintf(stderr, "\n--- solo: default state ---\n");

    vivid::FrameExecutor exec;
    check(exec.solo_node_idx() == -1, "default solo_node_idx is -1");
    check(!exec.is_solo_active(), "solo not active by default");
    check(exec.solo_active_set().empty(), "active set empty by default");
}

static void test_solo_set_and_get() {
    std::fprintf(stderr, "\n--- solo: set and get ---\n");

    vivid::FrameExecutor exec;
    std::vector<bool> active = {true, false, true};
    exec.set_solo(2, active);

    check(exec.solo_node_idx() == 2, "solo_node_idx == 2");
    check(exec.is_solo_active(), "solo is active");
    check(exec.solo_active_set().size() == 3, "active set size 3");
    check(exec.solo_active_set()[0] == true, "active[0] true");
    check(exec.solo_active_set()[1] == false, "active[1] false");
    check(exec.solo_active_set()[2] == true, "active[2] true");
}

static void test_solo_clear() {
    std::fprintf(stderr, "\n--- solo: clear ---\n");

    vivid::FrameExecutor exec;
    exec.set_solo(1, {true, true});
    exec.set_solo(-1, {});

    check(!exec.is_solo_active(), "solo cleared");
    check(exec.solo_node_idx() == -1, "back to -1");
}

// ---------------------------------------------------------------------------
// find_gpu_sink tests
// ---------------------------------------------------------------------------

static void test_find_gpu_sink_none() {
    std::fprintf(stderr, "\n--- find_gpu_sink: no GPU nodes ---\n");

    vivid::FrameExecutor exec;
    auto cg = make_graph({
        {"lfo", false, false, false},
        {"gain", false, false, false},
    });
    check(exec.find_gpu_sink(cg) == -1, "no GPU nodes → -1");
}

static void test_find_gpu_sink_found() {
    std::fprintf(stderr, "\n--- find_gpu_sink: GPU sink present ---\n");

    vivid::FrameExecutor exec;
    auto cg = make_graph({
        {"shape",     true, false, true},  // generator (has output, not sink)
        {"composite", true, true, false},  // sink (has input, no output)
    });
    int idx = exec.find_gpu_sink(cg);
    check(idx == 1, "finds sink at index 1");
}

static void test_find_gpu_sink_no_sink_among_gpu() {
    std::fprintf(stderr, "\n--- find_gpu_sink: GPU nodes but no sink ---\n");

    vivid::FrameExecutor exec;
    auto cg = make_graph({
        {"shape",  true, false, true},
        {"bloom",  true, false, true},
    });
    check(exec.find_gpu_sink(cg) == -1, "no sink among GPU nodes");
}

// ---------------------------------------------------------------------------
// find_effective_gpu_sink tests
// ---------------------------------------------------------------------------

static void test_effective_sink_no_solo() {
    std::fprintf(stderr, "\n--- find_effective_gpu_sink: no solo ---\n");

    vivid::FrameExecutor exec;
    auto cg = make_graph({
        {"shape",     true, false, true},
        {"composite", true, true, false},
    });
    check(exec.find_effective_gpu_sink(cg) == 1, "falls through to find_gpu_sink");
}

static void test_effective_sink_solo_with_texture() {
    std::fprintf(stderr, "\n--- find_effective_gpu_sink: solo node with texture output ---\n");

    vivid::FrameExecutor exec;
    auto cg = make_graph({
        {"shape",     true, false, true},   // index 0: has texture output
        {"composite", true, true, false},   // index 1: sink
    });
    exec.set_solo(0, {true, false});

    int idx = exec.find_effective_gpu_sink(cg);
    check(idx == 0, "solo node 0 with texture output used as effective sink");
}

static void test_effective_sink_solo_without_texture() {
    std::fprintf(stderr, "\n--- find_effective_gpu_sink: solo node without texture output ---\n");

    vivid::FrameExecutor exec;
    auto cg = make_graph({
        {"lfo",       false, false, false}, // index 0: control, no GPU
        {"composite", true, true, false},   // index 1: sink
    });
    exec.set_solo(0, {true, false});

    int idx = exec.find_effective_gpu_sink(cg);
    check(idx == 1, "solo without texture → falls back to find_gpu_sink");
}

// ---------------------------------------------------------------------------
// has_gpu_operators tests
// ---------------------------------------------------------------------------

static void test_has_gpu_operators() {
    std::fprintf(stderr, "\n--- has_gpu_operators ---\n");

    vivid::FrameExecutor exec;

    auto no_gpu = make_graph({{"lfo", false, false, false}});
    check(!exec.has_gpu_operators(no_gpu), "no GPU operators");

    auto with_gpu = make_graph({
        {"lfo", false, false, false},
        {"shape", true, false, true},
    });
    check(exec.has_gpu_operators(with_gpu), "has GPU operators");
}

// ---------------------------------------------------------------------------
// gpu_sink_source_size tests
// ---------------------------------------------------------------------------

static void test_gpu_sink_source_size() {
    std::fprintf(stderr, "\n--- gpu_sink_source_size ---\n");

    vivid::FrameExecutor exec;
    auto cg = make_graph({
        {"shape",     true, false, true},
        {"composite", true, true, false},
    });
    // Set upstream texture size
    cg.nodes[0].gpu->tex_width = 1920;
    cg.nodes[0].gpu->tex_height = 1080;

    // Add a texture edge from shape → composite
    vivid::CompiledEdge edge{};
    edge.from_node = 0;
    edge.from_port = 0;
    edge.to_node = 1;
    edge.to_port = 0;
    edge.data_type = VIVID_PORT_TEXTURE;
    edge.targets_param = false;
    cg.edges.push_back(edge);

    uint32_t w = 0, h = 0;
    bool ok = exec.gpu_sink_source_size(cg, 1, w, h);
    check(ok, "found source size");
    check(w == 1920, "width 1920");
    check(h == 1080, "height 1080");
}

static void test_gpu_sink_source_size_no_edge() {
    std::fprintf(stderr, "\n--- gpu_sink_source_size: no upstream edge ---\n");

    vivid::FrameExecutor exec;
    auto cg = make_graph({{"composite", true, true, false}});

    uint32_t w = 0, h = 0;
    bool ok = exec.gpu_sink_source_size(cg, 0, w, h);
    check(!ok, "no upstream edge → false");
}

// ---------------------------------------------------------------------------
// needs_gpu_realloc tests
// ---------------------------------------------------------------------------

static void test_gpu_realloc_flag() {
    std::fprintf(stderr, "\n--- needs_gpu_realloc ---\n");

    vivid::FrameExecutor exec;
    check(!exec.needs_gpu_realloc(), "default false");
    exec.clear_gpu_realloc();
    check(!exec.needs_gpu_realloc(), "still false after clear");
}

// ---------------------------------------------------------------------------
// operators_src_dir tests
// ---------------------------------------------------------------------------

static void test_operators_src_dir() {
    std::fprintf(stderr, "\n--- operators_src_dir ---\n");

    vivid::FrameExecutor exec;
    check(exec.operators_src_dir().empty(), "default empty");
    exec.set_operators_src_dir("/path/to/ops");
    check(exec.operators_src_dir() == "/path/to/ops", "set and get");
}

// ---------------------------------------------------------------------------

int main() {
    std::fprintf(stderr, "=== test_frame_executor_queries ===\n");

    test_solo_defaults();
    test_solo_set_and_get();
    test_solo_clear();
    test_find_gpu_sink_none();
    test_find_gpu_sink_found();
    test_find_gpu_sink_no_sink_among_gpu();
    test_effective_sink_no_solo();
    test_effective_sink_solo_with_texture();
    test_effective_sink_solo_without_texture();
    test_has_gpu_operators();
    test_gpu_sink_source_size();
    test_gpu_sink_source_size_no_edge();
    test_gpu_realloc_flag();
    test_operators_src_dir();

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
