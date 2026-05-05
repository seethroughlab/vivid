# Operator-Editor Candidates

Follow-up to the four-phase operator-editors work (see parent `README.md`). DrumSequencer (phase 4) proved the platform; this folder enumerates the next operators that would meaningfully benefit from a dedicated editor window and sketches the approach for each.

Each doc opens with the interaction metaphor (the *why*), then drops into implementation detail (params, layout, keyboard/mouse, files). Docs are intentionally per-operator so they can be picked up and executed independently.

## Tiers

**Tier 1 — clear wins.** Dense grids or curves that the compact inspector cannot legibly host.

| Operator | Repo | Plan |
|---|---|---|
| Sequencer | core | [sequencer.md](sequencer.md) |
| Tracker | core | [tracker.md](tracker.md) |
| ParametricEQ | core | [parametric-eq.md](parametric-eq.md) |
| WavetableOsc | vivid-wavetable | [wavetable-osc.md](wavetable-osc.md) |

**Tier 2 — solid, not urgent.** Real UX improvement but the inspector is tolerable today.

| Operator | Repo | Plan |
|---|---|---|
| Arpeggiator | core | [arpeggiator.md](arpeggiator.md) |
| PatternSeq | core | [pattern-seq.md](pattern-seq.md) |
| Euclidean | core | [euclidean.md](euclidean.md) |
| ColorBands | core | [color-bands.md](color-bands.md) |
| Particles3D | vivid-3d | [particles3d.md](particles3d.md) |
| Material3D | vivid-3d | [material3d.md](material3d.md) |

## Recommended sequencing

1. **Sequencer first.** Smallest Tier-1 job (32 steps × 2 lanes, no patterns, no banks). Exercises the reusable editor UI vocabulary in a minimum-viable shape so that later adopters don't ship divergent versions.
2. **Extract shared helpers.** Before the second adopter lands, promote the reusable pieces out of `operators/control/drum_sequencer/drum_sequencer_editor_shared.{h,cpp}` — selection model, clipboard, grid hit-test, keyboard dispatch — into `operators/shared/editor_ui/` (or extend `operators/shared/sequencer/`, if the helpers are sequencer-specific). The infra plan called for this but deferred it past phase 4; the second grid editor is the forcing function.
3. **Tracker next.** Most ambitious grid; forces the helpers to handle many columns, scrolling, and text-cell editing. If the shared helpers survive Tracker, they're battle-tested.
4. **Arpeggiator, Euclidean, PatternSeq** in any order — each small, each reuses the grid vocabulary.
5. **ParametricEQ** (new idiom: draggable curve on a plane) and **ColorBands** (swatch row with pickers) introduce their own vocabularies — the grid helpers won't apply. Worth doing after the grid family so the divergence is deliberate.
6. **Sibling-repo work** (WavetableOsc, Particles3D, Material3D) can run in parallel with the core work once the shared helpers are published. Each of those operators lives in its own compile unit in its own repo, with no coupling to core editor adopters.

## Conventions carried from Phase 4

- **Retire the interactive mini-editor in the inspector** on every adopter — like DrumSequencer did when it deleted `drum_sequencer_inspector.cpp`. The dedicated editor becomes the sole interactive authoring surface. The inspector keeps a *passive preview* (existing `draw_thumbnail` is fine) and the host-provided "Open Editor" button, plus the default param list for top-level controls. Two interactive surfaces diverge fast and double the maintenance cost.
- Editor metadata (default + minimum window size, title) is declared via `editor_metadata()` on the operator core.
- `VIVID_EDITOR(OperatorName)` goes in the entry cpp alongside `VIVID_REGISTER`.
- Reusable drawing/interaction helpers live in a separate `_editor_shared.{h,cpp}` compilation unit per operator so tests can exercise them without a live runtime.
- Editors render via the host-provided `VividEditorContext` (draw ops + input events); no direct access to ImGui or the renderer.

## What this folder is not

- Not a commitment to ship all ten. Tier 2 should be reassessed against real user feedback after Tier 1 lands.
- Not a platform redesign. If an operator's needs push outside the current `VividEditorContext` surface (e.g., embedding a live 3D scene viewport for a material preview), that's a platform extension to scope separately, not something to smuggle into an adopter.
