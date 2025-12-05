# Vivid Windows Debugging Notes

## Current Status
**INTERMITTENT / UNCERTAIN** - The mesh-test example appeared to work after adding extensive debug output to `renderScene()`. The crash may be timing-related or an intermittent issue. Debug output has been left in place to help diagnose if it recurs.

## Environment
- Windows 10/11
- NVIDIA GeForce RTX 3080 Ti
- Vulkan backend (via Diligent Engine)
- Visual Studio 2022

## Crash Details

### Location
The crash occurs in `runtime/src/operators/render3d.cpp` in the `renderScene()` function during the **first frame** of rendering.

### Exception
- Exception code: `0xc0000005` (Access Violation / Segmentation Fault)
- Exception address: `0x00007FFD839628C1`

### Call Sequence Before Crash
```
[Mesh Test] Update frame 0
[Mesh Test] Calling render3d->process...
[Render3D] renderScene() start
[Render3D] haveShadows=0
[Render3D] CRASH!
```

The crash happens AFTER:
- PBR_Renderer created successfully
- ShadowMapManager initialized
- All 6 meshes added to scene
- All 4 lights configured

The crash happens DURING:
- First frame render, after checking haveShadows

## Issues Fixed So Far

### 1. PBR_Renderer Creation Crash (FIXED)
**Problem**: PBR_Renderer constructor crashed during `PrecomputeBRDF()` when `EnableIBL=true`

**Fix**: Disabled IBL in PBR_Renderer::CreateInfo:
```cpp
pbrCI.EnableIBL = false;   // TODO: Investigate IBL crash
```

### 2. SRB nullptr Issues (FIXED)
**Problem**: ShaderResourceBinding pointers contained garbage values after DLL hot-reload

**Fix**: Explicit nullptr initialization in constructors:
```cpp
Output() : pso_(nullptr), srb_(nullptr) {}
```

### 3. memset of CreateInfo (FIXED)
**Problem**: Using `memset(&pbrCI, 0, sizeof(pbrCI))` zeroed out sampler filter types, causing validation errors

**Fix**: Removed memset, let default constructor initialize values properly

## Current Investigation

### Likely Cause
The crash appears to be related to one of:
1. **PSO Cache lookup** - `pbrRenderer_->GetPsoCacheAccessor()` or `psoCache.Get()`
2. **Buffer mapping** - `MapHelper<Uint8>` for frame/primitive/material attribs
3. **Render target setup** - `context->SetRenderTargets()`

### Suspicious Areas

1. **Frame Attribs Buffer Layout**: The buffer is sized for 4 lights + 1 shadow-casting light, but the exact layout depends on PBR_Renderer internal expectations

2. **Shadow Map Manager**: Even with shadows disabled (`haveShadows=0`), the ShadowMapManager is still created and initialized

3. **PSO Flags**: May be requesting features that weren't enabled in PBR_Renderer::CreateInfo:
```cpp
PBR_Renderer::PSO_FLAGS basePsoFlags =
    PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS |
    PBR_Renderer::PSO_FLAG_USE_TEXCOORD0 |
    PBR_Renderer::PSO_FLAG_USE_LIGHTS;
```

### Latest Findings (Session 2)

After adding extensive debug output throughout `renderScene()`, the crash stopped occurring. The program rendered successfully with all 6 objects visible. This suggests:

1. **Timing/Race Condition**: The `std::cout` calls and `flush()` operations introduce small delays that may be masking a race condition between CPU and GPU operations.

2. **Vulkan Synchronization**: The crash may be related to missing synchronization barriers. The debug output accidentally provides enough delay for previous GPU operations to complete.

3. **Memory Initialization**: The extra time from debug output may be allowing some asynchronous initialization to complete before the render pass uses the resources.

4. **Possible Root Causes**:
   - Resource state transitions not being properly waited on
   - Buffer mapping happening before previous frame's commands are finished
   - PSO creation being async and not completed when first used

### Debug Output Left in Place

Debug output has been added to `renderScene()` at these locations to help diagnose future crashes:
- Start of function (context pointer, haveShadows flag)
- ShadowMapAttribs initialization
- Render target setup (rtv/dsv pointers)
- Viewport setup
- PSO cache accessor retrieval
- Frame buffer mapping
- Object draw loop

### What to Try If Crash Returns

1. **Add explicit fence/synchronization** after resource creation

2. **Check for proper resource state transitions** - ensure all buffers are in correct states

3. **Add validation layers** for Vulkan to catch synchronization issues

4. **Consider adding a frame delay** before using newly created resources

## Key Files

- `runtime/src/operators/render3d.cpp` - Main crash location (~line 260-400)
- `runtime/include/vivid/operators/render3d.h` - Render3D class definition
- `examples/mesh-test/chain.cpp` - Test example
- `external/DiligentEngine/DiligentFX/PBR/src/PBR_Renderer.cpp` - DiligentFX PBR implementation

## Build Commands
```bash
# CMake configure
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -G "Visual Studio 17 2022" -A x64

# Build
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Debug --target vivid

# Run
build\runtime\Debug\vivid.exe examples\mesh-test
```
