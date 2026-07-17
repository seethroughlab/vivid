#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace vivid {

class OpRegistry;
class OperatorLoader;

// Load one .dylib operator: validate it, register it by its descriptor name (with
// discovery metadata) into `reg`, and move the loader into `loaders`. Returns the
// registered op name, or "" on failure / when the name is already registered
// (built-ins + first-loaded win). Shared by the startup scan and package install.
// ADR-0018: if `quarantined` contains the op's name it is NOT registered (a repeat crasher is
// disabled by default; its persisted nodes then load as op_missing()).
std::string load_and_register_operator(const std::string& dylib_path, OpRegistry& reg,
                                       std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                                       const std::set<std::string>* quarantined = nullptr);

// Scan `dir` for *.dylib operators and load_and_register_operator each. Returns the
// number newly registered.
int scan_operator_dir(const std::string& dir, OpRegistry& reg,
                      std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                      const std::set<std::string>* quarantined = nullptr);

}  // namespace vivid
