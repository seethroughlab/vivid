// Test: GPU lane promotion planning (audit 01-F6).
//
// Unit-tests graph_compiler_internal::plan_gpu_lane_promotion() directly with
// hand-built CompiledGraphs — device-free, no dylibs. Verifies the conservative
// promotion policy:
//   1. A lane-array input ≥ threshold feeding a GPU-only consumer is promoted.
//   2. A lane-array input below threshold is NOT promoted.
//   3. A source that also feeds a CPU-frame consumer is NOT promoted (readback).
//   4. A source that also feeds an audio-cadence consumer is NOT promoted.

#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/graph_compiler_internal.h"
#include "runtime/graph/lane_types.h"
#include "operator_api/types.h"
#include <cstdio>
#include "test_helpers.h"

using namespace vivid;

namespace {

// Append a plain (non-GPU) node to the graph; returns its index.
uint32_t add_plain_node(CompiledGraph& cg, const char* id) {
    CompiledNode cn;
    cn.node_id = id;
    cg.nodes.push_back(std::move(cn));
    return static_cast<uint32_t>(cg.nodes.size() - 1);
}

// Append a GPU node with a single LANE_ARRAY input port carrying `lane_count`
// lanes (non-scalar provenance); returns its index.
uint32_t add_gpu_lane_consumer(CompiledGraph& cg, const char* id, uint32_t lane_count) {
    CompiledNode cn;
    cn.node_id = id;
    cn.gpu = std::make_unique<GpuNodeState>();
    cn.input_port_count = 1;
    cn.input_port_types = { VIVID_PORT_LANE_ARRAY };
    // GPU lane promotion now gates on declared multiplicity (lane-value 7d.5d.1),
    // not the LANE_ARRAY port type. This hand-built node must set it explicitly.
    cn.input_port_multiplicities = { VIVID_MULTIPLICITY_MANY };
    LaneSet ls;
    ls.lane_set_id = 7;          // non-zero → not scalar
    ls.lane_count = lane_count;
    cn.input_lane_sets = { ls };
    cg.nodes.push_back(std::move(cn));
    return static_cast<uint32_t>(cg.nodes.size() - 1);
}

void add_lane_edge(CompiledGraph& cg, uint32_t from_node, uint32_t to_node) {
    CompiledEdge e;
    e.from_node = from_node;
    e.from_port = 0;
    e.to_node = to_node;
    e.to_port = 0;
    e.data_type = VIVID_PORT_LANE_ARRAY;
    cg.edges.push_back(e);
}

bool promoted(const CompiledGraph& cg, uint32_t node) {
    const auto& g = cg.nodes[node].gpu;
    return g && !g->lane_input_gpu_promoted.empty() && g->lane_input_gpu_promoted[0];
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== test_gpu_lane_promotion ===\n\n");

    const uint32_t T = graph_compiler_internal::kGpuLanePromotionThreshold; // 256

    // --- Test 1: wide lane → GPU-only consumer is promoted ---
    {
        CompiledGraph cg;
        uint32_t src = add_plain_node(cg, "src");
        uint32_t gpu = add_gpu_lane_consumer(cg, "gpu", T + 44); // 300 ≥ 256
        add_lane_edge(cg, src, gpu);
        graph_compiler_internal::plan_gpu_lane_promotion(cg);
        check(promoted(cg, gpu), "wide lane (≥threshold) → GPU consumer promoted");
    }

    // --- Test 2: below threshold is NOT promoted ---
    {
        CompiledGraph cg;
        uint32_t src = add_plain_node(cg, "src");
        uint32_t gpu = add_gpu_lane_consumer(cg, "gpu", T - 1); // 255 < 256
        add_lane_edge(cg, src, gpu);
        graph_compiler_internal::plan_gpu_lane_promotion(cg);
        check(!promoted(cg, gpu), "lane below threshold NOT promoted");
    }

    // --- Test 3: source also feeds a CPU-frame consumer → NOT promoted ---
    {
        CompiledGraph cg;
        uint32_t src = add_plain_node(cg, "src");
        uint32_t gpu = add_gpu_lane_consumer(cg, "gpu", T + 44);
        uint32_t cpu = add_plain_node(cg, "cpu_frame"); // non-GPU consumer
        add_lane_edge(cg, src, gpu);
        add_lane_edge(cg, src, cpu);
        graph_compiler_internal::plan_gpu_lane_promotion(cg);
        check(!promoted(cg, gpu), "shared with CPU-frame consumer NOT promoted");
    }

    // --- Test 4: source also feeds an audio-cadence consumer → NOT promoted ---
    {
        CompiledGraph cg;
        uint32_t src = add_plain_node(cg, "src");
        uint32_t gpu = add_gpu_lane_consumer(cg, "gpu", T + 44);
        uint32_t aud = add_plain_node(cg, "audio");
        add_lane_edge(cg, src, gpu);
        add_lane_edge(cg, src, aud);
        cg.audio_order.push_back(aud); // mark as audio-cadence
        graph_compiler_internal::plan_gpu_lane_promotion(cg);
        check(!promoted(cg, gpu), "shared with audio consumer NOT promoted");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
