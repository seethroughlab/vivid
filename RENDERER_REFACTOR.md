# Refactor renderer.cpp (4782 lines)

**Goal**: Break up the monolithic renderer.cpp into logical modules while maintaining functionality.

## Current Structure Analysis

| Component | Lines | % of File | Description |
|-----------|-------|-----------|-------------|
| Embedded WGSL shaders | 24-1461 | 30% | 7 shader strings |
| GPU data structures | 1463-1574 | 2% | Uniform buffers, helper functions |
| Configuration setters | 1583-1816 | 5% | 30+ setter methods |
| Shadow utilities | 1817-2437 | 13% | Light matrices, resource management |
| Debug visualization | 2607-2840 | 5% | Camera/light debug drawing |
| Pipeline creation | 2921-3982 | 22% | createPipeline() 874 lines |
| Main render loop | 3983-4592 | 13% | process() 610 lines |
| Cleanup | 4595-4783 | 4% | Resource release |

## Refactoring Strategy

### Phase 1: Extract Shaders to .wgsl Files (saves ~1400 lines)

Move 7 embedded WGSL shaders to external files:

```
addons/vivid-render3d/shaders/
├── depth_copy.wgsl
├── flat.wgsl           (includes multi-light, shadow sampling)
├── pbr.wgsl
├── pbr_textured.wgsl
├── pbr_displacement.wgsl
├── pbr_ibl.wgsl
└── skybox.wgsl
```

Add shader loading utility that reads files at init time.

### Phase 2: Extract GPU Structs (~100 lines)

Create `gpu_structs.h`:
- `GPULight` struct (64 bytes)
- `Uniforms` struct (432 bytes)
- `PBRUniforms` struct (496 bytes)
- `PBRTexturedUniforms` struct (528 bytes)
- `lightDataToGPU()` helper function

### Phase 3: Extract Debug Visualization (~250 lines)

Create `debug_visualizer.h/cpp`:
- `DebugVisualizer` class containing:
  - `generateCameraFrustum()`
  - `generateDirectionalLightDebug()`
  - `generatePointLightDebug()`
  - `generateSpotLightDebug()`
  - `render()` method for debug pass

### Phase 4: Extract Shadow System (~600 lines)

Create `shadow_manager.h/cpp`:
- `ShadowManager` class containing:
  - `createDirectionalShadowResources()`
  - `createPointShadowResources()`
  - `destroyShadowResources()`
  - `renderDirectionalShadowPass()`
  - `renderPointShadowPass()`
  - Light matrix computation utilities

### Phase 5: Split createPipeline() (internal reorganization)

Break into focused helper methods:
- `createFlatPipeline()`
- `createPBRPipeline()`
- `createPBRTexturedPipeline()`
- `createPBRDisplacementPipeline()`
- `createPBRIBLPipeline()`
- `createSkyboxPipeline()`
- `createWireframePipeline()`

## Resulting File Structure

```
addons/vivid-render3d/
├── include/vivid/render3d/
│   ├── renderer.h          (existing, add forward decls)
│   ├── shadow_manager.h    (NEW)
│   ├── debug_visualizer.h  (NEW)
│   └── gpu_structs.h       (NEW)
├── src/
│   ├── renderer.cpp        (~2000 lines after refactor)
│   ├── shadow_manager.cpp  (NEW ~600 lines)
│   └── debug_visualizer.cpp(NEW ~250 lines)
└── shaders/                (NEW directory)
    └── *.wgsl files        (7 files, ~1400 lines total)
```

## Expected Results

| Before | After |
|--------|-------|
| renderer.cpp: 4782 lines | renderer.cpp: ~2000 lines |
| | shadow_manager.cpp: ~600 lines |
| | debug_visualizer.cpp: ~250 lines |
| | 7 .wgsl shader files: ~1400 lines |
| | gpu_structs.h: ~100 lines |

## Implementation Order

1. **Phase 2 first** (gpu_structs.h) - No functional change, just moves definitions
2. **Phase 1** (shaders) - Biggest line reduction, requires shader loading
3. **Phase 3** (debug_visualizer) - Self-contained, easy extraction
4. **Phase 4** (shadow_manager) - More complex, touches multiple areas
5. **Phase 5** (split createPipeline) - Internal reorganization

## Files to Create/Modify

| File | Changes |
|------|---------|
| `renderer.cpp` | Remove extracted code, add includes |
| `renderer.h` | Add forward declarations for managers |
| `CMakeLists.txt` | Add new source files, shader copy rules |
| NEW: `gpu_structs.h` | GPU uniform buffer definitions |
| NEW: `shadow_manager.h/cpp` | Shadow rendering system |
| NEW: `debug_visualizer.h/cpp` | Debug wireframe rendering |
| NEW: `shaders/*.wgsl` | 7 external shader files |
