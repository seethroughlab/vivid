# PLAN-08: Addon System Architecture ✅ IMPLEMENTED

A modular addon system that enables community-contributed extensions (Spout, Syphon, NDI, etc.) with fast hot-reload and zero configuration.

**Status: COMPLETE** — All addons migrated to new architecture (December 2024)

## Overview

The addon system solves two key problems:
1. **Slow hot-reload** — Previously, addons recompiled on every source change
2. **Complex configuration** — Users had to write custom CMakeLists.txt to use addons

The solution: **Pre-built static libraries with automatic detection**.

```
┌─────────────────────────────────────────────────────────────────┐
│                    ADDON SYSTEM ARCHITECTURE                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Build Time (once)                    Hot-Reload (every edit)   │
│  ┌─────────────────┐                  ┌─────────────────┐       │
│  │ vivid-spout/    │  cmake build     │ chain.cpp       │       │
│  │   src/*.cpp     │ ──────────────▶  │   #include      │       │
│  │   addon.json    │                  │   <vivid/spout/ │       │
│  └────────┬────────┘                  │    spout.h>     │       │
│           │                           └────────┬────────┘       │
│           ▼                                    │                │
│  ┌─────────────────┐                           │ scan           │
│  │ vivid-spout.lib │                           ▼                │
│  │ (static library)│                  ┌─────────────────┐       │
│  └────────┬────────┘                  │ AddonRegistry   │       │
│           │                           │ - detect headers│       │
│           │                           │ - match addons  │       │
│           │                           └────────┬────────┘       │
│           │                                    │                │
│           └──────────────┬─────────────────────┘                │
│                          │                                      │
│                          ▼                                      │
│                 ┌─────────────────┐                             │
│                 │ Auto-Generated  │                             │
│                 │ CMakeLists.txt  │                             │
│                 │ + addon linkage │                             │
│                 └────────┬────────┘                             │
│                          │                                      │
│                          ▼                                      │
│                 ┌─────────────────┐                             │
│                 │ operators.dll   │  Fast! Only user code       │
│                 │ (user code only)│  compiles on hot-reload     │
│                 └─────────────────┘                             │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Design Goals

1. **Zero Configuration** — Just `#include <vivid/spout/spout.h>` and it works
2. **Fast Hot-Reload** — Only user code recompiles, addons are pre-built
3. **Community Extensible** — Anyone can create and share addons
4. **Platform Aware** — Addons specify which platforms they support
5. **Discoverable** — Clear documentation and error messages

---

## Addon Directory Structure

Each addon lives in `addons/vivid-{name}/`:

```
addons/
├── CMakeLists.txt              # Master addon builder
├── vivid-spout/                # Windows texture sharing
│   ├── include/
│   │   └── vivid/spout/
│   │       └── spout.h         # Public API header
│   ├── src/
│   │   ├── spout_sender.cpp    # Implementation
│   │   └── spout_receiver.cpp
│   ├── addon.json              # Addon metadata
│   └── CMakeLists.txt          # Addon build script
├── vivid-syphon/               # macOS texture sharing
│   ├── include/
│   │   └── vivid/syphon/
│   │       └── syphon.h
│   ├── src/
│   │   ├── syphon_sender.mm
│   │   └── syphon_receiver.mm
│   ├── addon.json
│   └── CMakeLists.txt
└── vivid-ndi/                  # Cross-platform NDI
    ├── include/
    │   └── vivid/ndi/
    │       └── ndi.h
    ├── src/
    │   └── ndi.cpp
    ├── addon.json
    └── CMakeLists.txt
```

### Build Output

After `cmake --build`, addons are installed to the build directory:

```
build/
├── addons/
│   ├── include/                # All addon headers
│   │   └── vivid/
│   │       ├── spout/
│   │       │   └── spout.h
│   │       └── syphon/
│   │           └── syphon.h
│   ├── lib/                    # Pre-built static libraries
│   │   ├── vivid-spout.lib     # Windows
│   │   └── libvivid-syphon.a   # macOS
│   └── addons.json             # Combined addon registry
├── bin/
│   └── vivid.exe
└── ...
```

