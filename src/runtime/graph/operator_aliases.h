#pragma once

#include <string>
#include <unordered_map>

namespace vivid {

// Translates a legacy operator type id to its current id, mutating the
// node's param maps in place to inject the mode/topology/category param
// that selects the correct behavior on the merged operator.
//
// Rename rules in the alias table also rewrite legacy param keys to the
// new operator's param names so historical graphs load with identical
// behavior.
//
// Returns the resolved type id (or `raw_type` unchanged when no alias
// applies). Safe to call on every node during graph parse.
std::string resolve_operator_alias(
    const std::string& raw_type,
    std::unordered_map<std::string, float>& params,
    std::unordered_map<std::string, std::string>& string_params);

}  // namespace vivid
