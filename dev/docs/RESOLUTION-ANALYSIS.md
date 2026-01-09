# Resolution Handling in Vivid

This document provides an overview of how resolution is managed throughout the Vivid codebase, including where hardcoded values exist and how different components interact.

## Overview

Vivid has two distinct resolution concepts:

1. **Window Size** (`ctx.width()`, `ctx.height()`) - The actual display window dimensions, can change when resized
2. **Render Resolution** (`ctx.renderWidth()`, `ctx.renderHeight()`) - The texture resolution for operators, set via CLI `--resolution` or `chain.resolution()`

The default resolution used throughout the codebase is **1280x720** (720p HD).

---

## Hardcoded 1280x720 Locations

### Core Framework

| File | Lines | Member/Variable | Purpose |
|------|-------|-----------------|---------|
| `modules/vivid-core/include/vivid/context.h` | 1048-1049 | `m_renderWidth`, `m_renderHeight` | Context's default render resolution |
| `modules/vivid-core/include/vivid/chain.h` | 496-497 | `m_defaultWidth`, `m_defaultHeight` | Chain's default resolution |
| `modules/vivid-core/include/vivid/display.h` | 67-68 | `m_screenWidth`, `m_screenHeight` | Display/font texture defaults |
| `modules/vivid-core/include/vivid/effects/texture_operator.h` | ~290 | `m_width`, `m_height` | TextureOperator default output size |
| `src/cli/include/vivid/app.h` | 20-21 | `AppConfig::windowWidth/Height` | CLI app window defaults |
| `src/cli/app.cpp` | 326-327, 373-374 | Local variables | Window initialization |

### Modules

| File | Lines | Purpose |
|------|-------|---------|
| `modules/vivid-render3d/src/particles3d.cpp` | 338-339 | Particles3D texture init |
| `modules/vivid-video/include/vivid/video/webcam.h` | 130-131 | Webcam requested resolution |
| `modules/vivid-video/include/vivid/video/avf_webcam.h` | 46, 51, 56 | macOS webcam function defaults |
| `modules/vivid-video/include/vivid/video/mf_webcam.h` | 41-43 | Windows webcam function defaults |
| `modules/vivid-core/src/chain.cpp` | 517-519 | Memory estimation fallback |

### Special Case: Magic Value Detection

**`modules/vivid-core/include/vivid/effects/simple_texture_effect.h`** (lines 243-254)

```cpp
// If using default resolution, match window dimensions
if (this->m_width == 1280 && this->m_height == 720) {
    this->m_width = ctx.width();
    this->m_height = ctx.height();
}
```

This code detects "unset" resolution by checking if it matches the hardcoded default, then auto-sizes to window dimensions. This is problematic because explicitly setting 1280x720 would be treated as "unset".

---

## TextureOperator Resolution System

### Class Hierarchy

```
Operator (base)
└── TextureOperator
    ├── SimpleGeneratorEffect<T>  (template for generators)
    │   └── Gradient, Shape, SolidColor, LFO, Noise, etc.
    └── SimpleTextureEffect<T>    (template for processors)
        └── Blur, Quantize, Edge, Mirror, Tile, etc.
```

### Resolution Members (TextureOperator)

```cpp
class TextureOperator : public Operator {
protected:
    int m_width = 1280;   // Output texture width
    int m_height = 720;   // Output texture height

public:
    void setResolution(int w, int h);      // Set explicit resolution
    int outputWidth() const;                // Get current width
    int outputHeight() const;               // Get current height
    bool matchInputResolution(int index);   // Inherit from input operator
};
```

### Three Resolution Patterns

#### Pattern 1: Generator Effects (SimpleGeneratorEffect)

Generators like `Noise`, `Gradient`, `SolidColor` have no input to inherit from.

**Current behavior:**
1. Default to 1280x720
2. In `init()`, if resolution equals 1280x720, auto-resize to `ctx.width()` × `ctx.height()`

**Affected operators:** Gradient, Shape, SolidColor, LFO (and others using SimpleGeneratorEffect)

#### Pattern 2: Processor Effects (SimpleTextureEffect)

Processors like `Blur`, `Mirror`, `Tile` take input and inherit its resolution.

