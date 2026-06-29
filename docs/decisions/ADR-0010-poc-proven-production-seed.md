# ADR-0010: The C++ PoC Is Proven; Promote It to the Production Seed

Status: accepted

Date: 2026-06-28 (accepted 2026-06-29)

Follows: [ADR-0009](ADR-0009-two-surface-bridge-and-cpp-poc.md)

Superseded-in-part-by: [ADR-0011](ADR-0011-poc-to-product-architecture.md) — the productization target
(extensible + cross-platform) and the trunk question (grow the PoC vs. port onto vivid-classic's runtime)
are decided there.

## Context

ADR-0009 committed to two best-in-class surfaces plus a first-class bridge, and to a real macOS
C++ proof of concept that borrows *subsystems, not mental models* from `vivid-classic`. It declared
the PoC "proven when the interactive audio→visual bridge works end to end."

That bar is now cleared, and then some. Across 67 commits (Sprints 1–8 / P9–P28) the PoC became a
genuinely interactive two-surface instrument:

- **DAW surface** — multi-track Session View: Pigments / Serum / EZdrummer instruments + a warped
  audio-loop sampler track, per-track instrument→effect VST chains, live MIDI piano-roll and audio
  waveform clip editors, mixer, clip-grid content previews, JSON save/load with plugin-state
  persistence.
- **Visuals surface** — a rewireable node graph (GLSL plasma / video / feedback / blur ops, each
  rendering into its own `RenderTarget`, terminating in an `Output` node), with classic-style
  affordances: left-in/right-out ports, grid + canvas panning, a Tab operator chooser, and live
  per-node thumbnails.
- **The bridge** — a single string-keyed `MappingRegistry { source, dest, amount }` carries audio
  characteristics (level, transient, low/mid/high band energy) into visual parameters **and**
  visual state back into audio parameters, with one overview panel (M) to see and prune every
  mapping. The bridge is bidirectional and legible, exactly as ADR-0009 envisioned.

The PoC was deliberately built in production-grade C++, not throwaway HTML. The open question is no
longer "does the approach work" — it does — but "what do we carry forward, and how."

### What the PoC taught us

1. **The mapping registry is the keystone.** Collapsing `char_id` + per-port wiring into one
   `{source, dest, amount}` table (string IDs like `track_2.transient`, `uniform.warp`,
   `param:0:1:3`) made the forward bridge, the return path, and the overview fall out of *one* model
   for near-free. This is the single most important production foundation.
2. **Borrowing subsystems, not mental models, was correct.** miniaudio, the VST3 host
   (`vst3_host_common.h` — `cache_params` / `getState` / `setState` / effect-capable bus
   activation), `Renderer2D`, and native GLSL→wgpu all transplanted cleanly. Effects came nearly
   free because the host already supported them. The ~2K-LOC custom node editor was the right call
   over extracting Classic's ~17K-LOC `NodeGraphUI`.
3. **Model the visuals as a rewireable DAG from day one.** The first cut hardcoded
   plasma→feedback→blur; the user immediately wanted "a chain with an Output node." The redesign to
   input-edge nodes + a per-node `RenderTarget` executor is the production seed — and it should have
   been the starting point.
4. **Classic node-editor affordances are table stakes, not polish.** Each request — ports on the
   left, grid + pan, the Tab chooser, thumbnails — was the user reaching for a convention they
   already trust. The custom editor is cheap to build but must keep pace with Classic's affordance
   set to feel real.
5. **`Renderer2D` has no textured-quad path.** Live thumbnails had to be a *separate* GPU blit pass
   (the insight lifted from Classic's `ThumbnailRenderer`); the 2D renderer only draws its glyph
   atlas. Production must decide: extend `Renderer2D` with texture support, or keep image content as
   a distinct pass.
6. **One thread-safety pattern covers live edits.** Generation-counter + mutex + retired-list for
   structural edits (clips, FX chains) and a lock-free SPSC queue for param delivery kept the audio
   thread allocation-free under live editing. Reusable as-is.
7. **macOS hosting gotchas are now documented.** A `.app` bundle plus a `CFRunLoopTimer`-driven
   frame loop is required for hosted plugin-GUI interactivity; effect plugins need explicit audio-bus
   activation. These cost real debugging and are worth not re-discovering.

## Decision

**Promote the PoC codebase (`app/`) to the seed of the production product**, rather than treating it
as disposable or restarting from the reboot-docs track. Concretely:

1. **Carry forward as load-bearing foundations** (harden in place, don't rewrite):
   - the `MappingRegistry` model and its persistence;
   - the shared master transport (ADR-0003);
   - the VST3 host + multi-track session engine and the thread-safe live-edit pattern;
   - the visuals graph executor (op-nodes, `RenderTarget` per node, Output terminal).
2. **Treat the deliberately-minimal parts as known debt to be hardened**, not as design (see
   Consequences): per-op-type global params, single Output node, no zoom / view persistence, the
   stretch-to-pane thumbnail containment, and partial persistence coverage.
3. **Keep the borrowing from Classic explicit and narrow.** Continue lifting *subsystems*
   (`ThumbnailRenderer`-style passes, analysis, decoders) on demand; do not pull in Classic's
   operator registry, graph compiler, or lane-value runtime — the reboot still sheds those.
4. **Defer the production node-graph affordance question to its own ADR** once we know how far the
   minimal editor must scale (it may, or may not, eventually justify a heavier framework).

## Alternatives Considered

- **Keep the PoC disposable; rewrite production from scratch on the learnings.** Rejected: the PoC
  is already production-grade C++ with the right subsystem boundaries; a clean-room rewrite would
  re-pay costs (VST3 hosting, the bridge model, the graph executor) we have already paid. Refactor
  in place instead.
- **Freeze the PoC and resume the `vivid-4` reboot docs/HTML track.** Rejected: the PoC *is* the
  validated direction now; the disposable prototypes remain history (per ADR-0009), not the product.
- **Adopt the PoC wholesale with no hardening pass.** Rejected: several shortcuts (below) are PoC
  scaffolding, not decisions, and would calcify into the product if not named and addressed.

## Consequences

- **Positive:** the validated subsystems become the product's spine; the bidirectional bridge — the
  thing ADR-0009 set out to prove — already exists and is legible. Momentum carries directly into
  production rather than restarting.
- **Positive:** the single mapping model gives modulation, the return path, and the overview from one
  abstraction, and is the natural place to later add curve/polarity/range (Classic's
  `ModAssignmentDef` shape).
- **Cost / debt to schedule** (PoC scaffolding to harden before it calcifies):
  - visual-op params are **global per op-type**, not per-node — needs per-node param storage;
  - exactly **one `Output` node** and no graph zoom / view-state persistence;
  - thumbnails are contained by **skip-if-off-pane**, not a real clip-rect on the blit pass;
  - **persistence gaps** (view/pan offset, some device-chain detail);
  - `Renderer2D` **texture support** is unresolved (thumbnails remain a side pass until then);
  - the mapping registry stores only `amount` — **no curve/polarity/range** yet.
- **Follow-up:** a production-readiness pass that converts the debt list into tracked work; a future
  ADR on the node-graph affordance ceiling; continued *narrow* borrowing from Classic.

## Status note

**Accepted (2026-06-29).** The PoC is the seed; its ADR-0010 debt list was retired in full this
session. The productization target and the trunk question (keep growing `app/` vs. port the product
layer onto vivid-classic's runtime) are taken up in [ADR-0011](ADR-0011-poc-to-product-architecture.md)
with the [PoC → Product roadmap](../roadmap/poc-to-product.md).
