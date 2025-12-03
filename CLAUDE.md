# Vivid - Claude Code Context

A real-time visual programming runtime for creative coding, inspired by TouchDesigner.

## Project Structure

```
vivid/
├── runtime/           # C++ runtime (WebGPU renderer, hot-reload, preview server)
├── vscode-extension/  # VS Code extension for live previews
├── examples/          # Example projects
├── templates/         # Project templates for `vivid new`
├── shaders/           # Shared WebGPU shaders
└── addons/            # Optional addon modules
```

## Quick Start

```bash
# Create a new project
vivid new my-project

# Run a project
vivid my-project
```

## LLM-First Coding Patterns

Vivid projects are designed for iterative development with Claude Code. When working on a Vivid project:

### Project Files

Each project has these key files:
- `chain.cpp` - Main chain code with setup/update functions
- `SPEC.md` - Project specification and task tracking
- `CLAUDE.md` - Project-specific context for Claude

### Intent Comments

Use these comment conventions to communicate intent:

```cpp
// === SECTION NAME ===     // Mark logical sections
// GOAL: description        // Describe what code should accomplish
```

### Workflow

1. **Start with SPEC.md** - Define what the project should do
2. **Use stub templates** - `vivid new` creates scaffolded code with GOAL comments
3. **Iterative refinement** - Update SPEC.md checkboxes as tasks complete
4. **Hot-reload** - Changes auto-compile and refresh in the runtime

## Chain API

The Chain API is the recommended way to build Vivid projects:

```cpp
#include <vivid/vivid.h>
using namespace vivid;

void setup(Chain& chain) {
    // Called once at startup
    chain.setOutput("out");  // Set which operator provides final output
}

void update(Chain& chain, Context& ctx) {
    // Called every frame
    // Access operators via chain, render with ctx
}

VIVID_CHAIN(setup, update)
```

## Shader Editing

In dev mode (binary at `build/bin/vivid`), the runtime loads shaders directly from the source `shaders/` folder. Edit `shaders/*.wgsl` and changes take effect immediately - no copying needed.

In release builds, shaders are loaded from the installed `shaders/` directory alongside the binary.

## Building

```bash
mkdir build && cd build
cmake ..
make vivid
```

## Testing Examples

```bash
./build/bin/vivid examples/hello
```