**Current behavior:**
1. Default to 1280x720
2. In `process()`, call `matchInputResolution(0)` to inherit from input

**Affected operators:** Quantize, ChromaticAberration, Edge, Dither, Tile, Mirror, Vignette, Scanlines, BarrelDistortion, Transform, Pixelate, Flash, Downsample, FilmGrain, HSV, CRTEffect (17 total)

#### Pattern 3: Explicit Resolution

Some operators set resolution explicitly based on their content:

```cpp
// Image - uses loaded image dimensions
void Image::loadImage(Context& ctx) {
    m_width = imageData.width;
    m_height = imageData.height;
}

// Canvas - uses explicit size() call
canvas.size(1280, 720);

// Custom textures for 3D materials
noise.setResolution(512, 512);
```

---

## Context Resolution API

### Window Dimensions

```cpp
ctx.width()   // Current window width (may change on resize)
ctx.height()  // Current window height
```

### Render Resolution

```cpp
ctx.renderWidth()          // Render resolution width (default: 1280)
ctx.renderHeight()         // Render resolution height (default: 720)
ctx.setRenderResolution(w, h)  // Set render resolution
ctx.hasRenderResolution()  // Check if explicitly set
```

The render resolution can be set via:
- CLI: `vivid project --resolution 1920x1080`
- Chain: `chain.resolution(1920, 1080)`

---

## Chain Resolution

The Chain class also tracks resolution:

```cpp
class Chain {
    int m_defaultWidth = 1280;
    int m_defaultHeight = 720;

    void resolution(int w, int h);  // Set chain resolution
};
```

---

## 3D Rendering Resolution

### Renderer3D

```cpp
auto& renderer = chain.add<Renderer3D>("renderer");
renderer.resolution(1280, 720);  // Explicit resolution required
```

### Particles3D

Currently hardcodes 1280x720 in initialization:
```cpp
// particles3d.cpp:338-339
m_width = 1280;
m_height = 720;
```

---

## Webcam/Video Resolution

Webcam operators use 1280x720 as default requested resolution:

```cpp
// Base webcam class
int m_requestedWidth = 1280;
int m_requestedHeight = 720;

// Platform APIs
bool open(Context& ctx, int width = 1280, int height = 720, float fps = 30.0f);
```

This is a reasonable default for webcam capture (720p is widely supported).

---

## Examples/Tests

### Examples That Set Resolution Explicitly (9 files)

These demonstrate the correct explicit pattern:

- `modules/vivid-core/examples/hello-noise/chain.cpp` - `noise.setResolution(1920, 1080)`
- `modules/vivid-core/examples/input-handling/chain.cpp` - `bg.setResolution(ctx.width(), ctx.height())`
- `projects/showcase/depth-of-field/chain.cpp` - `texture.setResolution(256, 256)`
- `projects/3d-rendering/globe/chain.cpp` - `noise.setResolution(512, 512)`

### Examples Relying on Auto-Sizing (~224 files)

The vast majority of examples do NOT set explicit resolution and rely on:
- Generators auto-sizing to window dimensions
- Processors inheriting from input

---

## Memory Estimation

`chain.cpp` uses 1280x720 as fallback for memory estimation when actual dimensions are unknown:

```cpp
// Default to 1280x720 if unknown, RGBA16Float format = 8 bytes/pixel
int width = 1280;
int height = 720;
```

---

## CI/CD and Testing

- Linux CI uses virtual display at 1280x720
- Reference images generated at 1280x720
- Snapshot tests expect consistent resolution

---

## Summary of Issues

1. **Scattered defaults**: 1280x720 appears in 15+ locations
2. **Magic value detection**: `simple_texture_effect.h` uses equality check to detect "unset"
3. **Implicit behavior**: 96% of examples rely on auto-sizing rather than explicit resolution
4. **No single source of truth**: Each component defines its own default

## Potential Improvements

1. **Central defaults**: Create `vivid/defaults.h` with `kDefaultWidth`/`kDefaultHeight`
2. **Explicit flag**: Add `m_hasExplicitResolution` to avoid magic value detection
3. **Require explicit resolution**: Remove auto-sizing, update all examples
4. **Use render resolution**: Have generators default to `ctx.renderWidth()` instead of magic detection
