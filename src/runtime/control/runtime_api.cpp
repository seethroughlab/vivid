#include "runtime/control/runtime_api.h"
#include "runtime/graph/graph.h"

#include <filesystem>

namespace vivid {

RuntimeAPI::RuntimeAPI(Graph& graph, RuntimeCore& core, AudioEngine& audio_engine,
                       OperatorRegistry& registry, SystemMidiListener* system_midi)
    : graph_(graph), core_(core), audio_engine_(audio_engine),
      registry_(registry), system_midi_(system_midi) {
    if (!graph_.source_path().empty()) {
        active_graph_source_path_ =
            std::filesystem::path(graph_.source_path()).lexically_normal().string();
    }
    capture_saved_snapshot();
}

bool RuntimeAPI::split_addr(const std::string& addr, std::string& node, std::string& port) {
    auto slash = addr.find('/');
    if (slash == std::string::npos) return false;
    node = addr.substr(0, slash);
    port = addr.substr(slash + 1);
    return !node.empty() && !port.empty();
}

} // namespace vivid
