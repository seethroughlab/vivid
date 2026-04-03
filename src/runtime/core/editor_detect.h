#pragma once
#include <string>
#include <vector>

namespace vivid {

struct DetectedEditor {
    std::string name;    // display name, e.g. "Visual Studio Code"
    std::string app_id;  // app name for `open -a`, empty = system default
};

// Scans /Applications for known editors.
// Always includes "System Default" first and "Custom Command..." last.
std::vector<DetectedEditor> detect_editors();

} // namespace vivid
