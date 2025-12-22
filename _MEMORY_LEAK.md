# Memory Leak Investigation

## Status: FIXED (December 2024)

**The leak was in wgpu-native's FFI layer, NOT the Metal driver.**

Comparative testing showed Rust wgpu doesn't leak, but C++ wgpu-native does - proving the bug was in the C bindings, not the underlying Metal backend.

**Fix merged:** https://github.com/gfx-rs/wgpu-native/pull/542
**Issue:** https://github.com/gfx-rs/wgpu-native/issues/541

**Next step:** Update Vivid to use the next wgpu-native release (after v27.0.2.0) that includes this fix.

### The Bug

In `wgpuCommandEncoderFinish`, the encoder's `open` flag is set to `false`:
```rust
command_encoder.open.store(false, atomic::Ordering::SeqCst);
```

But the Drop implementation only cleans up when `open == true`:
```rust
if self.open.load(atomic::Ordering::SeqCst) && !thread::panicking() {
    context.command_encoder_drop(self.id);  // Never reached!
}
```

**Result:** `command_encoder_drop()` is never called, causing memory to leak.

### Test Results (with fix applied)

| Version | Memory at 0s | Memory at 180s | Behavior |
|---------|--------------|----------------|----------|
| wgpu-native (unfixed) | 23 MB | 33 MB | +0.5 MB every 10 seconds |
| wgpu-native (fixed) | 21 MB | 21 MB | Stable |

### Next Steps

Once the PR is merged, update Vivid's wgpu-native dependency to the new version.

---

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

## Root Cause: wgpu-native Command Encoder Drop Bug (CONFIRMED)

**Updated December 2024:** The leak is NOT in the Metal driver - it's in wgpu-native's C FFI layer.

Comparative testing proved this:
- **Rust wgpu 24.0**: Memory stable after warm-up (no leak)
- **C++ wgpu-native v27.0.2.0**: Memory grows ~0.5 MB per 10 seconds (leaks)

Both use the same Metal backend (wgpu-hal), so the bug must be in the wgpu-native C bindings.

**The Bug:** In `wgpuCommandEncoderFinish`, the encoder is marked "closed" (`open = false`) before being released. But the Drop implementation only runs cleanup when `open == true`, so `command_encoder_drop()` is never called.

**Previous Misdiagnosis:** Stack traces showing `AGX::Compiler::compileProgram` were a red herring - they showed where memory was allocated, not why it wasn't being freed. The actual cause was missing cleanup in wgpu-native.

---

## Resolution

**Fix merged:** https://github.com/gfx-rs/wgpu-native/pull/542 (December 22, 2024)

The fix is a one-line change - remove the `open` check from the Drop implementation:

```rust
// Before (broken):
if self.open.load(atomic::Ordering::SeqCst) && !thread::panicking() {

// After (fixed):
if !thread::panicking() {
```

**Verified with Vivid:**
| Test | Before Fix (180s) | After Fix (180s) |
|------|-------------------|------------------|
| hello-noise | ~114 MB (+18 MB leak) | 95.9 MB (stable) |
| shadow-point | ~163 MB (+54 MB leak) | 109.2 MB (stable) |

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
