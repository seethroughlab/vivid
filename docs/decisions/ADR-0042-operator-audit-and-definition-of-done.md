# ADR-0042: Operator Audit — a Per-Operator Definition of Done + Audit Harness

Status: proposed

Date: 2026-07-30

> **Baseline audit run (2026-07-30).** The harness this ADR proposes was built and run against the whole
> catalog (72 registered ops). Result: **9 PASS · 29 WARN · 14 needs-input · 2 FAIL · 18 audio**. The two
> genuine defects are **`Switch3D`** (blank node thumbnail — neither thumbnail path is implemented) and
> **`SpikeSolid`** (a dev/spike op that renders blank). The 14 "needs-input" ops (signal consumers, mesh/
> asset/video ops, authored-text ops) can't be exercised by a synthetic scaffold and are expected, not
> defects. The 29 WARNs are ops that render + thumbnail fine but have parameters that showed **no visible
> change** in the single-frame test graph — many legitimate (rotating a symmetric sphere, materials on a
> small object), a few worth a look. Node thumbnails come from **two undocumented mechanisms** —
> `draw_thumbnail(VividThumbnailContext)` (CPU 2D, audio ops) and rendering a preview into the op's own
> `output_texture_view` during `process_gpu` (every vivid-3d scene op, LaneRamp, Clock, Notes); the ~18
> audio ops are name-only cells (partly a structural limit — the thumbnail context excludes GPU/sample/live
> data). Before this pass there was **no per-operator render smoke test, no check that a parameter affects
> output, and no per-operator performance signal**. Full report: `tools/operator_audit/reports/`.

## Context

