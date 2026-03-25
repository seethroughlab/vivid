#pragma once

#include "runtime/compiled_graph.h"
#include "runtime/graph.h"
#include "runtime/operator_registry.h"
#include <filesystem>
#include <string>

namespace vivid {

// ---------------------------------------------------------------------------
// GraphCompiler — builds a CompiledGraph from a Graph + OperatorRegistry.
//
// Single compile pass that replaces both Scheduler::build() and
// AudioEngine::build().  Determines cadence per node, classifies edges
// as Direct or Snapshot, builds independent topological orders for each
// cadence, and pre-allocates all execution state.
// ---------------------------------------------------------------------------

class GraphCompiler {
public:
    struct Options {
        // Base directory for resolving relative file paths in node params.
        std::filesystem::path graph_base_dir;

        // Source directory for operators (for WGSLFilter hot-reload paths).
        std::string operators_src_dir;

        // Default GPU texture dimensions.
        uint32_t default_tex_width  = 800;
        uint32_t default_tex_height = 600;

        // Audio buffer size and sample rate.
        uint32_t audio_buffer_size = 256;
        uint32_t audio_sample_rate = 48000;
    };

    // Compile a Graph into a ready-to-execute CompiledGraph.
    // Returns nullptr on failure (cycle detected, missing operators, etc.).
    static std::unique_ptr<CompiledGraph> compile(
        const Graph& graph,
        OperatorRegistry& registry,
        const Options& options);

    // Initialize the frame-side state on a CompiledNode (ports, params, spreads,
    // strings, custom ports, file params, GPU resources).  Public so that
    // Scheduler::reload_operator() can reinitialize a single node in-place.
    static void init_frame_state(
        CompiledNode& cn,
        const VividOperatorDescriptor* desc,
        const std::unordered_map<std::string, float>* param_overrides,
        const std::unordered_map<std::string, std::string>* string_overrides,
        const std::filesystem::path& graph_base_dir);
};

} // namespace vivid
