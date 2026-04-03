#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>

namespace vivid {

class Graph;
class RuntimeCore;
class OperatorRegistry;

namespace control_server_checks {

std::string handle_run_diagnostics(Graph& graph, RuntimeCore& core, OperatorRegistry& registry);
std::string handle_validate_checks(const nlohmann::json& root);
std::string handle_run_checks(Graph& graph, RuntimeCore& core, OperatorRegistry& registry,
                              const nlohmann::json& root);

} // namespace control_server_checks
} // namespace vivid