Vivid deliberately ships a **small, lean operator catalog** (ADR-0041 chose "port-and-integrate, not
build-from-scratch" and explicitly deferred porting the full `vivid-classic` back-catalog). The bet is
quality over quantity: a handful of composable, generative operators, each excellent. But "each excellent"
is currently an aspiration with no instrument behind it.

1. **Thumbnails are inconsistent, and the mechanism is undocumented.** Two independent paths exist:
   (A) an operator overrides `virtual draw_thumbnail(const VividThumbnailContext*)` (`operator.h:362`) and
   draws with the CPU 2D `VividDrawAPI` — `VIVID_REGISTER` emits the `vivid_draw_thumbnail` export (ABI
   v14); this is how audio-node and session-grid cells are drawn. (B) A visual op renders a small preview
   into its own `output_texture_view` during `process_gpu`, and the node card simply blits that texture
   (`visual_graph.cpp:420`). Path B is why a *stubbed* `draw_thumbnail` is usually **not** a missing
   thumbnail. This dual mechanism is nowhere documented; it caused the whole `vivid-3d` package to carry
   `TODO(ADR-0041 Phase 1): reimplement against trunk 2D VividDrawAPI` stubs that are, in fact, already
   covered by path B. The genuine gaps: **`Switch3D`** (a passthrough selector — no preview on either
   path) and audio ops whose thumbnail context lacks the data to draw anything meaningful.

2. **No per-operator correctness or parameter verification.** The C++ test suite (`app/tests/`) is
   deliberately headless, GPU-free, audio-free — it can *instantiate* an op runtime (`gpu/op_runtime.cpp`,
   `test_op_registry.cpp`) and validate its descriptor structurally (`operator_descriptor_validation.cpp`,
   `semantic_vocab.h`, both CI-gated) but it **cannot render a visual op**, and there is **no check that
   changing a parameter changes the output**. A dead param, a mis-wired uniform, or a param that only
   matters with a specific input connected would all pass today's checks silently.

3. **No per-operator performance signal.** The only runtime measurement is the whole-frame FPS HUD
   (`frame.cpp:161`). There is no per-node attribution, so "truly optimized" is unfalsifiable per operator.

4. **The tooling to close all three already exists** and has never been aimed at the catalog: the control
   server can build a graph and read back pixels (`add_node` / `connect_nodes` / `set_active_output` /
   `set_node_param` → `capture_frame` → `image_analysis_tools::analyze_rgba` → `{is_blank, brightness,
   contrast, activity, hash}` + `hash_hamming`); the showcase QA harness
   (`examples/demos/showcase/{runner,gates,registry}.py`) is a working PASS/WARN/FAIL per-graph validator;
   and MCP `list_operators()` yields the full catalog with params + ports for automated fan-out.

## Decision

Adopt a **per-operator Definition of Done (DoD)** and build a **repeatable audit harness** that measures
the catalog against it, run first as an **advisory report**.

1. **Definition of Done — four dimensions.** An operator is "done" when:
   1. **Thumbnail** — it has a rich, dynamic node card. *Visual ops:* never blank (path A or path B).
      *Audio generators/modulators* (LFO, ADSR, Arp, Euclid, Chord, …): draw a meaningful shape from the
      read-only param snapshot (`VividThumbnailContext.param_values`). *Pure DSP effects* (Bitcrush,
      Filter, and the glitch pack): a name-only cell is acceptable — an **explicit, listed exemption**,
      because their context carries nothing static worth drawing. No ABI change is taken to lift this limit
      in this pass (see Alternatives).
   2. **Renders / works** — a minimal per-operator test graph produces **non-blank, plausible** output
      (`analyze_rgba` reports not `is_blank`); audio ops satisfy their existing behavioral contract
      (notes/audio out, RT-safe per `docs/operator-authoring/README.md`).
   3. **Parameters affect output** — every non-editor, non-transient parameter, swept across its declared
      range (and across time for time-dependent ops), **measurably changes** the output (`hash_hamming > 0`
      or a brightness/contrast delta beyond noise). A param that never moves the output is flagged as a
      defect (dead param / mis-wired uniform / needs-a-specific-input).
   4. **Performance** — the operator's per-frame cost is recorded against the headless 1080p/60fps budget
      (ADR-0041), and hotspots (e.g. `SDF3D` raymarch) are flagged.

2. **Build the audit harness** (`tools/operator_audit/`) — a Python tool driving the running app over the
   control server, reusing the `Vivid` client (`examples/demos/vivid_demo.py`) and the showcase-runner
   pattern. It enumerates `list_operators()`, builds a minimal test graph per operator from a small set of
   per-category **scaffolds** (chosen by the op's ports), routes each op to the active Output, and runs the
   four DoD checks, emitting per-op JSON records + a console PASS/WARN/FAIL table (mirroring
   `examples/demos/showcase/reports/`). Robustness rules: a param that cannot change a single static frame
   → **WARN** (not FAIL); an op whose required inputs a scaffold can't supply → **WARN** with a reason.
   Audio ops get a lighter lane (instantiate + behavioral contract, `test_generator_ops.cpp`-style, plus
   the thumbnail presence/exemption check) since they cannot be GPU-captured.

3. **Add one small engine affordance for the perf dimension:** a read-only control endpoint `get_perf`
   returning `{fps, frame_ms}` from the same EMA the FPS HUD already computes, so the harness can measure a
   per-operator frame-time via an A/B delta (scaffold-without-op vs scaffold-with-op). This is the **only**
   engine code change in this pass; a matching MCP tool keeps parity (`mcp/tests/test_mcp_parity.py`).

4. **Advisory first, gate later.** The harness runs on demand and produces a report; it is **not** wired
   into CI yet. Once the catalog is green and the checks are proven stable, promote the render + param +
   thumbnail checks into `production-gate-pr.yml`.

5. **Document the dual thumbnail mechanism** (path A vs path B) in `docs/operator-authoring/` so the
   "stubbed `draw_thumbnail`" confusion does not recur, and record the audio-effect thumbnail exemption
   list there.

## Alternatives Considered

- **Golden pixel-diff baselines per operator.** *Deferred.* Exact-pixel baselines are brittle across GPU
  drivers and time-dependent ops. The perceptual `average_hash` + `hash_hamming` + `is_blank`/contrast
  signals already in `image_analysis_tools.cpp` are robust enough for an advisory pass; a golden-hash
  baseline can be promoted later if regression coverage demands it.
- **Per-node GPU timestamp queries** (`wgpuQuerySet`/`writeTimestamp`) around each node in
  `visual_graph.cpp::run_chain`. *Deferred.* Accurate, but a real engine change. The A/B frame-time delta
  via `get_perf` is coarser but sufficient to flag hotspots now; timestamps become Phase 3 if the A/B
  signal proves too noisy.
- **Extend the ABI (v15, additive) to give audio thumbnails live/sample data** (Sampler waveform, live LFO
  phase, meter-driven effect cells). *Deferred.* Genuinely richer, but an ABI bump + host plumbing for a
  cosmetic gain. This pass takes "draw what the param snapshot allows" and exempts pure effects instead.
- **Wire the checks into CI immediately.** *Rejected for now.* The catalog is not yet green; a hard gate
  would red the build while we are still fixing findings. Advisory-first, then gate.
- **A pure C++ headless test per visual operator.** *Rejected.* The headless suite has no GPU device by
  design; rendering an operator requires the live app. The control-server harness is the right layer.

## Consequences

- **Positive.** The catalog gets a single, repeatable instrument for the four things that make an operator
  trustworthy. The baseline report becomes a prioritized fix-list. The dual thumbnail mechanism stops
  being tribal knowledge. `list_operators()`-driven fan-out means new operators are audited for free.
- **Tradeoff.** The harness depends on the **running app** (a live GPU), not the headless CI — so until
  Phase 4 it is a developer/agent tool, not a merge gate. It also depends on the control-server API surface
  staying stable (already the case; MCP parity is guarded).
- **Tradeoff.** Per-category scaffolds need a few per-op overrides for operators with unusual required
  inputs; the harness must degrade to WARN (not crash/FAIL) when it can't construct a valid graph.
- **Follow-up.** The report will name concrete defects (starting with `Switch3D`'s blank thumbnail) to fix
  in Phase 2.

### Implementation phases

- **Phase 1 (this pass).** Write this ADR; add the `get_perf` endpoint (+ MCP parity); build
  `tools/operator_audit/` (`scaffolds.py`, `audit.py`, `reports/`); run the **baseline** across the whole
  catalog and commit the report. No operator fixes yet.
- **Phase 2 — fixes, prioritized by the report.** `Switch3D` thumbnail (path B render into its output
  texture); any dead/mis-wired params the param check surfaces; audio generator/modulator thumbnails drawn
  from the param snapshot; document the dual mechanism + effect-exemption list.
- **Phase 3 — perf depth.** If the A/B frame-time is too coarse, add per-node GPU timestamp queries in
  `run_chain`; set explicit per-operator budgets.
- **Phase 4 — enforcement.** Promote the render + param-affects-output + thumbnail checks into
  `production-gate-pr.yml` once the catalog passes reliably.

## References

- Planning blueprint: `~/.claude/plans/quick-detour-the-parameter-lucky-teacup.md`
- ADR-0041 (procedural 3D scene graph; operator philosophy, 1080p/60fps budget, `draw_thumbnail` stubs)
- ADR-0026 (evaluation asserts quality against intent — the doctrinal anchor for a "definition of done")
- ADR-0016 (shaders-are-content), ADR-0025 (C++17 organization / composability)
- Reused code: `app/src/cli/control_handlers_visual_analysis.cpp` (`capture_frame`/`compare_frames`),
  `app/src/cli/image_analysis_tools.cpp` (`analyze_rgba`/`average_hash`/`hash_hamming`),
  `examples/demos/showcase/{runner,gates,registry}.py` (QA harness pattern),
  `app/src/operator_api/{operator.h,types.h}` (Param system, ABI v14, `VividThumbnailContext`),
  `mcp/vivid_mcp.py` (`list_operators`)
