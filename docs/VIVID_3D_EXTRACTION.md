# Extract 3D Operators to vivid-3d Package

## Context

The 3D operator suite (15 operators, PBR scene format, tests, demos) is bundled into vivid core. The goal is to extract all of it into `../vivid-3d` so vivid is truly unopinionated about 3D. Core keeps only generic plumbing: a renamed `VIVID_PORT_GPU_DATA` port type with `void*` routing. Users can install vivid-3d, build their own 3D approach, or use any third-party 3D package — all outputting a standard `GPU_TEXTURE` at the end of their chain.

---

## Step 1: Rename GPU_SCENE → GPU_DATA and make void*

### `src/operator_api/types.h` (line 40)
- `VIVID_PORT_GPU_SCENE = 6` → `VIVID_PORT_GPU_DATA = 6`

### `src/operator_api/gpu_operator.h`
- Remove line 6: `namespace vivid::gpu { struct VividSceneFragment; }`
- Rename + retype `VividGpuState` fields:
  - `vivid::gpu::VividSceneFragment* output_scene` → `void* output_data`
  - `vivid::gpu::VividSceneFragment** input_scenes` → `void** input_data`
  - `uint32_t input_scene_count` → `uint32_t input_data_count`

### `src/runtime/scheduler.h`
- Remove line 12: `namespace vivid::gpu { struct VividSceneFragment; }`
- Rename `NodeState` fields:
  - `vivid::gpu::VividSceneFragment* scene_fragment` → `void* gpu_data`
  - `std::vector<uint32_t> scene_input_port_indices` → `data_input_port_indices`
  - `std::vector<vivid::gpu::VividSceneFragment*> resolved_scene_inputs` → `std::vector<void*> resolved_data_inputs`
- Rename `Wire` field:
  - `bool is_scene_wire` → `bool is_data_wire`

### `src/runtime/scheduler.cpp`
- Update all references to renamed fields (~12 occurrences):
  - `scene_input_port_indices` → `data_input_port_indices`
  - `resolved_scene_inputs` → `resolved_data_inputs`
  - `scene_fragment` → `gpu_data`
  - `is_scene_wire` → `is_data_wire`
  - `VIVID_PORT_GPU_SCENE` → `VIVID_PORT_GPU_DATA`
  - `output_scene` → `output_data`
  - `input_scenes` → `input_data`
  - `input_scene_count` → `input_data_count`
  - Update error messages: "GPU_SCENE" → "GPU_DATA"

### `src/runtime/control_server.cpp` (line 62)
- `case VIVID_PORT_GPU_SCENE: return "gpu_scene"` → `case VIVID_PORT_GPU_DATA: return "gpu_data"`

---

## Step 2: Create vivid-3d repo structure

`git init ../vivid-3d` and copy files into:

```
vivid-3d/
  vivid-package.json
  CMakeLists.txt
  include/operator_api/
    gpu_3d.h                         ← from src/operator_api/gpu_3d.h
  deps/
    cgltf/cgltf.h                    ← from deps/cgltf/
    tinyobjloader/tiny_obj_loader.h  ← from deps/tinyobjloader/
  operators/gpu/
    render_3d/render_3d.cpp
    shape3d/shape3d.cpp
    transform3d/transform3d.cpp
    scene_merge/scene_merge.cpp
    light3d/light3d.cpp
    mesh_import/mesh_import.cpp
    deformer/deformer.cpp
    instancer3d/instancer3d.cpp
    particles3d/particles3d.cpp
    sdf3d/sdf3d.cpp
    material3d/material3d.cpp
    ssao3d/ssao3d.cpp
    dof3d/dof3d.cpp
    environment3d/environment3d.cpp
    boolean3d/boolean3d.cpp
  tests/
    test_gpu_3d.cpp, test_render_3d.cpp, test_shape3d.cpp,
    test_material3d.cpp, test_scene3d.cpp, test_shadow3d.cpp,
    test_depth_output.cpp, test_ssao3d.cpp, test_dof3d.cpp,
    test_environment3d.cpp, test_mesh_import.cpp, test_deformer.cpp,
    test_instancer3d.cpp, test_boolean3d.cpp, test_particles3d.cpp,
    test_sdf3d.cpp, test_transform3d.cpp
    data/tetrahedron.obj
  graphs/
    3d_hello_world_demo.json ... 3d_curl_noise_demo.json (12 files)
```

`gpu_3d.h` placed at `include/operator_api/gpu_3d.h` so operators keep their existing `#include "operator_api/gpu_3d.h"` unchanged.

---

## Step 3: Update gpu_3d.h for the rename

