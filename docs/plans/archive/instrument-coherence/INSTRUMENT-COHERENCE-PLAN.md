# Instrument Coherence Additions for Vivid Core

## Summary
`vivid-wavetable` is now a strong package-side synth voice: lane-aware oscillators, sub/noise/drive layers, conditioned interaction modes, and a curated reference library. The next gap versus a finished instrument like Ableton Live Wavetable is not mainly “more oscillator tricks.” It is **instrument coherence**.

Ableton Wavetable feels complete because it combines dual-oscillator voicing, strong filter routing, quick modulation assignment, custom wavetable workflow, and performance-ready control in one readable instrument surface. The best Vivid-core additions are the generic platform pieces that let packages present that same kind of coherence without moving package-owned synth logic back into core.

This document describes the core additions that would most help Vivid host instrument-like packages such as `vivid-wavetable`.

> Archived note (2026-04-05): This directory is historical context for the shipped `v1` instrument-coherence work. The live deferred backlog now lives in [docs/plans/ROADMAP.md](../ROADMAP.md). Any “canonical source of truth” wording below is historical and superseded by that roadmap backlog.

What this plan is not:

- not a request to move `vivid-wavetable` operators back into core,
- not a full Serum/Pigments-style modulation-matrix project,
- not a push toward unrelated new synthesis engines,
- not a package-specific shortcut that only helps one repo.

## Current Status

| Step | Status | Implemented | Deferred Follow-Up |
| --- | --- | --- | --- |
| [Step 1](./INSTRUMENT-COHERENCE-PLAN-STEP-1.md) | `v1 shipped` | Module-file-first subgraph instruments with exposed controls, presets, and source-module editing | Embedded subgraph authoring, peek-inside visualization, nested module editing |
| [Step 2](./INSTRUMENT-COHERENCE-PLAN-STEP-2.md) | `v1 shipped` | Composite-local modulation sources, destinations, assignments, and lowering | Richer curves and scaling, assignment gestures, broader lane-aware authoring helpers |
| [Step 3](./INSTRUMENT-COHERENCE-PLAN-STEP-3.md) | `v1 shipped` | `DualFilter` with serial/parallel/split routing and lane-aware poly behavior | Stereo-aware variants, tap outputs, routing morphing, broader filter-flavor expansion |
| [Step 4](./INSTRUMENT-COHERENCE-PLAN-STEP-4.md) | `v1 shipped` | Wavetable-first asset import, indexing, metadata, and cache lifecycle | Richer asset picker UI, broader asset kinds, `asset_id` persistence, batch library workflows |
| [Step 5](./INSTRUMENT-COHERENCE-PLAN-STEP-5.md) | `v1 shipped` | `MidiInput` expressive-note lanes plus module performance-page metadata | Richer performance widgets, modulation/performance UX integration, broader controller-learn workflows |
| [Step 6](./INSTRUMENT-COHERENCE-PLAN-STEP-6.md) | `v1 shipped` | Instrument-oriented graph metadata and unified browser filtering | Richer browser grouping, preview-control authoring UI, favorites/curation, tighter performance-page/browser integration |

The detailed design for each shipped `v1` slice and each deferred follow-up remains in the linked step docs in this archive. The live deferred backlog has been consolidated into [docs/plans/ROADMAP.md](../ROADMAP.md).

## 1. Graph Encapsulation With Exposed Controls
The single highest-leverage addition is a core subgraph/composite instrument system that lets a graph-based synth voice behave like one instrument.

### Why it matters
Today, Vivid is excellent at modular graph composition, but a polished synth package still looks like many operators plus a lot of glue. That makes it harder to:

- present a small instrument-level control surface,
- hide implementation detail without losing graph flexibility,
- ship instrument-like presets instead of only graph presets,
- make package reference voices feel like coherent products.

### Core capability
Add a graph encapsulation system that supports:

- wrapping an internal graph or graph slice as one reusable instrument/composite,
- exposing a small set of named public controls,
- grouping those controls into readable sections,
- storing defaults and inspector metadata on the exposed surface,
- preserving the existing graph internally rather than inventing a separate synth architecture.

### Boundaries
- Keep the internal graph model intact.
- Keep exposed controls generic so this helps many packages, not just wavetable synthesis.
- Do not require packages to stop shipping normal graphs.

## 2. Lightweight Modulation Assignment Layer
Vivid needs something between “wire everything manually” and “build a huge modulation matrix.”

### Why it matters
Packages like `vivid-wavetable` can already do sophisticated modulation, but it is graph-first and often patch-fragile. A finished instrument usually offers a fast way to say:

- this envelope should affect brightness,
- this macro should control motion,
- this performance control should influence interaction depth.

### Core capability
Add a reusable modulation-assignment primitive with:

- named sources,
- named destinations,
- bipolar or unipolar amounts,
- optional scaling or curve shaping,
- inspector visibility,
- serialization that remains readable and compatible with the graph model.

This should be intentionally smaller than a full DAW/synth modulation matrix. The goal is fast instrument-level modulation assignment, not maximal routing complexity.

