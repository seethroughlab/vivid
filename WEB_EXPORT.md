# Vivid Web Export Plan

Export vivid projects to WebAssembly for browser deployment.

## Requirements

- No hot-reload (static WASM builds)
- Support: Core 2D effects + vivid-render3d
- Output: Static site folder (index.html + .wasm + .js)

## Architecture

```
Native:  chain.cpp -> clang++ -> chain.dylib (hot-reloaded via dlopen)
         vivid runtime -> wgpu-native -> Metal/Vulkan/D3D12

Web:     chain.cpp + vivid runtime -> emcc -> vivid.wasm + vivid.js
         emdawnwebgpu -> browser WebGPU API
         Emscripten GLFW3 -> HTML5 canvas
```

## Output Structure

```
MyProject/
  index.html          # HTML page with canvas
  MyProject.js        # Emscripten JS glue code
  MyProject.wasm      # Compiled WebAssembly
  MyProject.data      # Preloaded assets (shaders, images)
```

---

## Implementation Phases

### Phase 1: CMake Emscripten Support

**File: `CMakeLists.txt` (root)**

1. Add Emscripten detection at top:
```cmake
if(EMSCRIPTEN)
    message(STATUS "Building for Web (Emscripten)")
    set(VIVID_WEB_BUILD ON)
    set(VIVID_BUILD_VIDEO OFF)
    set(VIVID_BUILD_AUDIO OFF)
endif()
```

2. Skip GLFW FetchContent for Emscripten (uses its own):
```cmake
if(NOT EMSCRIPTEN)
    FetchContent_Declare(glfw ...)
endif()
```

3. Skip wgpu-native download for Emscripten:
```cmake
if(EMSCRIPTEN)
    add_library(webgpu INTERFACE)  # Stub - emdawnwebgpu via link flags
else()
    # ... existing wgpu-native download ...
endif()
```

**File: `core/CMakeLists.txt`**

1. Build vivid-core as STATIC for web:
```cmake
if(EMSCRIPTEN)
    add_library(vivid-core STATIC ${VIVID_CORE_LIB_SOURCES})
    # Exclude network operators (no socket support)
else()
    add_library(vivid-core SHARED ${VIVID_CORE_LIB_SOURCES})
endif()
```

2. Add web-specific executable target:
```cmake
if(EMSCRIPTEN AND VIVID_WEB_CHAIN_CPP)
    add_executable(vivid-web
        ${VIVID_CORE_SOURCES}
        ${VIVID_CORE_LIB_SOURCES}
        ${IMGUI_SOURCES}
        ${VIVID_WEB_CHAIN_CPP}
    )
    target_link_options(vivid-web PRIVATE
        -sUSE_WEBGPU=1
        -sUSE_GLFW=3
        -sASYNCIFY=1
        -sALLOW_MEMORY_GROWTH=1
        --preload-file ${CMAKE_SOURCE_DIR}/core/shaders@/shaders
        --preload-file ${VIVID_WEB_ASSETS}@/assets
    )
    set_target_properties(vivid-web PROPERTIES SUFFIX ".html")
endif()
```

### Phase 2: Main Loop Refactoring

**File: `core/src/main.cpp`**

1. Add Emscripten includes:
```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif
```

2. Extract loop body into function:
```cpp
struct MainLoopContext {
    GLFWwindow* window;
    WGPUDevice device;
    // ... other state ...
};

void mainLoopIteration(void* userData) {
    MainLoopContext* mlc = static_cast<MainLoopContext*>(userData);
    // ... existing loop body ...
}
```

3. Use emscripten_set_main_loop:
```cpp
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(mainLoopIteration, &mlc, 0, true);
#else
    while (!glfwWindowShouldClose(window)) {
        mainLoopIteration(&mlc);
    }
#endif
```

4. Guard wgpuDevicePoll (not needed on web):
```cpp
#ifndef __EMSCRIPTEN__
    wgpuDevicePoll(device, false, nullptr);
#endif
```

### Phase 3: Hot Reload Stub

**File: `core/src/hot_reload.cpp`**

Add Emscripten stub at top:
```cpp
#ifdef __EMSCRIPTEN__
// No hot-reload on web - chain is statically compiled
namespace vivid {
bool HotReload::compile() { return false; }
bool HotReload::load() { return false; }
void HotReload::unload() {}
// ... other stubs ...
}
#else
// ... existing implementation ...
#endif
```

