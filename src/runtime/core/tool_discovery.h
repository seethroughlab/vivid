#pragma once

#include <string>

namespace vivid {

// Resolve the path for an external tool (clang++, cmake, git).
// Checks VIVID_CXX / VIVID_CMAKE / VIVID_GIT env var overrides first,
// then falls back to `command -v` lookup. Returns empty string if not found.
std::string find_tool(const char* tool);

// Return a user-friendly error message for a missing tool, including
// the relevant VIVID_* env var override hint.
std::string missing_tool_error(const char* tool);

} // namespace vivid