In the vivid-3d copy of gpu_3d.h:
- Update any references from `GPU_SCENE` → `GPU_DATA`
- Update any references from `output_scene` → `output_data`, `input_scenes` → `input_data`, `input_scene_count` → `input_data_count`
- Add a convenience helper:

```cpp
inline VividSceneFragment* scene_input(const VividGpuState* gpu, uint32_t idx) {
    if (!gpu->input_data || idx >= gpu->input_data_count) return nullptr;
    return static_cast<VividSceneFragment*>(gpu->input_data[idx]);
}
```

---

## Step 4: Update 3D operators for the rename

All 15 operators need updates for the field/enum renames:
- `VIVID_PORT_GPU_SCENE` → `VIVID_PORT_GPU_DATA` in port declarations
- `gpu->output_scene` → `gpu->output_data` for scene output
- `gpu->input_scenes[i]` → `scene_input(gpu, i)` using the typed helper (adds cast)
- `gpu->input_scene_count` → `gpu->input_data_count`

---

## Step 5: Create vivid-package.json

```json
{
  "name": "vivid-3d",
  "version": "0.1.0",
  "description": "3D rendering operators for Vivid",
  "build": "cmake",
  "gpu_operators": [
    "gpu/render_3d", "gpu/shape3d", "gpu/transform3d",
    "gpu/scene_merge", "gpu/light3d", "gpu/mesh_import",
    "gpu/deformer", "gpu/instancer3d", "gpu/particles3d",
    "gpu/sdf3d", "gpu/material3d", "gpu/ssao3d",
    "gpu/dof3d", "gpu/environment3d", "gpu/boolean3d"
  ],
  "operators": [],
  "dependencies": {
    "vendor": [
      { "name": "cgltf", "include": "deps/cgltf" },
      { "name": "tinyobjloader", "include": "deps/tinyobjloader" }
    ]
  }
}
```

---

## Step 6: Create vivid-3d CMakeLists.txt

The `PackageManager` passes `VIVID_SRC_DIR`, `VIVID_BUILD_DIR`, `VIVID_PLUGIN_SUFFIX` for cmake-type packages (`src/runtime/package_manager.cpp:280-343`).

Key elements:
- `vivid_operator_api` INTERFACE target: `${VIVID_SRC_DIR}/src` + `${VIVID_SRC_DIR}/deps/glfw/deps` (linmath.h)
- `${CMAKE_SOURCE_DIR}/include` for vivid-3d's own `operator_api/gpu_3d.h`
- WebGPU discovery from `${VIVID_BUILD_DIR}/_deps/` (mirrors `src/runtime/package_compiler.cpp:77-112`)
- `deps/cgltf` and `deps/tinyobjloader` as include dirs
- FetchContent Manifold v3.0.1 for boolean3d
- `add_3d_operator()` helper mirroring vivid's `add_vivid_operator()` (CMakeLists.txt:99)

---

## Step 7: Remove 3D artifacts from vivid

### `CMakeLists.txt` — remove:
- Lines 174-188: 14 `add_vivid_operator()` calls (render_3d through environment3d)
- Line 180: `target_include_directories(mesh_import ...)` for cgltf/tinyobjloader
- Lines 190-203: Manifold FetchContent + `add_vivid_operator(boolean3d ...)`
- Lines 428-439: 12 `configure_file(graphs/3d_*.json ...)` lines
- Lines 805-1013: All 3D test executable blocks

### Delete files:
- `src/operator_api/gpu_3d.h`
- 15 operator dirs: `operators/gpu/{render_3d,shape3d,transform3d,scene_merge,light3d,mesh_import,deformer,instancer3d,particles3d,sdf3d,material3d,ssao3d,dof3d,environment3d,boolean3d}/`
- `deps/cgltf/`, `deps/tinyobjloader/`
- `graphs/3d_*.json` (12 files)
- 3D test files: `tests/test_{gpu_3d,render_3d,shape3d,material3d,scene3d,shadow3d,depth_output,ssao3d,dof3d,environment3d,mesh_import,deformer,instancer3d,boolean3d,particles3d,sdf3d,transform3d}.cpp`
- `tests/data/tetrahedron.obj`

---

## Step 8: Update MCP documentation

Check `mcp/vivid_mcp.py` for hardcoded 3D operator names. Replace specific references with: "3D operators are available via the vivid-3d package." Update `gpu_scene` → `gpu_data` in port type documentation.

---

## Verification

1. **Build vivid** — confirm renames + void* compile and non-3D tests pass:
   ```bash
   cmake -B build && cmake --build build && ctest --test-dir build
   ```

2. **Link vivid-3d**:
   ```bash
   vivid link ../vivid-3d
   ```

3. **Verify operators load**:
   ```bash
   vivid list-types  # should show all 15 3D operators
   ```

4. **Run a demo graph** from `vivid-3d/graphs/`
