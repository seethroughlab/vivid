// Vivid CLI Commands
// Handles all CLI parsing via CLI11

#pragma once

#include <vivid/app.h>
#include <string>
#include <optional>

namespace vivid::cli {

// Version info - injected from CMake via VIVID_VERSION define
#ifndef VIVID_VERSION
#define VIVID_VERSION "0.1.0"  // Fallback if not defined by CMake
#endif
constexpr const char* VERSION = VIVID_VERSION;

// Result of parsing CLI arguments
struct ParseResult {
    bool handled = false;          // True if a subcommand was handled (use exitCode)
    int exitCode = 0;              // Exit code if handled
    std::optional<AppConfig> config;  // Config if project should be run
};

// Parse all CLI arguments using CLI11
// Returns ParseResult indicating what to do next
ParseResult parseArgs(int argc, char** argv);

// Create a new project
int createProject(const std::string& name, const std::string& templateName, bool minimal, bool skipPrompts);

// Print help and version
void printUsage();
void printVersion();

} // namespace vivid::cli
