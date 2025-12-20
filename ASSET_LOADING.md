# Asset Loading Abstraction (Web Export Prep)

Refactor shader and asset loading to use an abstraction layer. This prepares for web export (where assets must be preloaded into Emscripten's virtual filesystem) while also improving code organization.

## Current State

Shaders and assets are loaded directly from the filesystem at runtime:
```cpp
// core/src/display.cpp, effects/*.cpp, etc.
fs::path shaderPath = exeDir / "shaders" / "blit.wgsl";
std::ifstream file(shaderPath);
// ... read file contents
```

This pattern is scattered across:
- `core/src/display.cpp` - blit.wgsl, text.wgsl
- `core/src/effects/*.cpp` - effect shaders (blur.wgsl, composite.wgsl, etc.)
- `addons/vivid-render3d/src/*.cpp` - 3D shaders
- `addons/vivid-video/src/*.cpp` - video-related shaders

## Proposed Design

Create an `AssetLoader` class in core that abstracts asset loading:

```cpp
// core/include/vivid/asset_loader.h
namespace vivid {

class AssetLoader {
public:
    // Get singleton instance
    static AssetLoader& instance();

    // Load text asset (shaders, config files)
    std::string loadText(const std::string& path);

    // Load binary asset (images, fonts)
    std::vector<uint8_t> loadBinary(const std::string& path);

    // Check if asset exists
    bool exists(const std::string& path);

    // For web: preload assets into virtual filesystem
    void preload(const std::vector<std::string>& paths);

    // Set base path (useful for bundled apps)
    void setBasePath(const fs::path& path);

private:
    fs::path m_basePath;

#ifdef __EMSCRIPTEN__
    // Emscripten-specific: assets loaded via --preload-file
    std::unordered_map<std::string, std::string> m_textCache;
    std::unordered_map<std::string, std::vector<uint8_t>> m_binaryCache;
#endif
};

} // namespace vivid
```

## Implementation Phases

**Phase 1: Create AssetLoader** ✅ Complete
- [x] Create `core/include/vivid/asset_loader.h`
- [x] Create `core/src/asset_loader.cpp`
- [x] Native implementation: thin wrapper around filesystem operations
- [x] Add to core CMakeLists.txt

**Phase 2: Migrate Core Shaders** ✅ Complete
- [x] Update `display.cpp` to use AssetLoader
- [x] Update `core/src/effects/noise.cpp` to use AssetLoader
- [x] Update `core/src/effects/feedback.cpp` to use AssetLoader
- [x] Update `core/src/effects/ramp.cpp` to use AssetLoader
- [x] Remove duplicated path resolution logic

**Phase 3: Migrate Addon Shaders** ✅ Complete (no migration needed)
- [x] Verified `vivid-render3d` - no file-based shader loading
- [x] Verified `vivid-video` - no file-based shader loading
- [x] Addons use embedded shaders or PipelineBuilder

**Phase 4: Emscripten Support (when ready)**
- [ ] Add `#ifdef __EMSCRIPTEN__` implementation
- [ ] Preloaded assets accessible via Emscripten's virtual filesystem
- [ ] CMake: add `--preload-file` for shader directories

## Benefits

1. **Centralized asset loading** - One place to find/fix asset loading issues
2. **Web-ready** - Just swap implementation for Emscripten
3. **Bundling support** - Easy to embed assets in binary for standalone apps
4. **Caching** - Can add caching layer for frequently-loaded assets
5. **Error handling** - Consistent error messages for missing assets

## Files to Modify

| File | Change |
|------|--------|
| `core/include/vivid/asset_loader.h` | New file |
| `core/src/asset_loader.cpp` | New file |
| `core/src/display.cpp` | Use AssetLoader for shaders |
| `core/src/effects/*.cpp` | Use AssetLoader for effect shaders |
| `addons/vivid-render3d/src/*.cpp` | Use AssetLoader for 3D shaders |
| `core/CMakeLists.txt` | Add new source files |
