#pragma once

#include "operator_api/types.h"
#include <string>

namespace vivid {

class OperatorRegistry;

struct CreateOperatorResult {
    bool success;
    std::string error;        // empty on success
    std::string cpp_path;     // e.g. operators/audio/filter/filter.cpp
    std::string target_name;  // e.g. filter
};

class OperatorCreator {
public:
    // Validate name: returns empty string on success, error message on failure.
    // Checks: valid C++ identifier, no collision with existing types or filesystem.
    static std::string validate_name(const std::string& name, const OperatorRegistry& reg);

    // Create directory, write .cpp from template, patch CMakeLists.txt.
    // src_dir is the project root (parent of operators/).
    static CreateOperatorResult create(const std::string& name, VividDomain domain,
                                       const std::string& src_dir);

    // Open file in $VISUAL/$EDITOR/open (async, non-blocking).
    static void open_in_editor(const std::string& path);
};

} // namespace vivid
