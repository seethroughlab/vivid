#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace vivid {

class AudioEngine;
class ControlServer;
class Graph;
class GpuContext;
class PackageCatalog;
class RuntimeCore;
class OperatorRegistry;

namespace control_server_checks {

std::string handle_run_diagnostics(Graph& graph, RuntimeCore& core, OperatorRegistry& registry,
                                   AudioEngine* audio_engine,
                                   GpuContext* gpu_context = nullptr,
                                   PackageCatalog* package_catalog = nullptr,
                                   const ControlServer* control_server = nullptr);
std::string handle_get_runtime_health(Graph& graph, RuntimeCore& core, OperatorRegistry& registry,
                                      AudioEngine* audio_engine,
                                      GpuContext* gpu_context,
                                      PackageCatalog* package_catalog = nullptr,
                                      const ControlServer* control_server = nullptr);
std::string handle_validate_checks(const nlohmann::json& root);
std::string handle_run_checks(Graph& graph, RuntimeCore& core, OperatorRegistry& registry,
                              const nlohmann::json& root);

} // namespace control_server_checks
} // namespace vivid
