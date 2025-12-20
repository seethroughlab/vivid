# Memory Leak Investigation

## Status: CONFIRMED DRIVER-LEVEL ISSUE
Leak rate correlates with render pass count - this is a Metal/wgpu-native driver-level issue that cannot be fixed at the application level.

**Upstream Issue Filed:** https://github.com/gfx-rs/wgpu/issues/8768 (December 2024)
Check back for updates or potential workarounds from the wgpu maintainers.

| Test | Render Passes | Leak Rate |
|------|---------------|-----------|
| hello-noise (1 operator) | ~2 | ~1 MB/10s |
| lighting-test (3D, no shadows) | ~2 | ~1.5 MB/10s |
| shadow-point (6 cube faces) | ~9 | ~3-4 MB/10s |

## Current Understanding (December 2024)

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

## Render3D Shadow Bind Group Caching - IMPLEMENTED (December 2024)

The Render3D point shadow system was creating bind groups every frame:

### Before (Per-Frame Allocations)
```cpp
// In process() - shadow sample bind group recreated EVERY FRAME:
if (m_shadowSampleBindGroup) {
    wgpuBindGroupRelease(m_shadowSampleBindGroup);
}
m_shadowSampleBindGroup = wgpuDeviceCreateBindGroup(...);  // 60x per second!

// In renderShadowPass() - bind group created every frame:
WGPUBindGroup shadowBindGroup = wgpuDeviceCreateBindGroup(...);
// ... use it ...
wgpuBindGroupRelease(shadowBindGroup);

// In renderPointShadowPass() - 6 bind groups created every frame (one per cube face):
for (int face = 0; face < 6; face++) {
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(...);
    // ... use it ...
    wgpuBindGroupRelease(bindGroup);
}

// Also: wgpuRenderPipelineGetBindGroupLayout called every frame (acquires reference)
WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
// ... use it ...
wgpuBindGroupLayoutRelease(layout);
```

### After (Cached)
```cpp
// Shadow sample bind group: only recreated when textures change
if (m_shadowBindGroupDirty) {
    // ... recreate bind group ...
    m_shadowBindGroupDirty = false;
}

// Shadow pass bind groups: created once in createShadowResources/createPointShadowResources
// Reused every frame via m_shadowPassBindGroup and m_pointShadowPassBindGroup

// Bind group layout: cached as m_pointShadowBindGroupLayout
```

**Result:** Reduces per-frame GPU resource churn but does NOT fix the underlying leak. The leak is at the Metal driver level, not from bind group allocations.

### Files Modified

| File | Changes |
|------|---------|
| `addons/vivid-render3d/include/vivid/render3d/renderer.h` | Added `m_shadowBindGroupDirty`, `m_shadowPassBindGroup`, `m_pointShadowBindGroupLayout`, `m_pointShadowPassBindGroup` |
| `addons/vivid-render3d/src/renderer.cpp` | Cached bind groups in create functions, use cached versions in render functions |

### Test Results (shadow-point fixture)

```
Before bind group caching:
[10.0s] Memory: 109 MB (total: +0 MB, last 10s: +0 MB)
[80.0s] Memory: 132 MB (total: +23 MB, last 10s: +3 MB)

After bind group caching:
[10.0s] Memory: 109 MB (total: +0 MB, last 10s: +0 MB)
[80.0s] Memory: 133 MB (total: +24 MB, last 10s: +3 MB)
```

Same leak rate (~3 MB/10s), confirming the leak is NOT from bind group creation.

---

## Command Buffer Batching - IMPLEMENTED (December 2024)

All chain operators now share a single command encoder per frame:

```cpp
// Old (N+1 submits per frame):
// Operator A: create encoder → render → finish → submit
// Operator B: create encoder → render → finish → submit
// ...

// New (2 submits per frame):
// Chain::process start: create single encoder
// Operator A: render (reuse encoder)
// Operator B: render (reuse encoder)
// Chain::process end: finish → submit once
// Display blit: create encoder → render → finish → submit
```

**Result:** Leak rate is now independent of operator count (confirmed batching works), but leak persists.

---

## What We've Tried (Summary)

| Fix | Result |
|-----|--------|
| Command buffer batching | Leak independent of operator count, but persists |
| Vector → deque for history | Helped initial memory, not steady-state leak |
| Bind group caching (Bloom, Blur, Display) | No effect |
| Bind group caching (Render3D shadows) | No effect |
| wgpu-native upgrade to v27.0.2.0 | Already applied, leak persists |
| Surface texture release after present | Already applied |
| @autoreleasepool wrapper | No effect |
| Periodic GPU poll | Not a fix, reverted |

---

## What We've Ruled Out

1. **Bind group creation/release** - Extensively cached, no effect
2. **Vector fragmentation** - Fixed with deque
3. **wgpu-native texture view leak** - Fixed in v27.0.2.0 (already using this version)
4. **Surface texture lifecycle** - Fixed (release after present)
5. **Command encoder accumulation** - Fixed with batching
6. **ImGui rendering** - Leak persists when hidden

---

## Root Cause: Metal Driver-Level Allocations (CONFIRMED)

**Verified December 2024:** All apps leak, including the simplest 1-operator example (hello-noise). The leak is proportional to render pass count, not operator complexity.

