# Vivid Architecture & Distribution

## Repository Structure

```
vivid/                    # Main monorepo
├── core/                 # Runtime + merged modules (io, effects-2d, network)
├── addons/
│   ├── vivid-video/      # Optional: video playback
│   ├── vivid-render3d/   # Optional: 3D rendering
│   └── vivid-audio/      # Optional: audio synthesis/analysis
├── examples/
├── tests/
└── docs/

vivid-ml/                 # Separate repo: ML inference addon (ONNX)
vivid-vscode/             # Separate repo: VSCode extension
```

### Rationale

**Monorepo for core + most addons:**
- Single clone for development
- Atomic commits across components
- Easier refactoring
- Official addons serve as reference implementations

**vivid-ml separate:**
- Heavy external dependency (ONNX Runtime ~26MB)
- Truly optional - most users don't need ML
- Tests the external addon pattern before inviting contributors

**vivid-vscode separate:**
- Different tech stack (TypeScript vs C++)
- Different release cadence
- Different audience (extension users vs runtime developers)
- Can iterate quickly without touching core

## Core Library

After merging (see CORE_ADDON.md), `libvivid-core` contains:

| Module | Description | Previously |
|--------|-------------|------------|
| Core | Context, Chain, Operator, hot-reload | vivid-core |
| IO | Image loading (stb) | vivid-io |
| Effects | 25+ 2D texture operators | vivid-effects-2d |
| Network | WebSocket (editor bridge), OSC, UDP | vivid-network |

**Size:** ~12-13MB (dylib)

**Dependencies bundled:**
- stb (image loading)
- freetype (text rendering)
- ixwebsocket (editor communication)

## Optional Addons

Addons that remain separate within the monorepo:

| Addon | Size | External Dependencies |
|-------|------|----------------------|
| vivid-video | 10MB | Platform codecs, HAP, snappy |
| vivid-render3d | 10MB | Manifold (CSG) |
| vivid-audio | 254K | KissFFT |

External addon (separate repo):

| Addon | Size | External Dependencies |
|-------|------|----------------------|
| vivid-ml | 98K + 26MB | ONNX Runtime |

## Distribution Strategy

### GitHub Releases

Each release publishes platform-specific archives:

```
vivid v1.2.0
├── vivid-darwin-arm64.tar.gz      # macOS Apple Silicon
├── vivid-darwin-x64.tar.gz        # macOS Intel
├── vivid-win32-x64.zip            # Windows
├── vivid-linux-x64.tar.gz         # Linux
│
├── vivid-video-darwin-arm64.tar.gz    # Optional addon
├── vivid-video-darwin-x64.tar.gz
├── vivid-video-win32-x64.zip
├── vivid-video-linux-x64.tar.gz
│
├── vivid-render3d-darwin-arm64.tar.gz
├── ...
│
├── vivid-audio-darwin-arm64.tar.gz
├── ...
```

vivid-ml releases from its own repo:
```
vivid-ml v1.0.0
├── vivid-ml-darwin-arm64.tar.gz
├── vivid-ml-darwin-x64.tar.gz
├── vivid-ml-win32-x64.zip
├── vivid-ml-linux-x64.tar.gz
```

### Archive Contents

Core archive:
```
vivid-darwin-arm64/
├── bin/
│   └── vivid                      # Main executable
├── lib/
│   └── libvivid-core.dylib        # Core library
├── include/                       # Headers (for addon development)
│   └── vivid/
│       ├── context.h
│       ├── operator.h
│       ├── effects/
│       ├── io/
│       └── network/
└── shaders/                       # Embedded shaders
```

Addon archive:
```
vivid-video-darwin-arm64/
├── lib/
│   └── libvivid-video.dylib
└── include/
    └── vivid/
        └── video/
```

## VSCode Extension

### Download-on-Activation Pattern

The extension does NOT bundle the vivid runtime. Instead:

1. Extension activates
2. Check for vivid installation:
   ```
   ~/.vivid/
   ├── bin/vivid
   ├── lib/libvivid-core.dylib
   └── version.txt
   ```
3. If missing or outdated:
   - Fetch `https://api.github.com/repos/jeff/vivid/releases/latest`
   - Download archive for current platform (`process.platform` + `process.arch`)
   - Extract to `~/.vivid/`
   - Show progress notification
