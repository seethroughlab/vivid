# macOS App Bundle for Vivid

## Context

Vivid currently builds as a plain executable (`build/vivid`) with ~60 operator plugin dylibs, a font file, a WebGPU runtime library, and demo graphs all loose in the build directory. This means launching requires the terminal, and there's no way to double-click it in Finder, pin it to the Dock, or find it in Spotlight. Building as a macOS `.app` bundle solves all of this.

## Status

All sections are **implemented**. The icon was recovered from the `legacy` branch (`modules/vivid-core/assets/icons/vivid.icns`).

## Bundle Structure

```
Vivid.app/
  Contents/
    Info.plist
    MacOS/
      vivid                    (main executable)
      *.dylib                  (built-in operator plugins)
      libwgpu_native.dylib     (WebGPU runtime)
    Resources/
      Vivid.icns               (app icon)
      JetBrainsMono-Regular.ttf
      *.json                   (demo graphs)
      filters/*.wgsl           (WGSL filter presets)
```

**Why plugins in MacOS/ not PlugIns/:** The operator registry scans `exe_dir` (the directory containing the executable) for `*.dylib` files. Placing plugins in `Contents/MacOS/` means the existing scan logic works unmodified — no C++ changes needed for plugin discovery.

## Implementation

### 1. CMakeLists.txt — Bundle configuration (done)

The `VIVID_BUNDLE` option (default OFF) preserves the flat-directory dev workflow:

```cmake
option(VIVID_BUNDLE "Build as macOS .app bundle" OFF)

if(VIVID_BUNDLE AND APPLE)
    set_target_properties(vivid PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Vivid"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.vivid.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/platform/macos/Info.plist.in"
    )
endif()
```

### 2. CMakeLists.txt — Resource copying (done)

