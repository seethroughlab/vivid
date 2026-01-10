# Modules

Vivid modules are reusable libraries that add operators, effects, and integrations. They can be installed from GitHub or linked locally for development.

## Installing Modules

### From GitHub

```bash
vivid modules install https://github.com/seethroughlab/vivid-onnx
```

This downloads prebuilt binaries if available, or builds from source.

### List Installed

```bash
vivid modules list
```

Output:
```
Installed modules (1):

  vivid-onnx v0.1.0-alpha.1
    Source: prebuilt
    Path: /Users/you/.vivid/modules/vivid-onnx
```

### Update

```bash
vivid modules update              # Update all
vivid modules update vivid-onnx   # Update specific module
```

### Remove

```bash
vivid modules remove vivid-onnx
```

## Using Modules in Chains

Include the module's header and use its namespace:

```cpp
#include <vivid/vivid.h>
#include <vivid/onnx/onnx.h>

using namespace vivid;
using namespace vivid::onnx;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& pose = chain.add<PoseDetector>("pose");
    pose.input(&cam);
    pose.model("path/to/model.onnx");
}
```

Hot-reload automatically finds installed modules and links them.

## Creating Modules

### Project Structure

```
vivid-example/
  module.json           # Module metadata (required)
  CMakeLists.txt        # Build configuration
  include/
    vivid/
      example/
        example.h       # Public headers
  src/
    example.cpp         # Implementation
  lib/                  # Built libraries (created by cmake)
  examples/
    basic/
      chain.cpp         # Usage examples
  assets/               # Models, shaders, data files
```

### module.json

```json
{
  "name": "vivid-example",
  "version": "1.0.0",
  "description": "Example module for Vivid",
  "author": "Your Name",
  "license": "MIT",
  "vivid": ">=1.0.0"
}
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(vivid-example VERSION 1.0.0)

# Find Vivid SDK
if(DEFINED VIVID_ROOT)
    set(CMAKE_PREFIX_PATH "${VIVID_ROOT}" ${CMAKE_PREFIX_PATH})
endif()
find_package(vivid REQUIRED)

# Create module library
add_library(vivid-example SHARED
    src/example.cpp
)

target_include_directories(vivid-example PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(vivid-example PUBLIC vivid::vivid-core)

# Install to lib/ for module linking
install(TARGETS vivid-example
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)
```

## Development Workflow

### 1. Clone Your Module

```bash
git clone https://github.com/you/vivid-example ~/Developer/vivid-example
```

### 2. Build the Module

```bash
cd ~/Developer/vivid-example
cmake -B build -DVIVID_ROOT=~/Developer/vivid
cmake --build build
```

This creates `lib/libvivid-example.dylib` (or `.so`/`.dll`).

### 3. Link for Development

```bash
vivid modules link ~/Developer/vivid-example
```

This registers your module in `~/.vivid/modules/manifest.json` without copying files. The module appears with `[linked]` tag:

```
$ vivid modules list
Installed modules (1):

  vivid-example v1.0.0 [linked]
    Source: linked
    Path: /Users/you/Developer/vivid-example
```

### 4. Run Examples

```bash
cd ~/Developer/vivid-example/examples/basic
vivid .
```

### 5. Iterate

Edit your module code, rebuild, and Vivid hot-reload picks up changes:

```bash
# In terminal 1: Run Vivid
cd examples/basic && vivid .

# In terminal 2: Rebuild after changes
cmake --build build
```

### 6. Unlink When Done

```bash
vivid modules unlink vivid-example
```

## Publishing Modules

### Basic Distribution

Modules can be installed directly from GitHub:

```bash
vivid modules install https://github.com/you/vivid-example
```

Vivid clones the repo and builds from source.

### Prebuilt Binaries

For faster installation, add prebuilt URLs to `module.json`:

```json
{
  "name": "vivid-example",
  "version": "1.0.0",
  "prebuilt": {
    "darwin-arm64": "https://github.com/you/vivid-example/releases/download/v1.0.0/vivid-example-darwin-arm64.tar.gz",
    "darwin-x64": "https://github.com/you/vivid-example/releases/download/v1.0.0/vivid-example-darwin-x64.tar.gz",
    "linux-x64": "https://github.com/you/vivid-example/releases/download/v1.0.0/vivid-example-linux-x64.tar.gz",
    "win32-x64": "https://github.com/you/vivid-example/releases/download/v1.0.0/vivid-example-win32-x64.zip"
  }
}
```

Each archive should contain:
- `lib/` - Compiled library
- `include/` - Headers
- `module.json` - Metadata
- `assets/` - Any required assets

## Module Location

- **Installed modules**: `~/.vivid/modules/<name>/`
- **Linked modules**: Referenced in `~/.vivid/modules/manifest.json`, files stay in original location
- **Manifest**: `~/.vivid/modules/manifest.json` tracks all modules

## Example: vivid-onnx

The [vivid-onnx](https://github.com/seethroughlab/vivid-onnx) module demonstrates the complete pattern:

```bash
# For users
vivid modules install https://github.com/seethroughlab/vivid-onnx

# For developers
git clone https://github.com/seethroughlab/vivid-onnx ~/Developer/vivid-onnx
cd ~/Developer/vivid-onnx
cmake -B build -DVIVID_ROOT=~/Developer/vivid && cmake --build build
vivid modules link ~/Developer/vivid-onnx

# Run examples
cd examples/pose-tracking
vivid .
```