4. Launch vivid process

### Optional Addon Detection

When a chain.cpp uses features from an optional addon:

1. Extension detects missing addon (vivid reports error or extension parses includes)
2. Prompt: "This chain uses video features. Install vivid-video?"
3. User confirms → download from GitHub releases
4. Extract to `~/.vivid/lib/`

### Extension Release

vivid-vscode publishes to VS Code Marketplace independently:
- No native binaries in the extension package
- Tiny download (~500KB)
- Works offline if runtime already installed

## Addon Development Pattern

### For Contributors

Third-party addons should follow this structure:

```
vivid-myaddon/
├── CMakeLists.txt
├── addon.json                 # Metadata
├── README.md
├── include/
│   └── vivid/
│       └── myaddon/
│           └── my_operator.h
├── src/
│   └── my_operator.cpp
├── shaders/                   # Optional
│   └── my_effect.wgsl
├── examples/
│   └── basic/
│       └── chain.cpp
└── tests/
    └── test_my_operator.cpp
```

### addon.json

```json
{
  "name": "vivid-myaddon",
  "version": "1.0.0",
  "description": "My custom addon",
  "author": "Your Name",
  "license": "MIT",
  "vivid_version": ">=1.0.0",
  "operators": [
    {
      "name": "MyOperator",
      "type": "TextureOperator",
      "header": "vivid/myaddon/my_operator.h"
    }
  ],
  "dependencies": {
    "external": ["some-library"]
  }
}
```

### CMakeLists.txt Template

```cmake
cmake_minimum_required(VERSION 3.16)
project(vivid-myaddon VERSION 1.0.0)

find_package(vivid REQUIRED)  # Find installed vivid

add_library(vivid-myaddon SHARED
    src/my_operator.cpp
)

target_include_directories(vivid-myaddon PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(vivid-myaddon PUBLIC vivid::core)

# Install rules
install(TARGETS vivid-myaddon DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
```

### Reference Implementations

Official addons in the monorepo serve as reference:
- `addons/vivid-video/` - Platform-specific code, external codecs
- `addons/vivid-render3d/` - 3D rendering, CSG integration
- `addons/vivid-audio/` - DSP, synthesis, analysis

External addon reference:
- `vivid-ml/` (separate repo) - Heavy external dependency pattern

## Build & CI

### GitHub Actions Workflow

```yaml
# .github/workflows/release.yml
name: Release

on:
  push:
    tags: ['v*']

jobs:
  build:
    strategy:
      matrix:
        include:
          - os: macos-14
            target: darwin-arm64
          - os: macos-13
            target: darwin-x64
          - os: windows-latest
            target: win32-x64
          - os: ubuntu-latest
            target: linux-x64

    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4

      - name: Build
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Release
          cmake --build build --config Release

      - name: Package
        run: |
          # Create archives for core and each addon
          ./scripts/package.sh ${{ matrix.target }}

      - name: Upload Release Assets
        uses: softprops/action-gh-release@v1
        with:
          files: |
            dist/vivid-${{ matrix.target }}.*
            dist/vivid-video-${{ matrix.target }}.*
            dist/vivid-render3d-${{ matrix.target }}.*
            dist/vivid-audio-${{ matrix.target }}.*
```

## Migration Path

### Phase 1: Core Merge
See CORE_ADDON.md for detailed steps.

### Phase 2: Extract vivid-ml
1. Create `vivid-ml` repo
2. Move `addons/vivid-ml/` contents
3. Set up CI for independent releases
4. Update main repo to remove vivid-ml
5. Document as external addon example

### Phase 3: Extract vivid-vscode
1. Create `vivid-vscode` repo
2. Move extension code
3. Implement download-on-activation
4. Set up VS Code Marketplace publishing
5. Remove extension from main repo

### Phase 4: CI & Releases
1. Set up GitHub Actions for multi-platform builds
2. Create packaging scripts
3. Test full release flow
4. Document release process

## Future Considerations

- **vivid-addon-template** repo - Cookiecutter/template for new addons
- **Addon registry** - JSON file or simple website listing community addons
- **VST3 hosting** - See VST_HOST.md for future audio plugin support
- **Package manager** - `vivid install <addon>` CLI command