The flat-layout `configure_file()` calls run unconditionally (they're needed for dev builds and tests). The bundle resource block is additive — when `VIVID_BUNDLE` is ON, resources are *also* placed into `Contents/Resources/` via `MACOSX_PACKAGE_LOCATION`:

```cmake
# Flat dev layout (always runs — needed for tests and dev builds)
configure_file(fonts/JetBrainsMono-Regular.ttf ${CMAKE_BINARY_DIR}/JetBrainsMono-Regular.ttf COPYONLY)
file(GLOB WGSL_PRESETS filters/*.wgsl)
foreach(f ${WGSL_PRESETS})
    configure_file(${f} ${CMAKE_BINARY_DIR}/filters/${fname} COPYONLY)
endforeach()
# ... configure_file() for each demo graph ...

# Bundle resources (additive, not if/else)
if(VIVID_BUNDLE AND APPLE)
    # Font → Resources/
    target_sources(vivid PRIVATE fonts/JetBrainsMono-Regular.ttf)
    set_source_files_properties(fonts/JetBrainsMono-Regular.ttf
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Demo graphs → Resources/
    file(GLOB _BUNDLE_GRAPHS graphs/*.json)
    target_sources(vivid PRIVATE ${_BUNDLE_GRAPHS})
    set_source_files_properties(${_BUNDLE_GRAPHS}
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # WGSL filter presets → Resources/filters/
    file(GLOB _BUNDLE_FILTERS filters/*.wgsl)
    foreach(f ${_BUNDLE_FILTERS})
        target_sources(vivid PRIVATE ${f})
        set_source_files_properties(${f}
            PROPERTIES MACOSX_PACKAGE_LOCATION Resources/filters)
    endforeach()
endif()
```

### 3. CMakeLists.txt — Plugin dylibs into bundle (done)

Operator target names are accumulated via a `GLOBAL PROPERTY` (not a CMake list variable, which wouldn't propagate across directory scopes). Each `add_vivid_operator()` call appends to it, and a post-build loop copies them into the bundle:

```cmake
# Global property definition (near top of CMakeLists.txt)
define_property(GLOBAL PROPERTY VIVID_OPERATOR_TARGETS
    BRIEF_DOCS "All operator plugin targets"
    FULL_DOCS  "Accumulated operator target names for bundle plugin copying")
set_property(GLOBAL PROPERTY VIVID_OPERATOR_TARGETS "")

# In add_vivid_operator():
set_property(GLOBAL APPEND PROPERTY VIVID_OPERATOR_TARGETS ${name})

# After all operators are defined:
if(VIVID_BUNDLE AND APPLE)
    get_property(_op_targets GLOBAL PROPERTY VIVID_OPERATOR_TARGETS)
    foreach(op_target IN LISTS _op_targets)
        add_custom_command(TARGET vivid POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${op_target}>
                $<TARGET_BUNDLE_CONTENT_DIR:vivid>/MacOS/
        )
    endforeach()
endif()
```

### 4. CMakeLists.txt — WebGPU library into bundle (done)

The existing `target_copy_webgpu_binaries(vivid)` call copies `libwgpu_native.dylib` next to the executable. When `MACOSX_BUNDLE` is set, CMake's bundle layout places the executable in `Contents/MacOS/`, so this works automatically.

### 5. `platform/macos/Info.plist.in` (done)

CMake template for the bundle metadata:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>${MACOSX_BUNDLE_EXECUTABLE_NAME}</string>
    <key>CFBundleName</key>
    <string>${MACOSX_BUNDLE_BUNDLE_NAME}</string>
    <key>CFBundleIdentifier</key>
    <string>${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>
    <key>CFBundleVersion</key>
    <string>${MACOSX_BUNDLE_BUNDLE_VERSION}</string>
    <key>CFBundleShortVersionString</key>
    <string>${MACOSX_BUNDLE_SHORT_VERSION_STRING}</string>
    <key>CFBundleIconFile</key>
    <string>${MACOSX_BUNDLE_ICON_FILE}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>NSMicrophoneUsageDescription</key>
    <string>Vivid uses the microphone for audio input.</string>
</dict>
</plist>
```

### 6. `platform/macos/Vivid.icns` (done)

Recovered from the `legacy` branch (`modules/vivid-core/assets/icons/vivid.icns`). Wired into the bundle via `MACOSX_PACKAGE_LOCATION Resources` and `MACOSX_BUNDLE_ICON_FILE` in CMakeLists.txt.

### 7. C++ change: Resource path resolution in `src/runtime/main.cpp` (done)

A `resources_dir` variable resolves to `Contents/Resources/` inside a bundle, falling back to `exe_dir` for the flat dev layout:

```cpp
auto exe_dir = exe_path.parent_path();
#ifdef __APPLE__
auto resources_dir = exe_dir.parent_path() / "Resources";
if (!std::filesystem::is_directory(resources_dir))
    resources_dir = exe_dir;  // fallback for flat dev layout
#else
auto resources_dir = exe_dir;
#endif
```

This is used for:
- **Font lookup**: `resources_dir / "JetBrainsMono-Regular.ttf"`
- **WGSL filter presets**: `resources_dir / "filters"` passed to `registry.scan_wgsl_presets()`

**Plugin scanning is unchanged** — it already scans `exe_dir` which maps to `Contents/MacOS/`.

### 8. Package system compatibility

The package system (`PackageManager`, `PackageCompiler`, `PackageCatalog`) was added after this doc was originally written. Here's what works and what doesn't from a bundled app.

**Works without changes:**
- **Loading installed packages** — packages live in `~/Library/Application Support/Vivid/packages/` (via `get_config_dir()`), independent of `exe_dir`. `PackageManager::scan_installed()` picks them up normally.
- **Package catalog browsing/fetching** — `PackageCatalog` uses the same config directory for cache and downloads.

**Doesn't work from a bundle (dev-only features):**
- **Package compilation** — `PackageCompiler` needs `-I <vivid_src>/src` for `operator_api/` headers. The source dir is found by walking up from `exe_dir` looking for `CMakeLists.txt + src/runtime/` (`main.cpp:864-875`). This heuristic fails inside a bundle because `exe_dir` is `Contents/MacOS/`. Workaround: pass `--src-dir`, or install the `operator_api/` headers into the bundle (future work).
- **GPU package compilation** — additionally needs WebGPU headers/libs from `<build_dir>/_deps/`, which won't exist in a bundle.
- **Hot-reload of built-in operators** — requires the build directory and source tree.
- **Operator creator** — patches `CMakeLists.txt` in the source tree.

These are all dev-workflow features; a bundled app is for end users running pre-built graphs with pre-compiled packages.

## Files

| File | Status |
|------|--------|
| `platform/macos/Info.plist.in` | Created |
| `platform/macos/Vivid.icns` | Created (from legacy branch) |
| `CMakeLists.txt` | Modified — bundle option, resource copying, plugin copying |
| `src/runtime/main.cpp` | Modified — `resources_dir` path resolution |

## How to Build

```bash
# Dev build (unchanged)
cmake -B build && cmake --build build

# Bundle build
cmake -B build -DVIVID_BUNDLE=ON && cmake --build build
# Result: build/vivid.app (double-clickable from Finder)
```

## Verification
1. Build with `-DVIVID_BUNDLE=ON`
2. Verify `build/vivid.app/Contents/` has correct structure (MacOS/vivid, MacOS/*.dylib, Resources/*.ttf, Resources/*.json)
3. Verify `build/vivid.app/Contents/Resources/filters/` contains `.wgsl` files
4. Double-click `vivid.app` in Finder — should launch with window, operators loaded, font rendering
5. Verify WGSL filter operators appear in the operator list
6. Verify installed packages (from `~/Library/Application Support/Vivid/packages/`) load correctly in the bundled app
7. Build without `-DVIVID_BUNDLE` — verify existing flat layout still works
8. Hot-reload: won't auto-detect source tree from inside the bundle (expected), but `--src-dir` flag still works

## Notes
- Code signing is not included here — needed for distribution but not for local use
- Notarization (for Gatekeeper) would be a follow-up if distributing outside the App Store
- The `VIVID_BUNDLE` option keeps the dev workflow completely unchanged by default
