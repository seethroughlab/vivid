# Core Addon Merge Plan

## Overview

Merge `vivid-io`, `vivid-effects-2d`, and `vivid-network` into `core/` to eliminate artificial modularity. These addons have deep inter-dependencies and always ship together in practice.

### Current State
```
core/                    → libvivid-core.dylib (975K)
addons/vivid-io/         → libvivid-io.dylib (178K)
addons/vivid-effects-2d/ → libvivid-effects-2d.dylib (10M)
addons/vivid-network/    → libvivid-network.dylib (406K)
```

### Target State
```
core/                    → libvivid-core.dylib (~12MB)
addons/vivid-video/      → (unchanged, optional)
addons/vivid-render3d/   → (unchanged, optional)
addons/vivid-audio/      → (unchanged, optional)
addons/vivid-ml/         → (unchanged, optional)
```

## Rationale

- `vivid-effects-2d` depends on `vivid-io` and `vivid-core`
- `vivid-video` depends on `vivid-effects-2d`
- `vivid-render3d` depends on `vivid-effects-2d`
- `vivid-network` provides websocket for VSCode extension (always needed)
- The "modularity" is illusory—these always travel together

## Directory Structure After Merge

```
core/
├── include/vivid/
│   ├── context.h
│   ├── chain.h
│   ├── operator.h
│   ├── texture_operator.h
│   ├── editor_bridge.h
│   ├── io/                      ← from vivid-io
│   │   ├── image_loader.h
│   │   └── ...
│   ├── effects/                 ← from vivid-effects-2d
│   │   ├── noise.h
│   │   ├── blur.h
│   │   ├── composite.h
│   │   └── ...
│   └── network/                 ← from vivid-network
│       ├── osc.h
│       ├── udp.h
│       └── websocket.h
├── src/
│   ├── context.cpp
│   ├── chain.cpp
│   ├── io/                      ← from vivid-io
│   │   ├── image_loader.cpp
│   │   └── ...
│   ├── effects/                 ← from vivid-effects-2d
│   │   ├── noise.cpp
│   │   ├── blur.cpp
│   │   └── ...
│   └── network/                 ← from vivid-network
│       ├── osc.cpp
│       └── ...
├── shaders/                     ← from vivid-effects-2d
│   ├── noise.wgsl
│   ├── blur.wgsl
│   └── ...
└── CMakeLists.txt               ← updated to include all sources
```

## Steps

### Phase 1: Prepare

- [ ] 1.1 Create backup branch: `git checkout -b pre-core-merge`
- [ ] 1.2 Verify all tests pass before starting
- [ ] 1.3 Document current include paths used by external code

### Phase 2: Move vivid-io

- [ ] 2.1 Create `core/include/vivid/io/` directory
- [ ] 2.2 Move headers from `addons/vivid-io/include/vivid/io/` → `core/include/vivid/io/`
- [ ] 2.3 Create `core/src/io/` directory
- [ ] 2.4 Move sources from `addons/vivid-io/src/` → `core/src/io/`
- [ ] 2.5 Update `core/CMakeLists.txt` to include io sources
- [ ] 2.6 Add stb dependency to core CMakeLists.txt
- [ ] 2.7 Update include paths in moved files (if any)
- [ ] 2.8 Verify build succeeds

### Phase 3: Move vivid-effects-2d

- [ ] 3.1 Create `core/include/vivid/effects/` directory
- [ ] 3.2 Move headers from `addons/vivid-effects-2d/include/vivid/effects/` → `core/include/vivid/effects/`
- [ ] 3.3 Create `core/src/effects/` directory
- [ ] 3.4 Move sources from `addons/vivid-effects-2d/src/` → `core/src/effects/`
- [ ] 3.5 Create `core/shaders/` directory (if not exists)
- [ ] 3.6 Move shaders from `addons/vivid-effects-2d/shaders/` → `core/shaders/`
- [ ] 3.7 Update `core/CMakeLists.txt` to include effects sources
- [ ] 3.8 Add freetype dependency to core CMakeLists.txt
- [ ] 3.9 Update shader embed paths in CMake
- [ ] 3.10 Update include paths in moved files
- [ ] 3.11 Verify build succeeds

