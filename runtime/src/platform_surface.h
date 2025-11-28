#pragma once
#include <webgpu/webgpu.h>

struct GLFWwindow;

namespace vivid {

// Platform-specific surface creation
// Returns nullptr on failure
WGPUSurface createSurfaceForWindow(WGPUInstance instance, GLFWwindow* window);

} // namespace vivid
