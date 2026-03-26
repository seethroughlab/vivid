# Conventions and File Layout

## Operator Directory Structure

Each operator lives in its own directory under the appropriate execution environment:

```
operators/
├── control/
│   └── my_operator/
│       ├── my_operator.cpp      # or .h if header-only
│       └── factory_presets.json  # optional
├── audio/
│   └── my_synth/
│       └── my_synth.cpp
└── gpu/
    └── my_filter/
        ├── my_filter.cpp
        ├── my_filter.wgsl       # for WgslFilterBase operators
        └── factory_presets.json  # optional
```

## Naming Conventions

- **Operator name** (`kName`): PascalCase (e.g. `"ToneGen"`, `"SpreadNoise"`)
- **Directory and file names**: lowercase_with_underscores (e.g. `tone_gen/tone_gen.cpp`)
- **Param names**: lowercase_with_underscores (e.g. `"frequency"`, `"decay_time"`)
- **Port names**: lowercase_with_underscores (e.g. `"input"`, `"gate_out"`, `"texture"`)

## CMakeLists.txt Pattern

Each operator directory needs to be registered in its parent environment's `CMakeLists.txt`. The build system compiles each operator as a shared library (`.dylib`/`.so`/`.dll`).

For seed operators (built into the core):
```cmake
vivid_add_operator(my_operator control/my_operator/my_operator.cpp)
```

For package operators, the package's `CMakeLists.txt` handles registration.

## Package Manifest (vivid-package.json)

```json
{
  "name": "vivid-my-package",
  "version": "1.0.0",
  "description": "My custom operators",
  "build": "cmake",
  "operators": [
    {
      "name": "MyOperator",
      "source": "my_operator.cpp",
      "env": "control"
    }
  ]
}
```

## Semantic Tags

Common semantic tags used across the codebase:

| Tag | Meaning | Typical Unit |
|-----|---------|-------------|
| `frequency_hz` | Frequency parameter | Hz |
| `amplitude_linear` | Linear amplitude | — |
| `amplitude_db` | Decibel amplitude | dB |
| `gate` | Gate/trigger signal | — |
| `color_rgba` | Color value | — |
| `time_seconds` | Duration | s |
| `phase_normalized` | Phase [0,1] | — |
| `angle_degrees` | Rotation angle | ° |

Common shapes: `scalar`, `vec2`, `vec3`, `color`, `event`

Common intents: `input_gain`, `dc_offset`, `animation_rate`, `modulation_depth`

## When to Scaffold vs Start from an Example

- **Scaffold** (`scaffold_operator`) when building something new that doesn't closely resemble any existing operator. The template gives you correct boilerplate (CMake registration, capability interface, VIVID_REGISTER) and a working starting point.
- **Clone an example** (`clone_and_edit` in the UI, or copy an operator's source manually) when an existing operator is close to what you need. This preserves working patterns (port declarations, DSP logic, shader structure) and lets you modify rather than build from scratch.
- **Tip:** Use `search_example_operators(query)` in the opdev MCP server to find operators that match your goal before deciding.

## Operator Design Guidelines

1. **Single responsibility** — each operator does one thing well
2. **Generic naming** — `"ToneGen"` not `"MyBassSynth"`; broad params, not single-purpose
3. **Default to useful** — defaults should produce visible/audible output immediately
4. **kTimeDependent** — set `true` only if the operator actually reads `ctx->time`. False positives waste scheduler cycles.
5. **Semantic metadata** — add `semantic_tag`, `semantic_shape`, `semantic_unit` to params for MCP/tooling integration
6. **Audio thread safety** — no allocations, locks, or I/O in `process_audio()`
7. **GPU resource cleanup** — use RAII handles (`vivid::gpu::PipelineHandle` etc.) or explicit cleanup in destructor

## Factory Presets

Optional `factory_presets.json` alongside the operator source:
```json
{
  "presets": [
    {
      "name": "Warm Pad",
      "params": {
        "frequency": 220.0,
        "decay_time": 0.8,
        "waveform": 0
      }
    }
  ]
}
```
