# vivid-cef: Chromium Embedded Framework Integration Plan

## Overview

Replace `vivid-webview` (WKWebView-based) with `vivid-cef` (Chromium Embedded Framework) for proper offscreen rendering support. CEF is the industry standard for embedding web content that renders to a texture.

### Why CEF?

| Feature | WKWebView | CEF |
|---------|-----------|-----|
| Offscreen rendering | Hack (alpha=0 window) | Native support |
| Direct pixel access | Snapshot-based | Direct buffer access |
| Cross-platform | macOS only | macOS, Windows, Linux |
| WebGL support | Limited in offscreen | Full support |
| DevTools | No | Built-in |
| Used by | Few apps | TouchDesigner, Resolume, Unreal, Unity, OBS, Spotify, Steam |

### Tradeoffs

- **Binary size**: +80-150MB (CEF binaries)
- **Build complexity**: Prebuilt binaries or build from source
- **Process model**: Multi-process (browser subprocess)
- **Memory usage**: Higher baseline (~100MB)

---

## Architecture

### CEF Process Model

```
┌─────────────────────────────────────────────────────────┐
│                    Vivid Main Process                    │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │
│  │   Chain     │  │  Visualizer │  │   vivid-cef     │  │
│  │  Rendering  │  │  (NodeGraph)│  │   (CefClient)   │  │
│  └─────────────┘  └─────────────┘  └────────┬────────┘  │
│                                              │           │
└──────────────────────────────────────────────┼───────────┘
                                               │ IPC
┌──────────────────────────────────────────────┼───────────┐
│                    CEF Subprocess                        │
│  ┌─────────────────────────────────────────────────────┐ │
│  │              Chromium Render Process                │ │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────────────────┐  │ │
│  │  │   V8    │  │  Blink  │  │  Offscreen Buffer   │  │ │
│  │  │ (JS)    │  │ (HTML)  │  │  (shared memory)    │  │ │
│  │  └─────────┘  └─────────┘  └─────────────────────┘  │ │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

### Key CEF Classes

- **CefApp**: Application-level callbacks, process initialization
- **CefClient**: Browser-level callbacks, owns render handler
- **CefRenderHandler**: Receives pixel data via `OnPaint()`
- **CefBrowser**: Individual browser instance
- **CefV8Handler**: JavaScript binding to native code

---

## API Design

### Goal: Match vivid-webview API

```cpp
// Current vivid-webview API (to be preserved)
class WebView : public TextureOperator {
    void setUrl(const std::string& url);
    void loadHtml(const std::string& html, const std::string& baseUrl);
    void executeJS(const std::string& script, Callback callback);
    void registerCallback(const std::string& name, Callback callback);

    // Input
    void setInputEnabled(bool enabled);
    void setInputOffset(int x, int y);

    // Output
    WGPUTextureView outputView() const;
    bool isReady() const;
};
```

### vivid-cef Implementation

```cpp
namespace vivid::cef {

class Browser : public TextureOperator {
public:
    // Lifecycle
    bool init(Context& ctx, int width, int height);
    void cleanup();
    void process(Context& ctx) override;

    // Content
    void setUrl(const std::string& url);
    void loadHtml(const std::string& html, const std::string& baseUrl = "");
    void reload();
    void goBack();
    void goForward();

    // JavaScript
    void executeJS(const std::string& script,
                   std::function<void(const std::string&)> callback = nullptr);
    void registerCallback(const std::string& name,
                          std::function<void(const std::string&)> callback);

    // Input
    void sendMouseEvent(MouseEventType type, int x, int y,
                        MouseButton button, int clickCount);
    void sendKeyEvent(KeyEventType type, int keyCode,
                      int modifiers, const std::string& character);
    void sendScrollEvent(int x, int y, int deltaX, int deltaY);

    // State
    bool isLoading() const;
    bool isReady() const;
    std::string currentUrl() const;
    std::string pageTitle() const;

    // Texture output (from TextureOperator)
    WGPUTextureView outputView() const override;

    // DevTools (CEF bonus feature)
    void showDevTools();
    void closeDevTools();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

// Global CEF initialization (call once at startup)
bool initializeCef(int argc, char* argv[]);
void shutdownCef();
void pumpCefMessageLoop();  // Call each frame

} // namespace vivid::cef
```

---

## Build System Integration

### Option 1: Prebuilt Binaries (Recommended)

Download prebuilt CEF binaries from https://cef-builds.spotifycdn.com/index.html

```cmake
# CMakeLists.txt
option(VIVID_USE_CEF "Use CEF for web rendering" ON)

if(VIVID_USE_CEF)
    # Download or find prebuilt CEF
    set(CEF_VERSION "120.1.10+g3ce3184+chromium-120.0.6099.129")

