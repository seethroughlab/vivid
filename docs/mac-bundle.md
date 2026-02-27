# macOS App Bundle for Vivid

## Context

Vivid currently builds as a plain executable (`build/vivid`) with ~60 operator plugin dylibs, a font file, a WebGPU runtime library, and demo graphs all loose in the build directory. This means launching requires the terminal, and there's no way to double-click it in Finder, pin it to the Dock, or find it in Spotlight. Building as a macOS `.app` bundle solves all of this.

## Bundle Structure

```
Vivid.app/
  Contents/
    Info.plist
    MacOS/
      vivid                    (main executable)
      *.dylib                  (60 operator plugins)
      libwgpu_native.dylib     (WebGPU runtime)
    Resources/
      Vivid.icns               (app icon)
      JetBrainsMono-Regular.ttf
      *.json                   (demo graphs)
```

**Why plugins in MacOS/ not PlugIns/:** The operator registry scans `exe_dir` (the directory containing the executable) for `*.dylib` files. Placing plugins in `Contents/MacOS/` means the existing scan logic works unmodified — no C++ changes needed for plugin discovery.

## Changes

### 1. CMakeLists.txt — Bundle configuration

Add a `VIVID_BUNDLE` option (default OFF) so the flat-directory dev workflow is preserved:

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

### 2. CMakeLists.txt — Resource copying (bundle-aware)

Replace the `configure_file()` block with logic that handles both modes:

```cmake
if(VIVID_BUNDLE AND APPLE)
    # Font → Resources/
    target_sources(vivid PRIVATE fonts/JetBrainsMono-Regular.ttf)
    set_source_files_properties(fonts/JetBrainsMono-Regular.ttf
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Demo graphs → Resources/
    file(GLOB DEMO_GRAPHS graphs/*.json)
    target_sources(vivid PRIVATE ${DEMO_GRAPHS})
    set_source_files_properties(${DEMO_GRAPHS}
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Icon → Resources/
    target_sources(vivid PRIVATE platform/macos/Vivid.icns)
    set_source_files_properties(platform/macos/Vivid.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(vivid PROPERTIES
        MACOSX_BUNDLE_ICON_FILE Vivid.icns)
else()
    # Flat dev layout (current behavior — existing configure_file calls)
    configure_file(fonts/JetBrainsMono-Regular.ttf ...)
    # ... all existing configure_file(graphs/...) calls ...
endif()
```

### 3. CMakeLists.txt — Plugin dylibs into bundle

Plugins are MODULE libraries that build into the build root. For the bundle, copy them into `Vivid.app/Contents/MacOS/` via a post-build step. Accumulate operator target names in a list during `add_vivid_operator()` calls, then iterate:

```cmake
# In add_vivid_operator(), append to a list:
list(APPEND VIVID_OPERATOR_TARGETS ${name})

# After all operators are defined:
if(VIVID_BUNDLE AND APPLE)
    foreach(op_target IN LISTS VIVID_OPERATOR_TARGETS)
        add_custom_command(TARGET vivid POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${op_target}>
                $<TARGET_BUNDLE_CONTENT_DIR:vivid>/MacOS/
        )
    endforeach()
endif()
```

### 4. CMakeLists.txt — WebGPU library into bundle

The existing `target_copy_webgpu_binaries(vivid)` call copies `libwgpu_native.dylib` next to the executable. When `MACOSX_BUNDLE` is set, CMake's bundle layout places the executable in `Contents/MacOS/`, so this should work automatically.

### 5. New file: `platform/macos/Info.plist.in`

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

### 6. New file: `platform/macos/Vivid.icns`

Placeholder icon. Generate from a PNG with `iconutil`:
```bash
mkdir Vivid.iconset
# Add icon_16x16.png through icon_512x512@2x.png
iconutil -c icns Vivid.iconset -o platform/macos/Vivid.icns
```

### 7. C++ change: Resource path resolution (`src/runtime/main.cpp`)

The font and demo graph lookups need to check the bundle's `Resources/` directory. When running as a bundle, `exe_dir` is `Vivid.app/Contents/MacOS/` and resources are at `../Resources/`.

Add a `resources_dir` alongside the existing `exe_dir`:

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

Then use `resources_dir` for:
- **Font lookup**: `resources_dir / "JetBrainsMono-Regular.ttf"` (with existing exe_dir fallback)
- **Default graph**: `resources_dir / "graph.json"` (with existing CWD fallback)

**Plugin scanning is unchanged** — it already scans `exe_dir` which maps to `Contents/MacOS/`.

## Files to Create
- `platform/macos/Info.plist.in`
- `platform/macos/Vivid.icns` (placeholder)

## Files to Modify
- `CMakeLists.txt` — bundle option, resource copying, plugin copying
- `src/runtime/main.cpp` — resource path resolution for bundle layout

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
3. Double-click `vivid.app` in Finder — should launch with window, operators loaded, font rendering
4. Build without `-DVIVID_BUNDLE` — verify existing flat layout still works
5. Hot-reload: won't auto-detect source tree from inside the bundle (expected), but `--src-dir` flag still works

## Notes
- Code signing is not included here — needed for distribution but not for local use
- Notarization (for Gatekeeper) would be a follow-up if distributing outside the App Store
- The `VIVID_BUNDLE` option keeps the dev workflow completely unchanged by default
