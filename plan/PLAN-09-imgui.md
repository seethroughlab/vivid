# ImGUI Integration - COMPLETE

The vivid-imgui addon provides Dear ImGui integration for Vivid operators, enabling immediate-mode GUI for parameter tweaking, debugging, and custom control panels.

## Status: Working

The addon successfully builds and runs with wgpu-native v24.0.0.2 using the latest ImGui master branch.

## Implementation Details

### Key Changes
1. **ImGui master branch required** - The v1.91.6 release does not have the updated WebGPU backend. The master branch includes fixes for WGPUStringView, WGPUShaderSourceWGSL, and other API changes.

2. **Objective-C++ on macOS** - The `imgui_impl_wgpu.cpp` backend requires Cocoa/CAMetalLayer headers on macOS, so it must be compiled with `-x objective-c++` flags.

3. **IMGUI_IMPL_WEBGPU_BACKEND_WGPU define** - Must be defined to use the wgpu-native backend (vs Dawn).

### Files Created/Modified
- `addons/vivid-imgui/CMakeLists.txt` - Fetches ImGui master, builds as static lib
- `addons/vivid-imgui/include/vivid/imgui/imgui_integration.h` - Public API
- `addons/vivid-imgui/src/imgui_integration.cpp` - Implementation (pre-existing)
- `addons/vivid-imgui/addon.json` - Addon metadata for auto-detection
- `examples/imgui-demo/chain.cpp` - Example demonstrating ImGui usage

## Usage

```cpp
#include <vivid/vivid.h>
#include <vivid/imgui/imgui_integration.h>
#include <imgui.h>

using namespace vivid;

static bool guiInitialized = false;
static Texture guiTexture;
static float myValue = 0.5f;

void setup(Chain& chain) {
    chain.add<Noise>("noise").scale(4.0f);
    chain.add<Composite>("output").a("noise").b("gui");
    chain.setOutput("output");
}

void update(Chain& chain, Context& ctx) {
    // Initialize on first frame
    if (!guiInitialized) {
        guiTexture = ctx.createTexture();
        vivid::imgui::init(ctx);
        guiInitialized = true;
    }

    // Begin frame
    vivid::imgui::beginFrame(ctx);

    // Draw ImGui widgets
    ImGui::Begin("Controls");
    if (ImGui::SliderFloat("Scale", &myValue, 0.0f, 10.0f)) {
        chain.get<Noise>("noise").scale(myValue);
    }
    ImGui::End();

    // Render to texture and make available to chain
    vivid::imgui::render(ctx, guiTexture, {0, 0, 0, 0});
    ctx.setOutput("gui", guiTexture);
}

VIVID_CHAIN(setup, update)
```

## API Reference

### `vivid::imgui::init(Context& ctx)`
Initialize ImGui with the Vivid WebGPU context. Call once before using any other imgui functions.

### `vivid::imgui::shutdown()`
Shutdown ImGui and release resources.

### `vivid::imgui::beginFrame(Context& ctx)`
Begin a new ImGui frame. Call at the start of update() before any ImGui:: calls.

### `vivid::imgui::render(Context& ctx, Texture& output, glm::vec4 clearColor)`
Render ImGui to a texture. Use `{0, 0, 0, 0}` for transparent overlay.

### `vivid::imgui::wantsMouse()` / `vivid::imgui::wantsKeyboard()`
Check if ImGui wants to capture input.

## Known Issues

1. **Syphon addon disabled** - The Syphon addon requires OBJCXX language support which conflicts with GLFW's Objective-C compilation on newer Xcode versions. This is unrelated to ImGui.

## Build Requirements

- CMake 3.20+
- C++20 compiler
- wgpu-native v24.0.0.2
- Xcode (macOS) for Objective-C++ support
