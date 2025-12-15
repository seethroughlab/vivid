// Vivid CLI Commands
// Handles: vivid new, vivid --help, vivid --version

#pragma once

#include <string>

namespace vivid::cli {

// Version info
constexpr const char* VERSION = "3.0.0";

// Handle CLI commands before GPU initialization
// Returns: 0+ = handled (exit with this code), -1 = not a CLI command (continue to main)
int handleCommand(int argc, char** argv);

// Create a new project
int createProject(const std::string& name, const std::string& templateName, bool minimal, bool skipPrompts);

// Print help and version
void printUsage();
void printVersion();

} // namespace vivid::cli
