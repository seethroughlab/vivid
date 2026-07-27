# Native Instrument Candidate Pass

Date: 2026-07-26

Scope: small research pass for ADR-0034. Sources checked:

- `vivid-classic` branch audio operators and generated site metadata
- sibling package `../vivid-wavetable` (read-only; repo had unrelated local changes)

## Question

Should the ADR-0034 beginner path assume Surge XT, or should we port/ship a strong native instrument
from Vivid Classic or `vivid-wavetable` so the tutorial can avoid third-party plugins?

## Summary

Keep **Surge XT as assumed installed** for the public beginner path.

Do not port a classic instrument into core for ADR-0034. Classic has small useful synths and
samplers, but none appear strong enough to replace Surge XT as a beginner-facing musical voice
without either sounding like a test utility or pulling content/tooling weight into core.

Treat `vivid-wavetable` as the serious first-party/native instrument candidate, but keep it as a
package, not core. It already has the shape Vivid 4 wants: package manifest, assets, factory
presets, modules, tests, docs, and a curated showcase/instrument library. If Vivid needs a
"native" beginner instrument later, the better path is to make installing/using `vivid-wavetable`
excellent, not to fold its engine into core.

## Classic Candidates

### Oscillator

- Classic metadata: "Basic waveform oscillator with frequency and amplitude CV."
- Source size: ~122 lines.
- Strength: tiny, portable, useful as a signal/test source.
- Weakness: not a beginner instrument. No note input in the inspected source path, no musical voice
  quality beyond basic waveforms.
- Recommendation: utility/test/package primitive, not public beginner voice.

### FmSynth

- Classic metadata: "Two-operator FM synthesizer with ADSR envelope."
- Source size: ~343 lines.
- Strength: real polyphonic note-driven synth, ADSR, expression depths, semantic metadata, compact.
- Weakness: two-operator FM is useful but not broad or beginner-friendly enough to anchor public
  onboarding. It is likely to sound narrow without presets, effects, and curation.
- Recommendation: possible package or small future native utility, but not worth changing the
  ADR-0034 beginner assumption.

### Sampler / SP404

- Classic metadata:
  - Sampler: multi-group polyphonic sample player with ADSR and velocity control.
  - SP404: single-pad sampler with one-shot, loop, and gate playback.
- Source sizes: Sampler ~364 lines; SP404 ~622 lines.
- Strength: musically useful if bundled with good content. Classic also had sample assets such as
  SP404 kits and lapsteel articulations.
- Weakness: the instrument quality depends on sample-bank format, bundled/licensed content, browser
  UX, missing-asset recovery, and install size. This is more an onboarding content/package problem
  than a lean-core primitive.
- Recommendation: keep sampler capability as utility; do not make it the ADR-0034 beginner voice.

### GranularSynth

- Classic metadata: "Granular synthesis engine with up to 32 simultaneous grains."
- Source size: ~295 lines plus shared granular DSP.
- Strength: compelling creative texture engine.
- Weakness: effect/texture instrument, not a first beginner voice; depends on input/capture mental
  model and custom inspection.
- Recommendation: good creative package/showcase candidate, not core beginner instrument.

## `vivid-wavetable`

`vivid-wavetable` is the strongest native instrument candidate because it is already a Vivid 4
package rather than classic-era code.

Observed package surface:

- `vivid-package.json` with category `audio`, assets, site docs, operators, modules, tests.
- Operators: `WavetableLayer`, `WavetableOsc` (deprecated for new patches), `VoiceMixer`,
  `VoiceDrive`, `SubOsc`, `AnalogOsc`, `NoiseLayer`.
- Assets: factory wavetable WAV files.
- Factory presets and curated preset graphs.
- Modules: `LayerPad`, `DualWavetablePad`, `HybridKeys`, `SubAirPad`, `GlassInteractionKeys`.
- Tests: package manifest, DSP/audio correctness, module contract, factory assets, instrument
  metadata, graph fixtures.
- Docs identify `WavetableLayer` / `LayerPad` as the maintained production path.

Strengths:

- Real instrument quality and breadth: pads, keys, plucks, leads, basses, textures, arps.
- Current Vivid 4 architecture: native-note model, package manifests, modules, tests.
- Strong website fit: package docs, preview image, showcase/instrument library.
- Better aligned with "lean core + rich packages" than a core port.

Costs/risks:

- Larger engine surface: thousands of lines across layer renderer, wavetable bank, oscillators,
  assets, presets, and tests.
- Asset and preset management are part of the product experience.
- Installing/linking/rebuilding package must be beginner-safe before it can replace Surge in
  onboarding.
- Existing sibling repo had unrelated local changes during this pass, so do not mutate it from the
  website worktree.

Recommendation:

- Do not fold `vivid-wavetable` into core.
- Treat it as the first-party "native instrument package" candidate.
- Use it for a later tutorial after the Surge-based first path works.
- Add website/package work so `vivid-wavetable` can be presented as the native synthesis path:
  install, verify, load module/preset, inspect, map, save project.

## Decision For ADR-0034

For now:

- Beginner path assumes Surge XT.
- Core native audio remains utility/testing/routing unless a truly strong, low-maintenance
  instrument earns promotion.
- `vivid-wavetable` is the best candidate for a curated first-party package tutorial, not a core
  port.

Promotion rule for any future native instrument:

- It must sound good with bundled presets/assets.
- It must work from a signed build without source-build assumptions.
- It must have clear missing-asset/missing-package recovery.
- It must have tests, metadata, and at least one tutorial project.
- It must justify core residency over package residency.

