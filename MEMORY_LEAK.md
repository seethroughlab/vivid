# Memory Leak Investigation

## Status: FIXED ✅
**Before:** ~1.5-2 MB per 10 seconds (~96 bytes × 2 per frame)
**After:** Stable memory, only 48 bytes leaked (trivial startup allocations)

## The Fix (December 2024)

**Root cause:** Surface texture not released after presenting.

For wgpu-native, you MUST call `wgpuTextureRelease(surfaceTexture.texture)` AFTER `wgpuSurfacePresent()`. This is different from Dawn which releases before present.

```cpp
// Get surface texture
WGPUSurfaceTexture surfaceTexture;
wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);

// ... render ...

// Present and release (wgpu-native specific order)
wgpuSurfacePresent(surface);
wgpuTextureViewRelease(view);
wgpuTextureRelease(surfaceTexture.texture);  // <-- THE FIX
```

Also upgraded wgpu-native from v24.0.0.2 to v27.0.2.0 for additional fixes.

## Root Cause (December 2024)

**The leak is in MALLOC_SMALL heap allocations, NOT GPU memory.**

Profiling with `footprint` and `malloc_history` revealed:
- GPU memory (IOSurface, IOAccelerator) stays constant
- MALLOC_SMALL grows steadily: 13 MB → 17 MB over 25 seconds

**Stack trace shows Metal shader compilation on every frame:**
```
vivid::Chain::process
→ vivid::effects::Noise::process
→ wgpuRenderPassEncoderEnd
→ wgpu_core::command::render::RenderPassInfo::start
→ wgpu_hal::metal::CommandEncoder::begin_render_pass
→ AGXMetalG16X::renderCommandEncoderWithDescriptor
→ AGX::Compiler::compileProgram  ← SHADER COMPILATION EVERY FRAME
```

**Allocation pattern:** ~3000 allocations per 25 seconds at sizes 768, 192, 96, 16 bytes (≈2 allocations per frame at each size).

**Cause:** Metal's AGX driver accumulates internal state for each render pass. The shader compilation happens during `begin_render_pass`, not during command buffer submit. This means the leak is per-render-pass, not per-submit.

## Command Buffer Batching - IMPLEMENTED (December 2024)

All chain operators now share a single command encoder per frame:

```cpp
// Old (N+1 submits per frame):
// Operator A: create encoder → render → finish → submit
// Operator B: create encoder → render → finish → submit
// Operator C: create encoder → render → finish → submit
// Display blit: create encoder → render → finish → submit

// New (2 submits per frame):
// Chain::process start: create single encoder
// Operator A: render (reuse encoder)
// Operator B: render (reuse encoder)
// Operator C: render (reuse encoder)
// Chain::process end: finish → submit once
// Display blit: create encoder → render → finish → submit
```

**Implementation (December 2024):**
- Added `Context::beginGpuFrame()` and `Context::endGpuFrame()` methods
- All operators now use `ctx.gpuEncoder()` instead of creating their own encoder
- `Chain::process()` wraps operator processing with `beginGpuFrame()`/`endGpuFrame()`
- `TextureOperator::endRenderPass()` detects shared encoder and skips submit

**Testing confirmed batching works:**
- 1 operator: ~2 MB/10s leak
- 4 operators: ~2 MB/10s leak (same rate, confirming batching works)

**Conclusion:** Batching reduces GPU driver overhead but doesn't fix the leak. The leak is per-render-pass (shader compilation in `begin_render_pass`), not per-submit. With batching, we now have 2 submits/frame regardless of operator count, but each operator still creates a render pass.

## What We've Tried

### 1. Vector History Pattern - IMPLEMENTED (Partial Help)
**Location:** `core/src/main.cpp:687-707`, `core/include/vivid/editor_bridge.h:47-48`

Changed `std::vector<float>` to `std::deque<float>` for `fpsHistory` and `frameTimeHistory`, replacing `erase(begin())` with `pop_front()`.

**Result:** Helped with initial memory (dropped from 421MB to 125MB after ~30s), but did NOT fix the steady-state leak.

### 2. Bind Group Caching - IMPLEMENTED (No Effect)
Added bind group caching to avoid per-frame GPU resource creation:

- **Bloom** (`core/src/effects/bloom.cpp`): Caches 4 bind groups (threshold, blur H, blur V, combine)
- **Blur** (`core/src/effects/blur.cpp`): Caches 3 bind groups (H first, H subsequent, V)
- **Display blit** (`core/src/display.cpp`): Caches blit bind group

**Result:** No measurable improvement. The leak is NOT caused by bind group creation/release patterns.

### 3. Periodic Blocking GPU Poll - TESTED AND REVERTED
Tried `wgpuDevicePoll(device, true, nullptr)` every 30 frames.

**Result:** Not a proper fix (workaround). Reverted.

### 4. @autoreleasepool Wrapper - IMPLEMENTED (No Effect)
**Location:** `core/src/platform_macos.mm`, `core/src/main.cpp`

Added `@autoreleasepool` wrapper around each frame in the main loop to ensure Metal/ObjC objects are released promptly.

**Result:** No effect on the leak. The leak is inside wgpu-native's Rust code, not in ObjC autoreleased objects. However, this is still good practice for macOS Metal applications.

## What We've Ruled Out

1. **Bind group creation/release** - All investigated bind groups are properly released
2. **Vector fragmentation** - Fixed with deque, but leak persists
3. **Bloom/Blur per-frame allocations** - Now cached, no effect
4. **Display blit per-frame bind group** - Now cached, no effect
5. **3D renderer bind groups** - Checked, properly released

## Remaining Suspects

### Confirmed: wgpu-native Texture View Leak (December 2024)

Using macOS `leaks` tool, we identified the leak as **96-byte ROOT LEAK objects** accumulating at ~2 per frame (~132/second at 60fps). This matches the number of render passes per frame.

**Key findings:**
- All leaks are exactly 96 bytes, allocated consecutively
- Leak rate is independent of operator count (confirmed batching works)
- Leak rate is independent of ImGui visibility
- `@autoreleasepool` wrapper around frame loop had no effect

**wgpu Issue #5707 ("Texture view leaks")** describes this exact pattern - texture views leak because weak pointers aren't cleaned up until the parent texture is dropped. **This was fixed in PR #5874.**

### Recommended Fix

**Upgrade wgpu-native from v24.0.0.2 to v27.0.2.0** (or newer). The texture view leak fix is included in newer versions.

In `CMakeLists.txt`, change:
```cmake
set(WGPU_VERSION "v24.0.0.2")
```
to:
```cmake
set(WGPU_VERSION "v27.0.2.0")
```

Note: Test thoroughly after upgrade as there may be API changes between versions.

### Ruled Out (December 2024)
- ~~Command encoder/buffer accumulation~~ - **FIXED** with command buffer batching. All chain operators now share one encoder.
- ~~Texture/buffer creation patterns~~ - Checked, properly managed
- ~~@autoreleasepool missing~~ - Added to main loop, no effect (leak is in Rust/wgpu internals, not ObjC)
- ~~ImGui rendering~~ - Leak persists when ImGui is hidden

## Test Results

```
examples/3d-rendering/globe (After all fixes):
[10.0s] Memory: 420 MB (total: +0 MB)
[20.0s] Memory: 424 MB (total: +4 MB)
[30.0s] Memory: 250 MB (total: -170 MB)  <- Initial cleanup
[40.0s] Memory: 125 MB (total: -295 MB)
[50.0s] Memory: 127 MB (+2 MB)
[60.0s] Memory: 129 MB (+2 MB)
[70.0s] Memory: 131 MB (+2 MB)
...continues at ~1.5-2 MB/10s
```

## Files Modified

### Command Buffer Batching (December 2024)