---

## Addon Metadata (addon.json)

Each addon describes itself with `addon.json`:

```json
{
  "name": "spout",
  "version": "1.0.0",
  "description": "Spout texture sharing for Windows",
  "author": "Vivid Contributors",
  "license": "MIT",

  "platforms": ["windows"],

  "detect_headers": [
    "vivid/spout/spout.h"
  ],

  "include_dirs": ["include"],

  "libraries": {
    "windows": {
      "static": ["lib/vivid-spout.lib"],
      "system": ["opengl32.lib"]
    }
  },

  "dependencies": {
    "external": {
      "spout2": {
        "git": "https://github.com/leadedge/Spout2.git",
        "tag": "2.007j"
      }
    }
  }
}
```

### Schema Reference

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes | Addon identifier (lowercase, no prefix) |
| `version` | string | Yes | Semantic version |
| `description` | string | Yes | Short description |
| `author` | string | No | Author name/email |
| `license` | string | Yes | SPDX license identifier |
| `platforms` | string[] | Yes | Supported platforms: `windows`, `macos`, `linux` |
| `detect_headers` | string[] | Yes | Headers that trigger auto-detection |
| `include_dirs` | string[] | Yes | Relative paths to include directories |
| `libraries` | object | Yes | Platform-specific library configuration |
| `libraries.{platform}.static` | string[] | Yes | Static libraries to link |
| `libraries.{platform}.system` | string[] | No | System libraries to link |
| `libraries.{platform}.frameworks` | string[] | No | macOS frameworks to link |
| `dependencies.external` | object | No | External dependencies to fetch |

---

## Addon CMakeLists.txt

Each addon has a `CMakeLists.txt` that builds the static library:

```cmake
# addons/vivid-spout/CMakeLists.txt

# Only build on Windows
if(NOT WIN32)
    message(STATUS "Skipping vivid-spout (Windows only)")
    return()
endif()

# Fetch external dependency
include(FetchContent)
FetchContent_Declare(
    spout2
    GIT_REPOSITORY https://github.com/leadedge/Spout2.git
    GIT_TAG 2.007j
)
FetchContent_MakeAvailable(spout2)

# Build the addon as a static library
add_library(vivid-spout STATIC
    src/spout_sender.cpp
    src/spout_receiver.cpp
)

target_include_directories(vivid-spout
    PUBLIC include
    PRIVATE ${spout2_SOURCE_DIR}/SPOUTSDK/SpoutLibrary
)

target_link_libraries(vivid-spout
    PRIVATE SpoutLibrary
    PUBLIC opengl32
)

# Install to build/addons/
install(TARGETS vivid-spout
    ARCHIVE DESTINATION addons/lib
)

install(DIRECTORY include/
    DESTINATION addons/include
)

install(FILES addon.json
    DESTINATION addons/meta
    RENAME spout.json
)
```

### Master Addon CMakeLists.txt

The root `addons/CMakeLists.txt` builds all addons:

```cmake
# addons/CMakeLists.txt

# Collect all addon subdirectories
file(GLOB ADDON_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/vivid-*")

foreach(ADDON_DIR ${ADDON_DIRS})
    if(IS_DIRECTORY ${ADDON_DIR})
        get_filename_component(ADDON_NAME ${ADDON_DIR} NAME)
        message(STATUS "Configuring addon: ${ADDON_NAME}")
        add_subdirectory(${ADDON_DIR})
    endif()
endforeach()

# Generate combined addons.json registry
# (done via custom command after all addons configure)
```

---

## Runtime Addon Detection

The compiler detects which addons a project needs by scanning source files.

### AddonRegistry Class