The leak is in Apple's Metal/AGX driver, triggered by wgpu-native's render pass creation:

1. Each `wgpuCommandEncoderBeginRenderPass` triggers Metal shader compilation
2. Metal's AGX compiler (`AGX::Compiler::compileProgram`) allocates ~96 bytes per render pass
3. These allocations accumulate and are not freed promptly
4. This is outside application control - it's in the GPU driver

**Evidence:**
- Leak rate correlates with number of render passes, not bind groups or textures
- `leaks` tool shows 96-byte ROOT LEAK objects
- Stack traces point to `AGXMetalG16X::renderCommandEncoderWithDescriptor`
- wgpu-native v27.0.2.0 includes texture view leak fix, but this is a different issue

---

## Potential Mitigations

Since this is a driver-level issue, application-level fixes have limited impact:

| Mitigation | Feasibility | Impact |
|------------|-------------|--------|
| Reduce render pass count | Medium | Reduces leak rate proportionally |
| Pipeline caching | ✅ Verified | Already cached - no effect |
| Bind group caching | ✅ Verified | Already cached - no effect |
| Blocking wgpuDevicePoll | ✅ Tested | No effect |
| desiredMaximumFrameLatency=2 | ✅ Tested | No effect |
| Metal shader precompilation | Low | WebGPU doesn't expose this |
| Report to gfx-rs/wgpu | Recommended | May require upstream fix |
| Wait for Metal/driver fix | N/A | Apple-controlled |

**Recommendation:** Accept ~1 MB/10s baseline leak as unavoidable with current wgpu-native on Metal. Focus on reducing unnecessary render passes (e.g., merge shadow passes where possible).

---

## External Evidence: Known Cross-Project Issue

This leak affects multiple projects using wgpu on Metal:

### gfx-rs/gfx Issue #2647
[Metal memory leak ~1 MB/s](https://github.com/gfx-rs/gfx/issues/2647)
- Objects leaking per render pass: `BronzeMtlRenderCmdEncoder` (12.50 KiB from `renderCommandEncoderWithDescriptor`), `MTLRenderPassDescriptorInternal`, etc.
- Root cause: Metal internally uses autorelease for object cleanup

### wgpu-native Issue #132
[Memory leak on Metal in triangle example](https://github.com/gfx-rs/wgpu-native/issues/132)
- Leak rate: ~5 MB/second in simple triangle example
- Partially attributed to "Metal debug interaction"

### wgpu Issue #1783
[Memory leak on MacOS M1 OSX](https://github.com/gfx-rs/wgpu/issues/1783)
- "Has something to do with `begin_render_pass` and high framerates"
- Metal's `nextDrawable` fails to respect limits during occlusion

### Bevy Engine Issues
- [bevyengine/bevy#5856](https://github.com/bevyengine/bevy/issues/5856) - Memory skyrockets when occluded
- [bevyengine/bevy#3612](https://github.com/bevyengine/bevy/issues/3612) - MacOS M1 excessive memory in pass nodes
- Conclusion: "This appears to be an Apple problem - Google's Dawn has the same issue"

### Apple Developer Forums
[Metal render pass discussions](https://developer.apple.com/forums/thread/120931) confirm memory increase from `commit()` calls, attributed to "debug tracking overhead."

---

## Verification (December 2024)

Tested multiple configurations to confirm the leak is universal:

```bash
# Simple 2D (1 operator): ~1 MB/10s
timeout 30 ./build/bin/vivid examples/getting-started/02-hello-noise

# 3D without shadows (multiple lights): ~1.5 MB/10s
timeout 30 ./build/bin/vivid examples/3d-rendering/lighting-test

# 3D with point shadows (9 render passes): ~3-4 MB/10s
timeout 30 ./build/bin/vivid testing-fixtures/shadow-point
```

**Code review verified:**
- ✅ Pipelines are created once and cached (`m_initialized` guard)
- ✅ Bind groups are cached (shadow, IBL, material)
- ✅ Textures/views are reused (no per-frame creation)
- ✅ No per-frame allocations in TextureOperator, Noise, or Render3D

---

## Test Commands

```bash
# Run shadow-point fixture with memory tracking
./build/bin/vivid testing-fixtures/shadow-point

# Run for extended period to observe leak
timeout 120 ./build/bin/vivid testing-fixtures/shadow-point 2>&1 | grep Memory
```

---

## Files Modified (All Changes)

### Render3D Shadow Caching (December 2024)
- `addons/vivid-render3d/include/vivid/render3d/renderer.h`
- `addons/vivid-render3d/src/renderer.cpp`

### Command Buffer Batching (December 2024)
- `core/include/vivid/context.h`
- `core/src/context.cpp`
- `core/src/chain.cpp`
- `core/src/effects/texture_operator.cpp`
- Various effect files (noise, ramp, brightness, composite, etc.)

### Core Effect Bind Group Caching
- `core/include/vivid/effects/bloom.h` / `bloom.cpp`
- `core/include/vivid/effects/blur.h` / `blur.cpp`
- `core/include/vivid/display.h` / `display.cpp`

### Other Fixes
- `core/include/vivid/editor_bridge.h` - vector → deque
- `core/src/main.cpp` - Surface texture release, deque usage