    if(APPLE)
        set(CEF_PLATFORM "macosx64")  # or macosarm64
    elseif(WIN32)
        set(CEF_PLATFORM "windows64")
    else()
        set(CEF_PLATFORM "linux64")
    endif()

    # FetchContent or ExternalProject to download
    include(FetchContent)
    FetchContent_Declare(
        cef_binary
        URL "https://cef-builds.spotifycdn.com/cef_binary_${CEF_VERSION}_${CEF_PLATFORM}_minimal.tar.bz2"
    )
    FetchContent_MakeAvailable(cef_binary)

    add_subdirectory(modules/vivid-cef)
endif()
```

### Directory Structure

```
modules/vivid-cef/
├── CMakeLists.txt
├── module.json
├── include/
│   └── vivid/
│       └── cef/
│           ├── browser.h
│           └── cef_all.h
├── src/
│   ├── browser.cpp
│   ├── cef_app.cpp
│   ├── cef_app.h
│   ├── cef_client.cpp
│   ├── cef_client.h
│   ├── render_handler.cpp
│   └── render_handler.h
└── subprocess/
    └── cef_subprocess.cpp  # Separate executable for render process
```

### CEF Subprocess

CEF requires a separate executable for the render process:

```cpp
// subprocess/cef_subprocess.cpp
#include <cef_app.h>

int main(int argc, char* argv[]) {
    CefMainArgs args(argc, argv);
    return CefExecuteProcess(args, nullptr, nullptr);
}
```

---

## Platform-Specific Considerations

### macOS

- App bundle structure required for subprocess
- Hardened runtime considerations
- Framework embedding

```
Vivid.app/
├── Contents/
│   ├── MacOS/
│   │   ├── vivid
│   │   └── vivid_helper      # CEF subprocess
│   ├── Frameworks/
│   │   └── Chromium Embedded Framework.framework/
│   └── Resources/
```

### Windows

- DLLs in same directory as executable
- Visual C++ runtime required
- `libcef.dll` (~100MB)

### Linux

- Shared libraries in `lib/` or rpath
- May need system dependencies (GTK, NSS, etc.)

---

## GPU Texture Sharing (Zero-Copy Rendering)

### Platform Support Summary

| Platform | Standard CEF | Custom CEF Builds |
|----------|--------------|-------------------|
| Windows  | ✅ D3D11 shared textures via `OnAcceleratedPaint` | ✅ |
| macOS    | ❌ CPU only via `OnPaint` | ⚠️ OBS CEF 5060 has patches |
| Linux    | ❌ CPU only via `OnPaint` | ⚠️ OBS CEF 5060 / Ozone (future) |

### Why Only Windows?

CEF's `OnAcceleratedPaint` with `shared_texture_enabled` only works on Windows with D3D11 shared textures. On macOS and Linux, CEF falls back to `OnPaint` which provides a CPU buffer.

**References:**
- [CEF Issue #3216](https://bitbucket.org/chromiumembedded/cef/issues/3216) - Shared texture rendering calls OnPaint instead of OnAcceleratedPaint
- [CEF Forum Discussion](https://www.magpcss.org/ceforum/viewtopic.php?f=6&t=17551) - OnAcceleratedPaint not called

### Alternatives for Zero-Copy on macOS/Linux

1. **OBS's Custom CEF Build**
   - OBS maintains a forked CEF with `OnAcceleratedPaint2` support
   - Available from OBS CDN, pinned to CEF version 5060 (older Chromium)
   - Used in production by OBS Studio

2. **Custom CEF Patches**
   - [PR #285](https://www.magpcss.org/ceforum/viewtopic.php?f=6&t=19404) added Mac/Linux shared texture support
   - Cannot be rebased beyond CEF 5060 due to Chromium compositor changes
   - Requires building CEF from source

3. **Chromium Ozone Layer**
   - Chromium's preferred abstraction for GPU surfaces
   - Currently only supported on Linux
   - Not yet integrated into CEF's OSR (offscreen rendering) mode
   - See [CEF Issue #3263](https://github.com/chromiumembedded/cef/issues/3263)

### Current vivid-cef Implementation

**Windows (TODO):**
- `OnAcceleratedPaint` receives D3D11 shared texture handle
- Import via `ID3D11Device::OpenSharedResource`
- Copy to wgpu texture (D3D11→D3D12 interop if needed)

**macOS (Implemented):**
- `OnPaint` receives BGRA CPU buffer
- Upload to wgpu texture via `wgpuQueueWriteTexture`
- On Apple Silicon: ~0.5-1ms for 1080p (unified memory helps)
- IOSurface code exists for future use with custom CEF builds

**Linux (Implemented):**
- Same as macOS: CPU buffer upload
- DMABuf code path exists for future use

### Performance Notes

The CPU fallback is functional and reasonably performant:
- Apple Silicon unified memory makes uploads fast
- Double-buffering prevents stalls
- 60fps easily achievable for typical UI content
- WebGL/heavy content may see higher latency

For production apps requiring maximum performance on macOS/Linux, consider:
1. Using OBS's custom CEF build (version 5060)
2. Building CEF from source with shared texture patches
3. Accepting CPU path performance (often sufficient)

---

## Implementation Phases

### Phase 1: Basic Integration ✅ COMPLETE

- [x] Set up CEF binary download in CMake (Spotify CDN, auto-download)
- [x] Create subprocess executable (`vivid-cef-helper`)
- [x] Implement `CefApp` for process initialization
- [x] Basic `Browser` class with URL loading
- [x] Offscreen rendering to BGRA buffer
- [x] Upload buffer to WebGPU texture
- [x] Relative file:// URL resolution

**Milestone**: Load a webpage and see it rendered as a texture ✅

### Phase 2: Input Handling ✅ COMPLETE

- [x] Mouse event forwarding (click, move, scroll)
- [x] Keyboard event forwarding
- [x] Focus management
- [x] Cursor change callbacks
- [x] `processInput(Context&)` convenience method

**Milestone**: Interactive webpage (clickable links, text input) ✅

### Phase 3: JavaScript Bridge ✅ COMPLETE

- [x] `executeJS()` implementation
- [x] `registerCallback()` for JS-to-native calls
- [x] Console message forwarding
- [x] Error handling
- [x] Load end callbacks

**Milestone**: Bidirectional JS/native communication ✅

### Phase 4: IDE Panel Migration (TODO)

- [ ] Port IDE panel HTML to work with CEF
- [ ] PTY integration (same as before)
- [ ] Terminal (xterm.js) working
- [ ] Editor (Monaco) working
- [ ] DevTools support for debugging (currently disabled - needs windowed browser)

**Milestone**: Full IDE panel functionality with CEF

### Phase 5: Polish & Optimization (PARTIAL)

- [x] Double-buffering for frame synchronization
- [x] Resource cleanup and error handling
- [x] Example projects (4 examples working)
- [ ] Windows D3D11 shared texture implementation (only platform with zero-copy)
- [ ] Documentation (API docs)

**Milestone**: Production-ready vivid-cef module

---

## Migration Path

### Compatibility Layer

Keep `vivid-webview` API available, backed by either implementation:

```cpp
// vivid/webview/webview.h
#ifdef VIVID_USE_CEF
    #include <vivid/cef/browser.h>
    namespace vivid::webview {
        using WebView = vivid::cef::Browser;
    }
#else
    // Original WKWebView implementation
    class WebView { ... };
#endif
```

### Gradual Migration

1. **Phase 1**: vivid-cef as optional module, vivid-webview default
2. **Phase 2**: vivid-cef as default, vivid-webview deprecated
3. **Phase 3**: Remove vivid-webview, vivid-cef only

---

## Resources

- **CEF Documentation**: https://bitbucket.org/chromiumembedded/cef/wiki/
- **CEF C++ API**: https://magpcss.org/ceforum/apidocs3/
- **CEF Builds**: https://cef-builds.spotifycdn.com/index.html
- **cefsimple example**: In CEF binary distribution
- **cefclient example**: Full-featured reference implementation

---

## Open Questions (Updated)

1. **Minimum CEF version?** ✅ Using CEF 120 LTS series for stability
2. **GPU acceleration?** ✅ RESOLVED - Only Windows supports `OnAcceleratedPaint` with D3D11. macOS/Linux use CPU path (see GPU Texture Sharing section above)
3. **Multi-browser support?** Multiple Browser instances work - each gets own CefBrowser
4. **Sandboxing?** Currently disabled (`no_sandbox = true`) for simplicity
5. **Build from source?** Only needed for macOS/Linux zero-copy (OBS CEF 5060 or custom patches)

---

## Estimated Timeline

| Phase | Duration | Milestone |
|-------|----------|-----------|
| Phase 1 | 1-2 weeks | Basic rendering |
| Phase 2 | 1 week | Input handling |
| Phase 3 | 1 week | JS bridge |
| Phase 4 | 1-2 weeks | IDE panel |
| Phase 5 | 1 week | Polish |
| **Total** | **5-7 weeks** | **Production ready** |