```cpp
// runtime/src/addon_registry.h
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace vivid {

struct AddonInfo {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> platforms;
    std::vector<std::string> detectHeaders;
    std::vector<std::string> includeDirs;
    std::vector<std::string> staticLibs;
    std::vector<std::string> systemLibs;
    std::vector<std::string> frameworks;  // macOS only
};

class AddonRegistry {
public:
    // Load all addon.json files from addons directory
    void loadFromDirectory(const std::filesystem::path& addonsDir);

    // Scan source file for #include directives
    std::vector<std::string> scanSourceForAddons(
        const std::filesystem::path& sourcePath
    ) const;

    // Get addon info by name
    const AddonInfo* getAddon(const std::string& name) const;

    // Get all addons for current platform
    std::vector<const AddonInfo*> getAvailableAddons() const;

    // Check if addon is available on current platform
    bool isAvailable(const std::string& name) const;

private:
    std::unordered_map<std::string, AddonInfo> addons_;
    std::unordered_map<std::string, std::string> headerToAddon_;

    void parseAddonJson(const std::filesystem::path& jsonPath);
    static std::string getCurrentPlatform();
};

} // namespace vivid
```

### Header Scanning Algorithm

```cpp
std::vector<std::string> AddonRegistry::scanSourceForAddons(
    const std::filesystem::path& sourcePath
) const {
    std::vector<std::string> requiredAddons;
    std::ifstream file(sourcePath);
    std::string line;

    // Regex to match #include <vivid/...> directives
    std::regex includeRegex(R"(#include\s*<(vivid/[^>]+)>)");

    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, includeRegex)) {
            std::string header = match[1].str();

            // Check if this header belongs to an addon
            auto it = headerToAddon_.find(header);
            if (it != headerToAddon_.end()) {
                const std::string& addonName = it->second;

                // Check platform availability
                if (isAvailable(addonName)) {
                    if (std::find(requiredAddons.begin(),
                                  requiredAddons.end(),
                                  addonName) == requiredAddons.end()) {
                        requiredAddons.push_back(addonName);
                    }
                } else {
                    std::cerr << "[Addon] Warning: " << addonName
                              << " is not available on this platform\n";
                }
            }
        }
    }

    return requiredAddons;
}
```

---

## CMakeLists.txt Generation

The compiler generates CMakeLists.txt with addon support:

### Template

```cmake
# Auto-generated CMakeLists.txt for Vivid project
cmake_minimum_required(VERSION 3.20)
project(vivid_operators)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Vivid paths (passed by runtime)
set(VIVID_INCLUDE_DIR "" CACHE PATH "Vivid include directory")
set(VIVID_ADDONS_DIR "" CACHE PATH "Vivid addons directory")
set(VIVID_LIBRARY "" CACHE FILEPATH "Vivid import library")

# GLM for math
include(FetchContent)
find_package(glm CONFIG QUIET)
if(NOT glm_FOUND)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

# Build operators shared library
add_library(operators SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/chain.cpp"
)

target_include_directories(operators PRIVATE
    ${VIVID_INCLUDE_DIR}
    ${VIVID_ADDONS_DIR}/include
)

target_link_libraries(operators PRIVATE
    glm::glm
)

# Windows: Link against vivid.lib for symbol resolution
if(WIN32 AND VIVID_LIBRARY)
    target_link_libraries(operators PRIVATE ${VIVID_LIBRARY})
endif()

# === AUTO-DETECTED ADDONS ===
# Generated based on #include directives in source files

# Addon: spout
target_link_libraries(operators PRIVATE
    ${VIVID_ADDONS_DIR}/lib/vivid-spout.lib
    opengl32.lib
)

# === END ADDONS ===

set_target_properties(operators PROPERTIES
    OUTPUT_NAME "operators"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(APPLE)
    target_link_options(operators PRIVATE -undefined dynamic_lookup)
endif()
```

### Generation Logic

