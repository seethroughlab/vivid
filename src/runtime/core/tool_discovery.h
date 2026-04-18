#pragma once

#include <string>

namespace vivid {

// Resolve the path for an external tool (clang++, cmake, git).
// Checks VIVID_CXX / VIVID_CMAKE / VIVID_GIT env var overrides first,
// then falls back to `command -v` lookup. Returns empty string if not found.
std::string find_tool(const char* tool);

// Resolve a usable C++ compiler path. Checks VIVID_CXX, then tries
// `clang++`, `c++`, `g++` in PATH / platform fallbacks. Empty on failure.
// The distinction vs. find_tool("clang++") is that multiple candidate names
// are tried — on Linux the user may only have g++; on Windows clang++ may
// not be the conventional choice.
std::string find_cxx_compiler();

// Return a user-friendly error message for a missing tool, including
// the relevant VIVID_* env var override hint. Known tools: "clang++",
// "cmake", "git", "ninja".
std::string missing_tool_error(const char* tool);

} // namespace vivid
