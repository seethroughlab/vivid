# Vivid - Claude Code Context

Real-time visual programming runtime for creative coding, built on Diligent Engine.

## Current Status

**Fresh start in progress** - Rebuilding with Diligent Engine as core renderer.

See `ROADMAP.md` for the implementation plan (18 phases).

## Project Structure

```
vivid/
├── runtime/           # C++ runtime (Diligent renderer, hot-reload)
├── extension/         # VS Code extension for live previews
├── external/          # DiligentEngine submodule
├── shaders/           # HLSL shaders (being rebuilt)
├── examples/          # Example projects
├── addons/            # Optional addon modules
└── docs/              # API documentation
```

## Key Files

- `ROADMAP.md` - Implementation plan (18 phases)
- `docs/PHILOSOPHY.md` - Design principles
- `docs/CHAIN-API.md` - Chain API reference
- `docs/OPERATORS.md` - Operator reference

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
./build/bin/vivid examples/diligent-test
```

## Chain API

The Chain API is the recommended way to build Vivid projects:

```cpp
#include <vivid/vivid.h>
using namespace vivid;

void setup(Chain& chain) {
    chain.add<Noise>("noise").scale(4.0f);
    chain.add<Blur>("blur").input("noise").radius(5.0f);
    chain.setOutput("blur");
}

void update(Chain& chain, Context& ctx) {
    chain.get<Noise>("noise").speed(ctx.time() * 0.5f);
}

VIVID_CHAIN(setup, update)
```

## LLM-First Patterns

Vivid projects are designed for iterative development with Claude Code:

- `chain.cpp` - Main chain code with setup/update functions
- `SPEC.md` - Project specification and task tracking
- Use `// GOAL:` comments to describe intent
