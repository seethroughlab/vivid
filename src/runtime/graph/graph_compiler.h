#pragma once

#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"
#include <filesystem>
#include <string>
#include <unordered_set>

namespace vivid {

// ---------------------------------------------------------------------------
// GraphCompiler — builds a CompiledGraph from a Graph + OperatorRegistry.
//
// Single compile pass that determines cadence per node, classifies edges
// as Direct or Snapshot, builds independent topological orders for each
// cadence, and pre-allocates all execution state.
// ---------------------------------------------------------------------------

class GraphCompiler {
public:
    struct Options {
        // Base directory for resolving relative file paths in node params.
        std::filesystem::path graph_base_dir;

        // Source directory for operators (for shader hot-reload paths).
        std::string operators_src_dir;

        // Default GPU texture dimensions.
        uint32_t default_tex_width  = 1280;
        uint32_t default_tex_height = 720;

        // Audio buffer size and sample rate.
        uint32_t audio_buffer_size = 256;
        uint32_t audio_sample_rate = 48000;

        // Maximum lane count for LoopBased audio operators.
        // Buffers are pre-allocated to this capacity at compile time.
        uint32_t max_loop_lanes = 16;

        // Crash-recovery / safe-mode: nodes whose instances must not be
        // created.  The compiler treats them as missing operators with reason
        // "disabled", port stubs are still allocated, and Pass 2 drops all
        // edges touching them into CompiledGraph::dropped_connections.
        std::unordered_set<std::string> disabled_node_ids;
        std::unordered_set<std::string> disabled_types;

        // Quarantine: types flagged by the crash-history scan (Phase 4) as
        // repeat offenders.  Classification chain prefers "disabled" when a
        // type appears in both; otherwise the compiler uses reason
        // "quarantined" so the UI can distinguish the two suppression causes.
        std::unordered_set<std::string> quarantined_types;
    };

    // Compile a Graph into a ready-to-execute CompiledGraph.
    // Returns nullptr on failure (cycle detected, missing operators, etc.).
    static std::unique_ptr<CompiledGraph> compile(
        const Graph& graph,
        OperatorRegistry& registry,
        const Options& options);

    // Initialize the frame-side state on a CompiledNode (ports, params, lanes,
    // strings, custom ports, file params, GPU resources).
    static void init_frame_state(
        CompiledNode& cn,
        const VividOperatorDescriptor* desc,
        const std::unordered_map<std::string, float>* param_overrides,
        const std::unordered_map<std::string, std::string>* string_overrides,
        const std::filesystem::path& graph_base_dir);

    // Initialize audio-specific state on a CompiledNode (channel counts, audio
    // buffers, lane/string/custom port staging).
    static void init_audio_state(
        CompiledNode& cn,
        const VividOperatorDescriptor* desc,
        uint32_t buffer_size);

    // Hot-reload: destroy old instances of a given type, swap the dylib via
    // the registry, and recreate instances with param reconciliation.
    static bool reload_operator(
        CompiledGraph& cg,
        const std::string& type_name,
        OperatorRegistry& registry,
        const std::string& new_dylib_path,
        const std::filesystem::path& graph_base_dir);
};

} // namespace vivid
