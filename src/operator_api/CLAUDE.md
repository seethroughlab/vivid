# Operator API

## Purpose

This directory contains the public headers that define the contract between the Vivid runtime and all operators (seed, package, and project-local). Any operator `#include`s these headers and links against no other Vivid code. The stability and clarity of this API directly determines how reliably operators can be authored — by humans or by LLMs.

## Key Files

| File | Role |
|------|------|
| `operator.h` | Core contract: `OperatorBase`, `Param<T>`, compound param widget metadata, domain processable mixins, `VIVID_REGISTER` macro |
| `types.h` | Fundamental types: `VividFrameContext`, `VividAudioContext`, `VividGpuContext`, `VividOperatorDescriptor`, port type enums, `vivid_lane_state()` macro, ABI version |
| `wgsl_filter.h` | `WgslFilterBase` — GPU operator base class for WGSL shader-backed operators |
| `gpu_operator.h` | `GpuOperatorBase` — GPU operator base class for programmatic WebGPU operations |
| `gpu_common.h` | GPU utility functions shared across GPU operators (texture creation, buffer helpers) |
| `gpu_types.h` | GPU-specific type definitions |
| `audio_dsp.h` | Public DSP utilities: `WhiteNoise`, `PinkNoise`, `detect_trigger`, `waveform` |
| `child_op.h` | `ChildOp<T>` — embeds another operator as a persistent member for control-domain composition |
| `type_id.h` | `vivid_type_id<T>()` and `vivid_port_type<T>()` — compile-time type hashing for custom ports |
| `port_type_registry.h` | Custom port type registration API |
| `create_request.h` | `CreateRequest` struct used by operator scaffolding |
| `input_state.h` | `VividInputState` — keyboard/mouse state forwarded to operators when the graph UI is hidden |
| `thumbnail.h` | `VIVID_THUMBNAIL` macro for custom operator thumbnail rendering |
| `texture_readback.h` | GPU→CPU texture readback helpers |
| `data_driven_filter.h` | Data-driven WGSL filter loading |
| `draw_ui_helpers.h` | Inspector drawing helpers for custom operator UI |
| `draw_plot_helpers.h` | Plot/graph drawing helpers for operator inspectors |
| `adsr.h` | ADSR envelope implementation (embeddable) |
| `adsr_inspector.h` | ADSR inspector drawing helpers |
| `wgsl_preprocessor.h` | WGSL shader `#include` preprocessing |
| `note_types.h` | `VividNoteBuffer` / `VividNoteEvent` — the native per-note event protocol used by every note stream inside the graph |

## How It's Organized

The headers form three layers:

**Core contract** (`operator.h`, `types.h`): Every operator includes `operator.h`. It provides `OperatorBase` (params and ports), the three domain mixins (`FrameProcessable`, `AudioProcessable`, `GpuProcessable`), and the `VIVID_REGISTER` macro that generates the `extern "C"` entry points for `dlopen`. `types.h` defines the context structs passed to `process_frame/audio/gpu`, port types, and `VIVID_OPERATOR_ABI_VERSION`.

**Domain bases** (`wgsl_filter.h`, `gpu_operator.h`, `audio_dsp.h`): Specialized base classes and utilities for GPU and audio operators. `WgslFilterBase` handles the common pattern of a WGSL fragment shader with uniform params. `audio_dsp.h` provides oscillators, noise generators, and trigger detection.

**Composition and extension** (`child_op.h`, `type_id.h`, `port_type_registry.h`, `note_types.h`): Advanced features for operators that embed owned child operators, define custom port types, or carry the native note protocol. `ChildOp<T>` is for owned control-domain behavior; package-defined param widgets are presentation over primitive params, not custom storage.

## v3 ABI: display name, keywords, summary

`VIVID_OPERATOR_ABI_VERSION = 3` adds three optional descriptor fields. The `kName` static is the *stable id* — used in saved graphs, hot-reload, MCP node ids, docs URLs — and never changes. Three additional optional static members shape what users see and search:

- `kDisplayName` (`const char*`): human-facing label used in the chooser, inspector header, MCP catalog. Auto-derived from `kName` via underscore + CamelCase splitting when unset (`ChordProgression` → "Chord Progression"). Override for acronyms (`FmSynth` → "FM Synth", `Render2D` → "Render 2D").
- `kKeywords` (`std::array<const char*, N>`): search hints surfaced by the chooser (`{"harmony", "chords", "diatonic"}`). Use vocabulary a user would type that doesn't already appear in the name.
- `kSummary` (`const char*`): one-line description for the chooser preview and MCP catalog.

`VIVID_REGISTER` detects all three via SFINAE — operators that don't declare them compile and run identically to before, just with auto-derived display names. ABI mismatch (v2 vs v3 dylib) is rejected at hot-reload by the loader.

## Relationships

- **Consumers:** Every operator in `operators/`, every installed package, every project-local operator
- **Runtime coupling:** The runtime (`src/runtime/`) includes these headers to instantiate and process operators, but operators never include runtime headers
- **ABI boundary:** `VIVID_REGISTER` generates the `extern "C"` interface; `VIVID_OPERATOR_ABI_VERSION` in `types.h` is the staleness detector

## See Also

- `docs/ARCHITECTURE.md` §5.7 — operator contract design and rationale
- `docs/ARCHITECTURE.md` §5.9 — lane model and `vivid_lane_state()`
- `mcp/opdev_docs/` — API reference docs served by the opdev MCP tools
- `AGENTS.md` §Operators — file conventions and scaffolding patterns
