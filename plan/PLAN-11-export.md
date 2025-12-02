# PLAN-11: Export & Distribution

Video export, image sequences, WASM builds, and standalone executable packaging.

## Overview

Export capabilities for Vivid projects:

1. **Video Recording** — Export visuals to video files (MP4, MOV, HAP)
2. **Image Sequences** — Frame-by-frame image export (PNG, EXR)
3. **WASM Export** — Compile projects to WebAssembly for web browsers
4. **Standalone Export** — Package projects as standalone executables

```
┌─────────────────────────────────────────────────────────────────┐
│                       EXPORT PIPELINE                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Realtime                    Offline                            │
│  ┌──────────────┐           ┌──────────────┐                   │
│  │ Live Preview │           │ Fixed Delta  │                   │
│  │ (variable dt)│           │ (1/fps dt)   │                   │
│  └──────┬───────┘           └──────┬───────┘                   │
│         │                          │                            │
│         └──────────┬───────────────┘                            │
│                    │                                            │
│                    ▼                                            │
│         ┌──────────────────┐                                    │
│         │   Frame Buffer   │                                    │
│         └─────────┬────────┘                                    │
│                   │                                             │
│     ┌─────────────┼─────────────┐                               │
│     ▼             ▼             ▼                               │
│  ┌──────┐    ┌──────────┐   ┌──────────┐                       │
│  │ Video│    │ Image    │   │ WASM     │                       │
│  │ File │    │ Sequence │   │ Bundle   │                       │
│  └──────┘    └──────────┘   └──────────┘                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 12.11: Video Recording

### Record Operator

```cpp
class Record : public Operator {
public:
    // Configuration
    Record& output(const std::string& path);
    Record& codec(VideoCodec codec);  // H264, ProRes, HAP
    Record& fps(float framerate);
    Record& quality(float q);  // 0-1, codec-specific
    Record& duration(float seconds);  // Optional max duration

    // Control
    void start();
    void stop();
    bool isRecording() const;

    // Stats
    int framesRecorded() const;
    float durationRecorded() const;
};
```

### Video Codecs

| Codec | Format | Use Case | Platform |
|-------|--------|----------|----------|
| H.264 | MP4 | Web/sharing | All |
| H.265/HEVC | MP4 | High quality, smaller files | All |
| ProRes 422 | MOV | Professional editing | macOS |
| ProRes 4444 | MOV | With alpha channel | macOS |
| HAP | MOV | VJ playback | All |
| HAP Alpha | MOV | VJ with transparency | All |

### Video Encoder Implementation

```cpp
class VideoEncoder {
public:
    virtual ~VideoEncoder() = default;

    virtual bool open(const std::string& path, int width, int height,
                      float fps, VideoCodec codec) = 0;
    virtual bool writeFrame(const uint8_t* rgba, int stride) = 0;
    virtual void close() = 0;

    static std::unique_ptr<VideoEncoder> create();
};
```

### Platform Implementations

**macOS**: AVAssetWriter with VideoToolbox hardware encoding
**Windows**: Media Foundation with hardware encoding
**Linux/All**: FFmpeg with libx264/libx265

### Implementation Tasks

- [ ] Create VideoEncoder interface
- [ ] Implement AVAssetWriter encoder (macOS)
- [ ] Implement Media Foundation encoder (Windows)
- [ ] Implement FFmpeg encoder (cross-platform fallback)
- [ ] Add HAP encoding support
- [ ] Frame buffer readback (GPU → CPU)
- [ ] Create Record operator
- [ ] Offline rendering mode (fixed timestep)
- [ ] Progress reporting
- [ ] Audio mixdown (when audio support added)

---

## Phase 12.11b: Image Sequence Export

### ImageSequence Operator

```cpp
class ImageSequence : public Operator {
public:
    ImageSequence& output(const std::string& pattern);  // "frame_%04d.png"
    ImageSequence& format(ImageFormat fmt);  // PNG, EXR, TIFF
    ImageSequence& startFrame(int frame);

