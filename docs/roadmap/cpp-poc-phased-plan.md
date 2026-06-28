# Vivid C++ PoC — Phased Implementation Plan

Status: active. See [ADR-0009](../decisions/ADR-0009-two-surface-bridge-and-cpp-poc.md) for the
pivot rationale. Code lives under `app/` on branch `poc-cpp-prototype`.

A real macOS C++ proof of concept of **two best-in-class surfaces + a bridge** — a DAW (Ableton
Session View) and a node-based visuals platform (TouchDesigner), sharing one transport — leaning on
`vivid-classic` for battle-tested subsystems (not its runtime). Each phase is a committable, runnable
milestone ending in a concrete proof, ordered to de-risk the hard parts early.

**Verification reality:** the dev sandbox has no display/audio, so phases are verified to
**compile + link** there; the audible/visible proofs are run on a Mac
(`cmake --build app/build && ./app/build/vivid_poc`).

## Locked decisions

- Minimal hand-rolled core; copy specific Classic pieces, not the operator registry / graph compiler.
- Reuse `Renderer2D` for 2D/text; **build a minimal custom node editor** (not extract `NodeGraphUI`).
- WebGPU backend; shader operator authored in **GLSL** (GLSL→SPIR-V→wgpu).
- Build the two halves separately, then connect: audio (P1–P4), visual (P5–P7), bridge (P8).

## Phases

| Phase | Goal | Proof |
|-------|------|-------|
| **P0 — Foundation** ✅ | GLFW window + `GpuContext` (WebGPU) + miniaudio + shared `Transport` + test tone | window pulses on the beat; tone plays |
| **P1a — VST3 host** ✅ | Extract `vst3_host_common.h`; scan/load an instrument; audio thread plays a transport-synced arpeggio | a VST3 synth arpeggiates in time |
| **P1b — MIDI clips** | Extract `note_types.h` + `midi_clip_core.h` scheduling; play a pattern `[{p,s,d,v}]` sample-accurately, transport-locked | a MIDI clip plays through the synth |
| **P2 — Session model + launch** | In-memory tracks/clips/scenes; UI→audio clip-launch over a lock-free SPSC queue (`direct_param_queue.h`); transport-quantized | keys launch clips/scenes → audio changes |
| **P3 — `Renderer2D`** | Extract `Renderer2D` + `stb_truetype` + a font; draw text/rects/lines; GLFW input | text + shapes render; clicks reported |
| **P4 — Session grid UI** | Session View on `Renderer2D` (transport, tracks×scenes grid, mixer); click a clip → launch | **Proof A:** click a clip → hear it |
| **P5 — Shader operator** | GLSL→SPIR-V (glslang/shaderc)→wgpu fullscreen pass into a viewer; std uniforms | a GLSL shader renders live |
| **P6 — Analysis → shader** | RMS on master output → `Transport.level` bridge → shader uniform (hardcoded wire) | **Proof B:** shader reacts to audio |
| **P7 — Node editor** | ~2K LOC custom editor on `Renderer2D`: nodes, bezier wires, drag-to-connect; wiring drives the shader | the visuals graph is editable |
| **P8 — The bridge** | Right-click an audio characteristic → data-source node → wire to a shader uniform → VST output drives the shader | **the audio→visual bridge, end to end** |

**PoC is declared proven at P8.** P9 (JSON persistence, threading hardening, optional VST3 plugin-GUI
window) is post-PoC / optional.

## Classic subsystems borrowed (by phase)

- P1a: `operators/shared/vst3_host/vst3_host_common.h`, `vst3_vstiids.cpp`, `plugin_common/base64.h`
- P1b: `operators/control/midi_clip/midi_clip_core.h`, `src/operator_api/note_types.h`, `operators/shared/sequencer/note_helpers.h`
- P2: `operators/shared/plugin_common/direct_param_queue.h`; session structs from `src/runtime/graph/graph.h`
- P3: `src/ui/rendering/renderer_2d.{h,cpp}`, `deps/stb/stb_truetype.h`
- P5: `src/operator_api/gpu_common.h`, `operators/gpu/noise/noise.cpp`
- P6: `operators/audio/audio_analysis/audio_analysis.cpp` (RMS)

Foundation already borrowed: `src/runtime/gpu/gpu_context.{h,cpp}`, `deps/miniaudio/miniaudio.h`.
