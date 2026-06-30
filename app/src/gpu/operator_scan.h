#pragma once

#include <memory>
#include <string>
#include <vector>

namespace vivid {

class OpRegistry;
class OperatorLoader;

// Scan `dir` for *.dylib operators: load + validate each, register it by its
// descriptor name into `reg`, and move the loader into `loaders` (which must
// outlive any use of `reg`). Returns the number newly registered. A dylib whose
// name is already registered (e.g. a built-in) is skipped — built-ins win.
int scan_operator_dir(const std::string& dir, OpRegistry& reg,
                      std::vector<std::unique_ptr<OperatorLoader>>& loaders);

}  // namespace vivid