```cpp
// In compiler.cpp

std::string Compiler::generateCMakeLists(
    const std::filesystem::path& projectPath,
    const std::vector<std::string>& requiredAddons
) {
    std::ostringstream cmake;

    // Write header and standard configuration
    cmake << "# Auto-generated CMakeLists.txt\n";
    cmake << "cmake_minimum_required(VERSION 3.20)\n";
    // ... standard boilerplate ...

    // Add addon section
    cmake << "\n# === AUTO-DETECTED ADDONS ===\n";
    cmake << "# Generated based on #include directives\n\n";

    for (const auto& addonName : requiredAddons) {
        const AddonInfo* addon = addonRegistry_.getAddon(addonName);
        if (!addon) continue;

        cmake << "# Addon: " << addonName << "\n";
        cmake << "target_link_libraries(operators PRIVATE\n";

        // Static libraries
        for (const auto& lib : addon->staticLibs) {
            cmake << "    ${VIVID_ADDONS_DIR}/" << lib << "\n";
        }

        // System libraries
        for (const auto& lib : addon->systemLibs) {
            cmake << "    " << lib << "\n";
        }

        cmake << ")\n\n";

        // macOS frameworks
        if (!addon->frameworks.empty()) {
            cmake << "if(APPLE)\n";
            cmake << "    target_link_libraries(operators PRIVATE\n";
            for (const auto& fw : addon->frameworks) {
                cmake << "        \"-framework " << fw << "\"\n";
            }
            cmake << "    )\n";
            cmake << "endif()\n\n";
        }
    }

    cmake << "# === END ADDONS ===\n";

    return cmake.str();
}
```

---

## User Experience

### Before (Current System)

```cpp
// chain.cpp
#include <vivid/vivid.h>
#include <vivid/spout/spout.h>  // Want to use Spout

// User must also create:
// - CMakeLists.txt with addon include
// - Know the correct paths
// - Build takes longer (recompiles addon every time)
```

### After (New System)

```cpp
// chain.cpp
#include <vivid/vivid.h>
#include <vivid/spout/spout.h>  // Just include and use!

// That's it! No CMakeLists.txt needed.
// Addon is auto-detected and linked.
// Fast hot-reload (only chain.cpp recompiles).
```

### Error Messages

When an addon is not available:

```
[Addon] Error: 'spout' is not available on this platform (macOS)
        Spout is Windows-only. Use 'syphon' on macOS instead.

        Replace:
          #include <vivid/spout/spout.h>
        With:
          #include <vivid/syphon/syphon.h>
```

When an addon is not built:

```
[Addon] Error: 'spout' addon library not found
        Expected: build/addons/lib/vivid-spout.lib

        Rebuild Vivid with addons:
          cmake --build build --target vivid-spout
```

---

## Integration with Main Build

Update root `CMakeLists.txt`:

```cmake
# Near the end of CMakeLists.txt

# Build addons
option(VIVID_BUILD_ADDONS "Build addon libraries" ON)
if(VIVID_BUILD_ADDONS)
    add_subdirectory(addons)
endif()
```

---

## Creating a New Addon

### Step 1: Create Directory Structure

```bash
mkdir -p addons/vivid-myeffect/include/vivid/myeffect
mkdir -p addons/vivid-myeffect/src
```

### Step 2: Create addon.json

```json
{
  "name": "myeffect",
  "version": "1.0.0",
  "description": "My custom effect addon",
  "platforms": ["windows", "macos", "linux"],
  "detect_headers": ["vivid/myeffect/myeffect.h"],
  "include_dirs": ["include"],
  "libraries": {
    "windows": { "static": ["lib/vivid-myeffect.lib"] },
    "macos": { "static": ["lib/libvivid-myeffect.a"] },
    "linux": { "static": ["lib/libvivid-myeffect.a"] }
  }
}
```

### Step 3: Create Public Header

