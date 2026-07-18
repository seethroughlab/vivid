# Vivid Native Reboot — Phased Implementation Plan

Status: **COMPLETE** — P0–P8 validated on hardware (2026-06-28). See
[ADR-0009](../decisions/ADR-0009-two-surface-bridge-and-native-reboot.md) for the pivot rationale. Code
lives under `app/`.

A real macOS C++ implementation of **two best-in-class surfaces + a bridge** — a DAW (Ableton
Session View) and a node-based visuals platform (TouchDesigner), sharing one transport — leaning on
`vivid-classic` for battle-tested subsystems (not its runtime). Each phase is a committable, runnable
milestone ending in a concrete proof, ordered to de-risk the hard parts early.

**Verification reality:** the dev sandbox has no display/audio, so phases are verified to
**compile + link** there; the audible/visible proofs are run on a Mac
(`cmake --build app/build && ./app/build/vivid`).

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
| **P1b — MIDI clips** ✅ | Minimal `MidiClip` + transport-locked `ClipScheduler` (fmod playhead, sample-offset note-on, deferred note-off) | a MIDI clip plays through the synth |
| **P2 — Session model + launch** ✅ | Session = instrument + N clips; bar-quantized launch (atomic queue, audio-thread swap, note flush) | keys/clicks launch clips → audio changes |
| **P3 — `Renderer2D`** ✅ | Extracted `Renderer2D` + `stb_truetype` + JetBrainsMono; text/rects/lines; GLFW input | a HUD renders; clicks reported |
| **P4 — Session grid UI** ✅ | Session View on `Renderer2D` (transport, track, clip cells, hover/active/queued); click → launch | **Proof A:** click a clip → hear it |
| **P5 — Shader operator** ✅ | GLSL fragment compiled **natively by wgpu-native** (`WGPUShaderSourceGLSL`, no glslang) into a viewport pass | a GLSL shader renders live |
| **P6 — Analysis → shader** ✅ | RMS on master output → `Transport.level` → shader `u_reactive` | **Proof B:** shader reacts to audio |
| **P7 — Node editor** ✅ | ~250-LOC custom editor on `Renderer2D`: nodes, bezier wires, drag-to-connect; the wire gates reactivity | the visuals graph is editable |
| **P8 — The bridge** ✅ | Click a track characteristic (level / transient) → spawn a data node → wire to the shader | **the audio→visual bridge, end to end** |

## Outcome

**Native direction validated at P8** and verified interactively on hardware. Notable as-built findings:

- **GLSL needs no toolchain** — wgpu-native ingests GLSL natively (`WGPUShaderSourceGLSL`), so the
  GLSL-authoring decision cost zero extra deps (no glslang/shaderc). The plan's `glslang` assumption
  was unnecessary.
- **The node editor was a ~250-LOC build**, not the feared extraction — building on `Renderer2D`
  rather than adapting Classic's 17K-LOC `NodeGraphUI` was the right call.
- **`Renderer2D` and `GpuContext` extracted almost verbatim**; only `vst3_host_common.h` needed real
  surgery (and even that only swapped two Vivid-internal includes).
- The "borrow subsystems, not mental models" approach (Classic Lesson 10 / ADR-0009) held: we reused
  the hard parts (audio engine, VST3 host, WebGPU, 2D renderer, font atlas) and left the runtime.

P9 (JSON persistence, threading hardening, VST3 plugin-GUI windows) and deepening the bridge were
the next tranche of product work.

## Classic subsystems borrowed (by phase)

- P1a: `operators/shared/vst3_host/vst3_host_common.h`, `vst3_vstiids.cpp`, `plugin_common/base64.h`
- P1b: `operators/control/midi_clip/midi_clip_core.h`, `src/operator_api/note_types.h`, `operators/shared/sequencer/note_helpers.h`
- P2: `operators/shared/plugin_common/direct_param_queue.h`; session structs from `src/runtime/graph/graph.h`
- P3: `src/ui/rendering/renderer_2d.{h,cpp}`, `deps/stb/stb_truetype.h`
- P5: `src/operator_api/gpu_common.h`, `operators/gpu/noise/noise.cpp`
- P6: `operators/audio/audio_analysis/audio_analysis.cpp` (RMS)

Foundation already borrowed: `src/runtime/gpu/gpu_context.{h,cpp}`, `deps/miniaudio/miniaudio.h`.
