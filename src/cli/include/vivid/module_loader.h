// Module Loader - shared dynamic library loading for CLI and MCP
// Loads built-in and user-installed modules into OperatorRegistry

#pragma once

#include <filesystem>

namespace vivid {

/// Load a single module library (dylib/dll/so)
void loadModuleLibrary(const std::filesystem::path& libPath);

/// Load all modules: built-in from executable directory
/// and user-installed from ~/.vivid/modules/
void loadAllModules();

} // namespace vivid
