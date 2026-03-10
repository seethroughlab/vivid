#pragma once

#include "operator_api/create_request.h"
#include "operator_api/types.h"
#include <string>
#include <vector>

namespace vivid {

class OperatorRegistry;

// Legacy alias — kept for backward compat with CLI --outputs parsing in main.cpp
using OutputPortSpec = VividPortSpec;
using ParamSpec      = VividParamSpec;
using CreateOperatorRequest = VividCreateOperatorRequest;

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

    // Full-featured create from a CreateOperatorRequest.
    // src_dir is the project root (parent of operators/).
    static CreateOperatorResult create(const VividCreateOperatorRequest& request,
                                       const std::string& src_dir,
                                       bool package_layout = false);

    // Legacy overload — builds a request and delegates.
    static CreateOperatorResult create(const std::string& name, VividDomain domain,
                                       const std::string& src_dir,
                                       const std::string& variant = "",
                                       bool package_layout = false,
                                       const std::vector<VividPortSpec>& extra_outputs = {});

    // Open file in $VISUAL/$EDITOR/open (async, non-blocking).
    static void open_in_editor(const std::string& path);
};

} // namespace vivid