    void saveFrame();  // Save current frame
    void saveFrame(int frameNumber);  // Save with specific number
};
```

### Supported Formats

| Format | Use Case | Features |
|--------|----------|----------|
| PNG | General use | Lossless, alpha |
| EXR | HDR/VFX | 16/32-bit float, multi-layer |
| TIFF | Print/archive | Lossless, various bit depths |
| JPEG | Quick preview | Lossy, small files |

### Implementation Tasks

- [ ] Frame buffer readback
- [ ] PNG export (stb_image_write)
- [ ] EXR export (OpenEXR or tinyexr)
- [ ] TIFF export
- [ ] Numbered filename generation
- [ ] Batch export mode

---

## Phase 13.2: WASM Export

Compile Vivid projects to WebAssembly for browser playback.

### Architecture

```
Source Project          WASM Build              Browser
┌─────────────┐        ┌─────────────┐        ┌─────────────┐
│ chain.cpp   │        │ chain.wasm  │        │ <canvas>    │
│ assets/     │───────▶│ vivid.wasm  │───────▶│ WebGPU      │
│ shaders/    │        │ assets.js   │        │ JavaScript  │
└─────────────┘        └─────────────┘        └─────────────┘
```

### Build Requirements

- Emscripten SDK (emcc)
- WebGPU runtime (Dawn or browser-native)
- Asset bundling (embed textures, shaders, models)

### HTML Template

```html
<!DOCTYPE html>
<html>
<head>
    <title>Vivid Project</title>
    <style>
        canvas { width: 100vw; height: 100vh; }
    </style>
</head>
<body>
    <canvas id="canvas"></canvas>
    <script src="vivid.js"></script>
    <script>
        VividModule().then(module => {
            module.init(document.getElementById('canvas'));
            module.run();
        });
    </script>
</body>
</html>
```

### CLI Command

```bash
vivid export --wasm my-project --output dist/
```

### Implementation Tasks

- [ ] Emscripten build configuration
- [ ] WebGPU initialization for browser
- [ ] Asset embedding/bundling
- [ ] JavaScript glue code generation
- [ ] HTML template generation
- [ ] Local dev server for testing
- [ ] Optimization (size, performance)
- [ ] GitHub Pages deployment helper

---

## Phase 13.3: Standalone Export

Package Vivid projects as self-contained executables.

### Bundle Structure

```
MyProject.app/                      (macOS)
├── Contents/
│   ├── MacOS/
│   │   └── vivid-player
│   ├── Resources/
│   │   ├── chain.dylib
│   │   ├── shaders/
│   │   └── assets/
│   └── Info.plist

MyProject/                          (Windows)
├── vivid-player.exe
├── chain.dll
├── shaders/
├── assets/
└── *.dll (dependencies)

MyProject/                          (Linux)
├── vivid-player
├── chain.so
├── shaders/
├── assets/
└── lib/ (dependencies)
```

### CLI Command

```bash
vivid export --standalone my-project --output MyProject.app
vivid export --standalone my-project --output MyProject.exe  # Windows
```

### Features

- Minimal runtime (player only, no hot-reload)
- Embedded assets
- Code signing (optional)
- Installer creation (optional)

### Implementation Tasks

- [ ] Minimal player runtime (no compiler)
- [ ] Asset embedding/bundling
- [ ] Dependency collection
- [ ] macOS .app bundle creation
- [ ] Windows executable packaging
- [ ] Linux AppImage creation
- [ ] Code signing support
- [ ] Installer generators (optional)

---

## Offline Rendering

For deterministic frame-accurate recording:

```cpp
class OfflineRenderer {
public:
    void setFrameRate(float fps);
    void setDuration(float seconds);
    void setOutput(const std::string& path, VideoCodec codec);

    void render();  // Blocking, renders all frames

    // Callbacks
    void onProgress(std::function<void(float)> cb);
    void onComplete(std::function<void()> cb);
};
```

### Fixed Timestep Rendering

```cpp
// Normal realtime loop
void update(Context& ctx) {
    float dt = ctx.dt();  // Variable, depends on actual frame time
}

// Offline rendering loop
void update(Context& ctx) {
    float dt = 1.0f / 60.0f;  // Fixed 60fps
    // Renders at exact intervals regardless of actual speed
}
```

---

## Dependencies

| Library | Purpose | License |
|---------|---------|---------|
| FFmpeg | Video encoding (H.264, HAP) | LGPL/GPL |
| stb_image_write | PNG/JPEG export | Public Domain |
| tinyexr | EXR export | BSD |
| Emscripten | WASM compilation | MIT |

---

## Implementation Order

1. **Image Sequence** — PNG frame export
2. **Video Recording** — H.264 MP4 export
3. **Offline Rendering** — Fixed timestep mode
4. **HAP Export** — VJ-friendly codec
5. **ProRes Export** — macOS professional workflow
6. **WASM Export** — Browser deployment
7. **Standalone Export** — Desktop application bundles

---

## References

- [FFmpeg Encoding](https://trac.ffmpeg.org/wiki/Encode/H.264)
- [AVAssetWriter](https://developer.apple.com/documentation/avfoundation/avassetwriter)
- [Media Foundation Encoding](https://docs.microsoft.com/en-us/windows/win32/medfound/tutorial--using-the-sink-writer-to-encode-video)
- [Emscripten](https://emscripten.org/)
- [WebGPU Specification](https://www.w3.org/TR/webgpu/)