### Phase 4: Move vivid-network

- [ ] 4.1 Create `core/include/vivid/network/` directory
- [ ] 4.2 Move headers from `addons/vivid-network/include/vivid/network/` → `core/include/vivid/network/`
- [ ] 4.3 Create `core/src/network/` directory
- [ ] 4.4 Move sources from `addons/vivid-network/src/` → `core/src/network/`
- [ ] 4.5 Update `core/CMakeLists.txt` to include network sources
- [ ] 4.6 Move ixwebsocket dependency from vivid executable to vivid-core library
- [ ] 4.7 Update include paths in moved files
- [ ] 4.8 Verify build succeeds

### Phase 5: Update Remaining Addons

- [ ] 5.1 Update `addons/vivid-video/CMakeLists.txt` - remove vivid-effects-2d dependency (now in core)
- [ ] 5.2 Update `addons/vivid-render3d/CMakeLists.txt` - remove vivid-effects-2d dependency
- [ ] 5.3 Update include statements in vivid-video sources
- [ ] 5.4 Update include statements in vivid-render3d sources
- [ ] 5.5 Verify addons build against new core

### Phase 6: Update Examples and Tests

- [ ] 6.1 Update example CMakeLists.txt files (remove addon link dependencies)
- [ ] 6.2 Move `addons/vivid-io/tests/` → `tests/io/` or `core/tests/io/`
- [ ] 6.3 Move `addons/vivid-effects-2d/tests/` → `tests/effects/` or `core/tests/effects/`
- [ ] 6.4 Move `addons/vivid-network/tests/` → `tests/network/` or `core/tests/network/`
- [ ] 6.5 Move `addons/vivid-effects-2d/examples/` → `examples/effects/`
- [ ] 6.6 Move `addons/vivid-network/examples/` → `examples/network/`
- [ ] 6.7 Update test CMakeLists.txt to link only vivid-core
- [ ] 6.8 Run all tests, fix failures

### Phase 7: Cleanup

- [ ] 7.1 Remove `addons/vivid-io/` directory
- [ ] 7.2 Remove `addons/vivid-effects-2d/` directory
- [ ] 7.3 Remove `addons/vivid-network/` directory
- [ ] 7.4 Update root CMakeLists.txt (remove addon subdirectories)
- [ ] 7.5 Update `.gitignore` if needed
- [ ] 7.6 Final build verification

### Phase 8: Documentation

- [ ] 8.1 Update `CLAUDE.md` project structure section
- [ ] 8.2 Update `docs/LLM-REFERENCE.md`
- [ ] 8.3 Update `docs/OPERATOR-API.md`
- [ ] 8.4 Update any README files referencing old addon structure
- [ ] 8.5 Delete this file (CORE_ADDON.md)

## Include Path Changes

Users will need to update includes:

```cpp
// Before
#include <vivid/effects/noise.h>      // worked (no change)
#include <vivid/io/image_loader.h>    // worked (no change)
#include <vivid/network/osc.h>        // worked (no change)

// The paths stay the same - just the library they come from changes
```

## CMake Changes for Users

```cmake
# Before
target_link_libraries(my_app
    vivid-core
    vivid-io
    vivid-effects-2d
    vivid-network
)

# After
target_link_libraries(my_app
    vivid-core
)
```

## Risk Mitigation

- Keep old addon directories until all tests pass
- Test each phase independently before proceeding
- Maintain backward-compatible include paths
- Document breaking changes for any external users

## Estimated Impact

| Metric | Before | After |
|--------|--------|-------|
| Core library size | 975K | ~12MB |
| Number of libraries | 5 | 2 (core + remaining addons) |
| CMake complexity | High (inter-addon deps) | Lower |
| Build time | Similar | Slightly longer for clean builds |