### Phase 4: Bundle Command

**File: `core/src/cli.cpp`**

Implement `bundleForWeb()`:

```cpp
int bundleForWeb(const fs::path& srcProject, const fs::path& chainPath,
                 const std::string& appName, const fs::path& outputDir) {
    // 1. Check EMSDK environment
    const char* emsdkEnv = std::getenv("EMSDK");
    if (!emsdkEnv) {
        std::cerr << "Error: EMSDK not set. Install Emscripten first.\n";
        std::cerr << "  brew install emscripten  # macOS\n";
        std::cerr << "  Then: source /path/to/emsdk_env.sh\n";
        return 1;
    }

    // 2. Create temp build directory
    fs::path tempBuild = fs::temp_directory_path() / ("vivid_web_" + appName);
    fs::create_directories(tempBuild);

    // 3. Run emcmake cmake
    std::stringstream cmd;
    cmd << "emcmake cmake -S " << getVividRoot() << " -B " << tempBuild
        << " -DVIVID_WEB_CHAIN_CPP=" << chainPath
        << " -DVIVID_WEB_ASSETS=" << (srcProject / "assets");
    executeCommand(cmd.str());

    // 4. Build
    executeCommand("cmake --build " + tempBuild.string());

    // 5. Copy output to bundle directory
    fs::path webOutput = outputDir / appName;
    fs::create_directories(webOutput);
    fs::copy(tempBuild / "bin/vivid-web.html", webOutput / "index.html");
    fs::copy(tempBuild / "bin/vivid-web.js", webOutput / (appName + ".js"));
    fs::copy(tempBuild / "bin/vivid-web.wasm", webOutput / (appName + ".wasm"));
    fs::copy(tempBuild / "bin/vivid-web.data", webOutput / (appName + ".data"));

    // 6. Print instructions
    std::cout << "\nWeb bundle created: " << webOutput << "\n";
    std::cout << "To test locally:\n";
    std::cout << "  cd " << webOutput << "\n";
    std::cout << "  python3 -m http.server 8080\n";
    std::cout << "  # Open http://localhost:8080 in Chrome/Firefox\n";

    return 0;
}
```

### Phase 5: Render3D Support

**File: `addons/vivid-render3d/CMakeLists.txt`**

```cmake
if(EMSCRIPTEN)
    add_library(vivid-render3d STATIC ${RENDER3D_SOURCES})
else()
    add_library(vivid-render3d SHARED ${RENDER3D_SOURCES})
endif()
```

Add shader preloading in core CMakeLists.txt for web builds.

---

## Critical Files Summary

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add Emscripten detection, skip GLFW/wgpu-native |
| `core/CMakeLists.txt` | Add vivid-web target with link flags |
| `core/src/main.cpp` | Refactor loop for emscripten_set_main_loop |
| `core/src/hot_reload.cpp` | Stub out for Emscripten |
| `core/src/cli.cpp` | Implement bundleForWeb() |
| `addons/vivid-render3d/CMakeLists.txt` | Static library for web |

## Features Excluded from Web

| Feature | Reason |
|---------|--------|
| Hot-reload | No dlopen in browser |
| Network operators | No raw sockets (could add WebSocket later) |
| vivid-video | Platform codecs not available |
| vivid-audio | miniaudio (could add Web Audio API later) |
| Video export | Platform APIs not available |

## Browser Requirements

- Chrome 113+ or Firefox 121+ (WebGPU support)
- HTTPS or localhost (WebGPU security requirement)
- Hardware: GPU with WebGPU driver support

## Usage

```bash
# Install Emscripten (one-time setup)
brew install emscripten  # macOS
# or: git clone https://github.com/emscripten-core/emsdk.git && ./emsdk install latest

# Source the environment
source /path/to/emsdk_env.sh

# Bundle for web
./build/bin/vivid bundle examples/getting-started/02-hello-noise --platform web

# Test locally
cd ~/vivid-bundles/02-hello-noise
python3 -m http.server 8080
# Open http://localhost:8080 in browser
```

## Future Enhancements

1. **Web Audio API** - Add vivid-audio support via Emscripten's audio worklet
2. **WebSocket networking** - Enable NDI-like streaming over WebSocket
3. **PWA support** - Add manifest.json and service worker for offline support
4. **Size optimization** - Use `-Os` and closure compiler for smaller bundles
