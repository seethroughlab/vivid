# ADR-0009: Two Best-in-Class Surfaces + a Bridge; Native Reboot Borrows Classic Subsystems

Status: accepted

Date: 2026-06-27

Refines: [ADR-0002](ADR-0002-session-view-first.md), [ADR-0007](ADR-0007-node-graph-contextual-deep-view.md)

## Context

The earlier direction (ADR-0002 / ADR-0007) made one blended Session View the single primary
surface and treated the node graph as a contextual deep view. Disposable HTML prototyping of that
blended surface — grid, variation wells, perception layer, an audio↔visual bridge — was useful but
ultimately unsatisfying: forcing audio and visual authoring into one interface model fought the
grain of each domain.

A new framing tested better: **two native, best-in-class surfaces with a first-class bridge between
them.** A DAW (Ableton Session View inspiration) and a node-based visuals platform (TouchDesigner
inspiration), sharing one transport. The bridge is where parity lives — music characteristics become
data-source nodes in the visuals graph, and (later) visual characteristics become DAW modulators.
This honors the PRD's "audio-visual parity, *not* symmetry" better than a single surface did.

## Decision

1. **Two primary surfaces, not one.** The DAW Session View and the visuals node graph are *both*
   primary, each with the interface model its domain deserves. This shifts ADR-0002/0007: the node
   graph is a primary surface on the visuals side, not merely a deep view.
2. **The bridge is first-class and bidirectional.** Audio characteristics (level, transient, bands,
   notes) → data-source nodes driving visual parameters; visual characteristics (brightness, motion,
   colour) → modulators on DAW parameters. One shared master transport (ADR-0003 holds).
3. **Build a real macOS C++ reboot trunk**, borrowing **subsystems, not mental models**, from
   `vivid-classic` (Classic Lesson 10). Specifically: reuse the audio engine pattern (miniaudio),
   VST3 host, MIDI-clip scheduling, `GpuContext` (WebGPU), `Renderer2D`, and audio analysis — but
   NOT the operator registry / graph compiler / lane-value runtime the reboot set out to shed.
4. **Stack choices** (see the phased plan): WebGPU backend; shader operator authored in **GLSL**
   (compiled GLSL→SPIR-V→wgpu); reuse `Renderer2D` for 2D/text; **build a minimal custom node
   editor** rather than extract Classic's 17K-LOC `NodeGraphUI`.

## Alternatives Considered

- **Keep the single blended Session View** (ADR-0002 as-is). Rejected: prototyping showed it
  flattens the audio→binding→visual relationship and serves neither domain natively.
- **Reuse Classic's `NodeGraphUI`.** Rejected after sizing: ~17K LOC welded to Classic's compiled
  graph / inspector / dialogs; a ~2K-LOC custom editor on `Renderer2D` is cheaper and decoupled.
- **OpenGL/GLSL or a from-scratch GPU stack.** Rejected: reusing `Renderer2D` commits the window to
  WebGPU; GLSL authoring is preserved via a translation layer instead of an OpenGL backend.

## Consequences

- The product is now two surfaces + a bridge over a shared transport; UI work splits into a DAW
  Session View and a visuals node editor, both on `Renderer2D`.
- The native app is the proving ground for the reboot; its initial phased plan lives in
  [`../roadmap/native-implementation-plan.md`](../roadmap/native-implementation-plan.md). The direction is
  validated when the interactive audio→visual bridge works end to end.
- Borrowing from Classic stays explicit and narrow; the heavy runtime is left behind.
- The earlier disposable HTML prototypes remain as history, not the product direction.