### Boundaries
- Keep normal graph wires as the primary routing model.
- Avoid introducing a new giant abstraction that touches every layer at once.
- Reuse existing inspector/runtime serialization patterns where possible.

## 3. Stronger Reusable Synth Filter Platform
One of the biggest remaining gaps between Vivid-hosted synth packages and a finished wavetable instrument is the filter story.

### Why it matters
Ableton Wavetable feels complete partly because filter routing is not an afterthought. Dual-filter behavior, musical routing choices, and predictable tone shaping are central to the instrument experience.

`vivid-wavetable` currently relies on graph assembly around existing core filters. That works, but it still feels more like modular construction than an intentional synth voice platform.

### Core capability
Add a more reusable synth-oriented filter platform in core, with support for:

- lane-aware polyphonic behavior,
- serial / parallel / split routing modes,
- multiple musical filter flavors,
- optional built-in drive/saturation at the filter stage,
- stable integration with package-owned poly voices.

This does not need to be “the Ableton filter clone.” It needs to be a generic core filter platform that makes synth packages easier to author coherently.

### Boundaries
- Keep package voicing decisions package-owned.
- Do not replace the current graph model with a hard-coded synth signal path.
- Prefer one strong reusable filter/routing primitive over many narrow one-off operators.

## 4. Reusable Custom Asset Workflow for Wavetables
Custom wavetable content should be supported by core as a first-class asset workflow, even if the sound-design logic stays package-owned.

### Why it matters
User content is a big part of what makes modern wavetable instruments feel alive. Packages can own wavetable synthesis behavior, but the platform should help with:

- asset import,
- normalization and analysis,
- caching,
- indexing,
- browsing and preview metadata.

### Core capability
Add generic content plumbing for importable package assets, with a concrete first target of custom wavetables:

- import path from files into a known package/user asset location,
- consistent indexing and metadata,
- optional preview/analysis support,
- cache lifecycle that survives rebuilds and restarts.

This should not hard-code wavetable synthesis behavior into core. It should provide the asset infrastructure packages can rely on.

### Boundaries
- Package-specific wavetable voicing remains package-owned.
- Do not tie the design to one file format unless necessary.
- Prefer generic package asset infrastructure with a strong wavetable use case.

## 5. Per-Note Expression and Performance Plumbing
Vivid’s control platform should do more to support expressive instrument play.

### Why it matters
Ableton explicitly positions Wavetable as modulation-rich and MPE-friendly. Vivid currently has useful MIDI inputs, but its expressive instrument-control story is still thin compared with a modern synth host.

### Core capability
Add better performance-expression plumbing, especially for:

- per-note expression / MPE-style lanes,
- pitch/mod/expression conventions that packages can read consistently,
- instrument-ready macro/performance pages for exposed controls,
- stable routing of expressive data into lane-aware package operators.

### Boundaries
- Keep this generic across packages.
- Do not require every package to reinvent expressive input handling.
- Avoid bolting per-note expression onto package graphs in an ad hoc way.

## 6. Instrument-Oriented Preset and Browser Support
The browser and preset layer should better distinguish between raw graphs and polished instrument-ready content.

### Why it matters
Even when the engine is strong, the experience still feels less complete if the platform treats every graph equally and offers no instrument-level browsing language.

### Core capability
Extend core graph/preset metadata and browser support for instrument content:

- category/family tags,
- hero/reference distinctions where useful,
- exposed-control snapshots,
- preview/playability metadata,
- cleaner grouping for package instrument libraries.

This would help packages like `vivid-wavetable` ship a more coherent instrument library without abandoning graph-native content.

### Boundaries
- Keep metadata additions compact and reusable.
- Do not force a package into one browser taxonomy.
- Preserve ordinary graph browsing for non-instrument use cases.

## Priority Order
If these core additions are staged, the best order is:

1. graph encapsulation with exposed controls,
2. lightweight modulation assignment,
3. stronger reusable synth filter platform,
4. reusable custom asset workflow for wavetables,
5. per-note expression and performance plumbing,
6. instrument-oriented preset/browser support.

This order favors the pieces that most directly change how a package feels as an instrument rather than only how many features it technically has.

## Validation Strategy
Each addition should be validated with package-backed instrument scenarios, not only core unit tests.

Core acceptance should include proving that a package like `vivid-wavetable` can:

- wrap a polyphonic synth graph as a coherent instrument surface,
- expose a small set of named controls without losing graph flexibility,
- assign a few named modulation sources without dense graph clutter,
- build a stronger filter-centered voice without package-specific hacks,
- load and reuse custom wavetable assets through a stable content path,
- respond to expressive performance data in a lane-aware way,
- present instrument-ready presets distinctly from raw graph examples.

No addition is successful if it only increases abstraction while making package authoring harder.

## Assumptions and Defaults
- The goal is to make Vivid a better host for instrument-like packages, not to absorb those packages into core.
- The comparison target is “closer to Ableton Wavetable as a coherent instrument,” not full Serum/Pigments breadth.
- Existing graph routing should remain central; new core features should reduce friction, not replace graph thinking.
- The best additions are reusable Vivid primitives that help multiple packages, with `vivid-wavetable` as the clearest motivating example.