| File | Changes |
|------|---------|
| `core/include/vivid/context.h` | Added `beginGpuFrame()`, `endGpuFrame()`, `gpuEncoder()` |
| `core/src/context.cpp` | Implemented GPU frame encoder lifecycle |
| `core/src/chain.cpp` | Wrapped operator processing with GPU frame |
| `core/src/effects/texture_operator.cpp` | Skip submit when using shared encoder |
| `core/src/effects/noise.cpp` | Use `ctx.gpuEncoder()` |
| `core/src/effects/ramp.cpp` | Use `ctx.gpuEncoder()` |
| `core/src/effects/brightness.cpp` | Use `ctx.gpuEncoder()` |
| `core/src/effects/composite.cpp` | Use `ctx.gpuEncoder()` |
| `core/src/effects/image.cpp` | Use `ctx.gpuEncoder()` |
| `core/src/effects/displace.cpp` | Use `ctx.gpuEncoder()` |
| `core/src/effects/lfo.cpp` | Use `ctx.gpuEncoder()` |
| `core/src/effects/switch_op.cpp` | Use `ctx.gpuEncoder()` |
| `core/src/effects/time_machine.cpp` | Use `ctx.gpuEncoder()` |
| `core/src/effects/frame_cache.cpp` | Use `ctx.gpuEncoder()`, remove manual submit |
| `core/src/effects/canvas_renderer.cpp` | Use `ctx.gpuEncoder()`, remove manual submit |
| `core/src/effects/plexus.cpp` | Use `ctx.gpuEncoder()` for all 3 render methods |
| `core/src/effects/particle_renderer.cpp` | Use `ctx.gpuEncoder()` for circles and sprites |
| `core/src/effects/feedback.cpp` | Use shared encoder for render + copy |
| `core/src/effects/bloom.cpp` | Use shared encoder for all 4 passes |
| `core/src/effects/blur.cpp` | Use shared encoder for both passes |

### @autoreleasepool Wrapper (December 2024)

| File | Changes |
|------|---------|
| `core/src/platform_macos.h` | Header for platform helpers |
| `core/src/platform_macos.mm` | Implements `withAutoreleasePool()` for macOS |
| `core/src/platform_stub.cpp` | No-op stub for Windows/Linux |
| `core/src/main.cpp` | Wrap frame loop with `withAutoreleasePool()` |
| `core/CMakeLists.txt` | Added platform files to build |

### Earlier Changes

| File | Status |
|------|--------|
| `core/include/vivid/editor_bridge.h` | Changed vector to deque |
| `core/src/main.cpp` | Changed erase to pop_front, added autoreleasepool |
| `core/include/vivid/effects/bloom.h` | Added bind group cache members |
| `core/src/effects/bloom.cpp` | Implemented bind group caching |
| `core/include/vivid/effects/blur.h` | Added bind group cache members |
| `core/src/effects/blur.cpp` | Implemented bind group caching |
| `core/include/vivid/display.h` | Added blit bind group cache |
| `core/src/display.cpp` | Implemented blit bind group caching |

## Follow-Up: Bind Group Caching Pattern

While bind group caching didn't fix the leak, it's still a good optimization that should be applied to other operators. Here's the pattern:

### Header Pattern
```cpp
// In effect header (e.g., my_effect.h)
private:
    WGPUBindGroup m_cachedBindGroup = nullptr;
    WGPUTextureView m_lastInputView = nullptr;  // Track input changes
```

### Implementation Pattern
```cpp
void MyEffect::updateBindGroups(WGPUTextureView inputView) {
    // Only recreate if input texture changed
    if (inputView == m_lastInputView && m_cachedBindGroup) {
        return;
    }
    m_lastInputView = inputView;

    // Release old bind group
    if (m_cachedBindGroup) {
        wgpuBindGroupRelease(m_cachedBindGroup);
        m_cachedBindGroup = nullptr;
    }

    // Create new bind group
    WGPUBindGroupEntry entries[] = {
        {.binding = 0, .textureView = inputView},
        {.binding = 1, .sampler = m_sampler},
        // ... other bindings
    };
    WGPUBindGroupDescriptor desc = {
        .layout = m_bindGroupLayout,
        .entryCount = sizeof(entries) / sizeof(entries[0]),
        .entries = entries
    };
    m_cachedBindGroup = wgpuDeviceCreateBindGroup(m_device, &desc);
}

void MyEffect::cleanup() {
    if (m_cachedBindGroup) {
        wgpuBindGroupRelease(m_cachedBindGroup);
        m_cachedBindGroup = nullptr;
    }
    m_lastInputView = nullptr;
    // ... other cleanup
}
```

### Operators to Update
- `core/src/effects/composite.cpp` - Already uses this pattern (reference implementation)
- `core/src/effects/feedback.cpp` - Creates bind group per frame
- `core/src/effects/chromatic_aberration.cpp` - Creates bind group per frame
- `core/src/effects/color_correction.cpp` - Creates bind group per frame
- `core/src/effects/edge_detect.cpp` - Creates bind group per frame
- `core/src/effects/pixelate.cpp` - Creates bind group per frame
- Other effects in `core/src/effects/` - Check each for per-frame bind group creation
