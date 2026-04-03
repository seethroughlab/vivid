#pragma once

#include <string>

namespace vivid {

// Runtime-only bridge: uses Objective-C runtime lookup so Vivid can compile
// without Sparkle headers; release builds can embed Sparkle.framework.
class SparkleBridge {
public:
    static bool available();
    static bool check_for_updates(std::string* error = nullptr);
};

}  // namespace vivid

