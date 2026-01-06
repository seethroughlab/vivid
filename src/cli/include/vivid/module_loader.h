// Module Loader - shared dynamic library loading for CLI and MCP
// Loads built-in addons and user-installed modules into OperatorRegistry

#pragma once

#include <filesystem>

namespace vivid {

/// Load a single module library (dylib/dll/so)
void loadModuleLibrary(const std::filesystem::path& libPath);

/// Load all modules: built-in addons from executable directory
/// and user-installed modules from ~/.vivid/modules/
void loadAllModules();

} // namespace vivid
