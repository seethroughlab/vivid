#pragma once

#include "ui/graph/graph_snapshot.h"
#include <string>

class OperatorInfoCache;

namespace vivid {

class Graph;
class RuntimeCore;
class AudioEngine;
class OperatorRegistry;
class SystemMidiListener;
class RuntimeAPI;
class CaptureCoordinator;
class ControlServer;
class SubgraphModuleRegistry;

vivid::ui::GraphSnapshot build_graph_snapshot(
    const Graph& graph,
    const RuntimeCore& runtime,
    AudioEngine* audio_engine,
    OperatorRegistry& registry,
    OperatorInfoCache& op_cache,
    SystemMidiListener* system_midi = nullptr,
    const RuntimeAPI* runtime_api = nullptr,
    CaptureCoordinator* capture_coordinator = nullptr,
    const ControlServer* control_server = nullptr,
    const SubgraphModuleRegistry* subgraph_modules = nullptr);

} // namespace vivid