```cpp
// addons/vivid-myeffect/include/vivid/myeffect/myeffect.h
#pragma once

namespace vivid {
namespace myeffect {

class MyEffect {
public:
    void doSomething();
};

} // namespace myeffect
} // namespace vivid
```

### Step 4: Create Implementation

```cpp
// addons/vivid-myeffect/src/myeffect.cpp
#include <vivid/myeffect/myeffect.h>

namespace vivid {
namespace myeffect {

void MyEffect::doSomething() {
    // Implementation
}

} // namespace myeffect
} // namespace vivid
```

### Step 5: Create CMakeLists.txt

```cmake
# addons/vivid-myeffect/CMakeLists.txt

add_library(vivid-myeffect STATIC
    src/myeffect.cpp
)

target_include_directories(vivid-myeffect PUBLIC include)

install(TARGETS vivid-myeffect ARCHIVE DESTINATION addons/lib)
install(DIRECTORY include/ DESTINATION addons/include)
install(FILES addon.json DESTINATION addons/meta RENAME myeffect.json)
```

### Step 6: Build and Use

```bash
cmake --build build
```

```cpp
// In any chain.cpp
#include <vivid/myeffect/myeffect.h>

void setup(Chain& chain) {
    myeffect::MyEffect effect;
    effect.doSomething();
}
```

---

## Migration Status ✅ COMPLETE

All phases completed as of December 2024.

### Phase 1: Update Spout Addon ✅
- [x] Created `addon.json` with proper schema
- [x] Updated `CMakeLists.txt` to build static library
- [x] Removed old `addon.cmake`
- [x] Tested with spout-in/spout-out examples

### Phase 2: Implement AddonRegistry ✅
- [x] Created `runtime/src/addon_registry.h` and `.cpp`
- [x] Added JSON parsing for addon metadata
- [x] Implemented header scanning (`scanSourceForAddons`)
- [x] Platform-aware addon filtering

### Phase 3: Update Compiler ✅
- [x] Integrated AddonRegistry into Compiler
- [x] Template-based CMakeLists generation with addon linkage
- [x] Removed need for custom CMakeLists in examples
- [x] Hot-reload performance verified (only user code recompiles)

### Phase 4: Migrate All Addons ✅
- [x] vivid-spout (Windows texture sharing)
- [x] vivid-syphon (macOS texture sharing)
- [x] vivid-models (3D model loading via Assimp)
- [x] vivid-storage (JSON key/value persistence)
- [x] vivid-nuklear (Nuklear GUI integration)
- [x] vivid-csg (CSG boolean operations via Manifold)

### Phase 5: Documentation ✅
- [x] Documented addon creation process (this file)
- [x] Example addon structure in each addon directory

---

## Current Addons

| Addon | Platform | Description | Status |
|-------|----------|-------------|--------|
| vivid-spout | Windows | Spout texture sharing | ✅ Working |
| vivid-syphon | macOS | Syphon texture sharing | ✅ Working |
| vivid-models | All | 3D model loading (Assimp) | ✅ Working |
| vivid-storage | All | JSON key/value storage | ✅ Working |
| vivid-nuklear | All | Nuklear GUI integration | ✅ Working |
| vivid-csg | All | CSG boolean operations | ✅ Working |
| vivid-imgui | All | ImGUI integration | ⏳ Blocked (WebGPU API mismatch) |

---

## Future Enhancements

1. **Addon Package Manager** — Download addons from registry
2. **Addon Versioning** — Specify addon version requirements
3. **Addon Dependencies** — Addons can depend on other addons
4. **Binary Distribution** — Pre-built addon binaries for common platforms
5. **Addon Templates** — `vivid new-addon myeffect` command

---

## References

- [CMake Static Libraries](https://cmake.org/cmake/help/latest/command/add_library.html)
- [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing
- [Spout2 SDK](https://github.com/leadedge/Spout2)
- [Syphon Framework](https://github.com/Syphon/Syphon-Framework)
