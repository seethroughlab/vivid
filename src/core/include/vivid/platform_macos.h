// Platform-specific helpers for macOS
// Provides autoreleasepool wrapper for Metal/WebGPU frame loop

#pragma once

#include <functional>

namespace vivid {
namespace platform {

// Execute a function inside an @autoreleasepool block
// This is critical on macOS to prevent memory leaks with Metal
// Call this once per frame to ensure autoreleased objects are released
void withAutoreleasePool(const std::function<void()>& fn);

} // namespace platform
} // namespace vivid
