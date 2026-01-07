# MCP Simplification Plan

## Current State: 19 Tools

The MCP has grown to include curated metadata that duplicates source code. This creates maintenance burden and sync issues.

## Proposed Simplification

### Keep (Runtime-only capabilities)

| Tool | Purpose | Why Essential |
|------|---------|---------------|
| `get_pending_changes` | Slider changes awaiting code update | Live runtime state |
| `get_live_params` | Current parameter values | Live runtime state |
| `clear_pending_changes` | Confirm changes applied | Runtime action |
| `discard_pending_changes` | Revert to code values | Runtime action |
| `get_runtime_status` | Compile errors, connection state | Live runtime state |
| `run_project` | Start Vivid with a project | Action |
| `stop_project` | Stop running Vivid | Action |
| `create_project` | Scaffold new project | Action |
| `capture_snapshot` | Render frame(s) to PNG | Action |
| `validate_chain` | Check if chain.cpp compiles | Action |
| `bundle_project` | Package as standalone app | Action |

**11 tools** - these do things Claude can't do by reading files.

### Simplify (Replace with file access)

| Current Tool | Current Behavior | Simplified |
|--------------|------------------|------------|
| `list_operators` | Returns JSON with all metadata | Return list of names + header paths |
| `get_operator` | Returns curated JSON (params, limitations, examples, api) | Return header file path (Claude reads it) |
| `search_docs` | Searches doc content | Return doc file paths (Claude uses Grep) |
| `search_operators` | Searches operator descriptions | Merge into `list_operators` |
| `list_modules` | Lists installed modules | Keep (useful) |
| `list_templates` | Lists project templates | Keep (useful) |
| `get_example` | Returns CLAUDE.md content | Return path (Claude reads it) |
| `list_examples` | Lists all examples | Return paths (Claude uses Glob) |

### Simplified Tool Set: 13 Tools

**Runtime (6):**
- `get_pending_changes`
- `get_live_params`
- `clear_pending_changes`
- `discard_pending_changes`
- `get_runtime_status`
- `list_operators` (simplified: names + paths only)

**Actions (5):**
- `run_project`
- `stop_project`
- `create_project`
- `capture_snapshot`
- `validate_chain`
- `bundle_project`

**Discovery (2):**
- `list_modules`
- `list_templates`

### Removed Tools

| Tool | Replacement |
|------|-------------|
| `get_operator` | Claude reads the header file directly |
| `search_docs` | Claude uses Grep on docs/ |
| `search_operators` | Claude uses Grep on headers |
| `get_example` | Claude reads CLAUDE.md directly |
| `list_examples` | Claude uses Glob on examples/ |

---

## REGISTER Macro Simplification

### Current (Complex)

```cpp
REGISTER_OPERATOR_FULL_EX(FFT, "Audio Analysis",
    "Fast Fourier Transform for frequency spectrum analysis",
    true, OutputKind::Texture)
    .limitations({"1024-sample window", "~23ms latency"})
    .related({"Levels", "BandSplit", "Spectrogram"})
    .examples({"modules/vivid-audio/examples/audio-reactive"})
    .api({".setSize(int n)"});
```

### Simplified

```cpp
REGISTER_OPERATOR(FFT, "Audio Analysis", true);
```

That's it. Category + requiresInput. Description comes from the Doxygen comment in the header. Everything else Claude can discover by reading source.

---

## Binary Distribution Structure

```
~/.vivid/
├── bin/
│   └── vivid                    # Executable
├── lib/
│   └── *.dylib                  # Shared libraries
├── include/
│   └── vivid/
│       ├── context.h            # Core API
│       ├── chain.h
│       ├── operator.h
│       └── effects/
│           ├── noise.h          # Each operator header
│           ├── blur.h
│           └── ...
├── modules/
│   ├── vivid-audio/
│   │   ├── include/vivid/audio/*.h
│   │   └── examples/*/
│   ├── vivid-video/
│   └── ...
├── docs/
│   ├── CHAIN-API.md
│   ├── CANVAS-API.md
│   ├── RECIPES.md
│   └── ...
├── templates/
│   └── *.cpp                    # Project templates
└── examples/
    └── */                       # Core examples with CLAUDE.md
```

Claude can:
1. `Read ~/.vivid/include/vivid/effects/noise.h` to understand Noise operator
2. `Grep "setSize" ~/.vivid/include/` to find methods
3. `Read ~/.vivid/docs/CHAIN-API.md` for chain documentation
4. `Glob ~/.vivid/examples/*/CLAUDE.md` to find examples

---

## Migration Path

1. **Phase 1**: Update binary distribution to include headers/docs
2. **Phase 2**: Simplify MCP tools (remove get_operator details, etc.)
3. **Phase 3**: Strip REGISTER macros to minimum
4. **Phase 4**: Remove unused metadata from OperatorMeta struct

---

## Benefits

- **No sync issues**: Source is truth, no duplicate metadata
- **Less maintenance**: No need to update REGISTER macros when API changes
- **Smaller binary**: Less embedded metadata
- **More accurate**: Claude sees actual code, not curated summaries
- **Simpler codebase**: Remove OperatorMetaBuilder, simplified macros
