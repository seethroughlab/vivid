# Memory Leak Investigation

## Status: UNRESOLVED
Leak rate: ~1.5-2 MB per 10 seconds (consistent across all project types)

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

## What We've Ruled Out

1. **Bind group creation/release** - All investigated bind groups are properly released
2. **Vector fragmentation** - Fixed with deque, but leak persists
3. **Bloom/Blur per-frame allocations** - Now cached, no effect
4. **Display blit per-frame bind group** - Now cached, no effect
5. **3D renderer bind groups** - Checked, properly released

## Remaining Suspects

### High Priority
1. **Command encoder/buffer accumulation** - Multiple operators submit separate command buffers per frame. wgpu-native may keep internal staging data.

2. **wgpu-native internal state** - The non-blocking `wgpuDevicePoll(device, false, nullptr)` may not fully release internal resources between frames.

3. **Texture/buffer creation patterns** - Some operators may recreate textures when resolution changes without properly tracking previous resources.

### Medium Priority
4. **ImGui/chain visualizer** - Not fully investigated
5. **Window manager blit** (`core/src/window_manager.cpp:780`) - Creates bind group per secondary window

### To Investigate
- Use native GPU profiling tools (Metal System Trace on macOS)
- Add explicit memory tracking with `wgpuDeviceGetFeatures` or similar
- Profile with Instruments to identify specific allocation sources
- Consider batching all operator work into single command buffer per frame

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

| File | Status |
|------|--------|
| `core/include/vivid/editor_bridge.h` | Changed vector to deque |
| `core/src/main.cpp` | Changed erase to pop_front |
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
