# Phase 3 — Domain Pipelines And Cross-Domain Behavior

## Scope Reviewed

Primary domain and cross-domain surfaces reviewed in this phase:

- audio-domain execution and hot reload behavior
- control/audio bridge operators and spread transport
- media decode/load/clock/session behavior
- MIDI parsing and MIDI runtime sequencing
- representative synth/effect operator-domain behavior
- semantic-tag metadata as a product-surface contract for tooling
- deferred GPU-domain repro for `rich_text_demo.json`

This phase focused on whether domain pipelines produce the right behavior, not just whether the runtime stays alive.

## Evidence Gathered

### Automated Phase 3 evidence bundle

Ran:

```bash
ctest --test-dir build --output-on-failure -R "test_audio_engine|test_audio_hot_reload|test_audio_float_snapshot|test_audio_robustness|test_audio_dsp_api|test_audio_domain_sequencer|test_midi|test_midi_file_parser|test_midi_file_player|test_media_headless|test_media_clock|test_media_session_queue|test_movie_decode_route|test_movie_decode_upload|test_movie_load_async|test_movie_load_generation|test_movie_long_loop_sync|test_cross_domain_spread|test_audio_spread_wire|test_spread_broadcast|test_signal_port|test_synth_transform_ops|test_latency_validation|test_capture_coordinator|test_semantic_tags"
```

Observed result:

- 24 of 25 matched tests passed
- the only failing lane was `test_semantic_tags`

Passing lanes:

- `test_audio_engine`
- `test_audio_hot_reload`
- `test_audio_float_snapshot`
- `test_audio_robustness`
- `test_audio_dsp_api`
- `test_audio_domain_sequencer`
- `test_midi`
- `test_midi_file_parser`
- `test_midi_file_player`
- `test_media_headless`
- `test_media_clock`
- `test_media_session_queue`
- `test_movie_decode_route`
- `test_movie_decode_upload`
- `test_movie_load_async`
- `test_movie_load_generation`
- `test_movie_long_loop_sync`
- `test_cross_domain_spread`
- `test_audio_spread_wire`
- `test_spread_broadcast`
- `test_signal_port`
- `test_synth_transform_ops`
- `test_latency_validation`
- `test_capture_coordinator`

### Focused semantic-tag triage

Ran:

```bash
ctest --test-dir build --output-on-failure -R "test_semantic_tags"
```

Observed result before cleanup:

- failed with 15 invalid semantic tags

Failing tag groups observed:

- generic/non-vocabulary tags:
  - `mode`
  - `pitch_class`
  - `coefficient`
  - `ratio`
- wrong core-vocabulary variants:
  - `amplitude_db` (spec uses `gain_db`)
  - `pan_lr` (spec uses `pan`)
  - `time_offset_ms` (spec uses `time_milliseconds`)
- unsupported resource-path tags that should be namespaced or left untagged in v1:
  - `path_midi`
  - `path_lut`

Affected operators before cleanup included:

- `Quantizer`
- `SampleHold`
- `AudioAnalysis`
- `FmSynth`
- `MidiFilePlayer`
- `Compressor`
- `Limiter`
- `StereoPanWidth`
- `LutApply`
- `MovieLoaded`

Follow-up validation after metadata cleanup:

```bash
ctest --test-dir build --output-on-failure -R "test_semantic_tags"
```

Observed result:

- `test_semantic_tags` now passes cleanly

### GPU sidecar follow-up

Ran:

```bash
./build/test_demo_graphs ./build/graphs rich_text_demo
```

Observed result:

- this environment still reports `No GPU — GPU-only graphs will be skipped`
- `rich_text_demo.json` was skipped again

Current classification:

- the earlier `Rich Text` GPU-path abort remains deferred
- it could not be reproduced or narrowed further from this machine because no usable headless GPU adapter is available here

## Findings

### 1. Core domain pipelines look healthy in the current tested surfaces

- Severity: `note`
- Workstreams:
  - `audio/control bridges`
  - `media timing and decode`
  - `MIDI and sequencing`
  - `cross-domain spread transport`
- Evidence:
  - 24 of 25 Phase 3 evidence-bundle tests passed
  - the passing lanes cover the main audio engine path, hot reload, media load/decode/session timing, MIDI parsing/playback, spread transport, capture coordination, and latency validation
- Current read:
  - no concrete signal emerged that audio/control/media/MIDI domain behavior is broadly wrong or unstable in the currently exercised release surfaces

### 2. Semantic-tag metadata drift was real, but is now fixed

- Severity: `fixed`
- Workstreams:
  - `tooling and metadata contract`
  - `LLM/MCP surface fidelity`
- Evidence:
  - the validation script and test enforce the vocabulary defined in `docs/SEMANTIC-PARAM-TAGS.md`
  - the original 15 failures clustered into a few understandable drift patterns:
    - generic tags that should have been namespaced or omitted: `mode`, `pitch_class`, `coefficient`, `ratio`
    - near-miss synonyms that should use existing core tags: `amplitude_db` -> `gain_db`, `pan_lr` -> `pan`, `time_offset_ms` -> `time_milliseconds`
    - resource types not present in v1 core taxonomy: `path_midi`, `path_lut`
  - a focused metadata cleanup normalized or removed those invalid tags in the affected operators
  - `test_semantic_tags` now passes
- Why this matters:
  - semantic tags are explicitly surfaced through:
    - control-server metadata
    - graph snapshots and inspector hints
    - operator-info caching
    - `RuntimeAPI::connect(..., semantic_defaults=true)`
    - operator scaffolding guidance for MCP/LLM workflows
  - the runtime tolerates unknown tags, so this is not a domain-execution stability bug
  - but the metadata is part of Vivid's user/tooling contract now, and invalid tags reduce trust in those surfaces and make semantic-default assistance less reliable
- Classification:
  - this was not a runtime blocker
  - this was not test drift
  - it was a real metadata-quality gap, and the cleanup was worth doing because LLM/MCP-assisted authoring is part of the product surface

### 3. The current semantic-tag failures do not point to broad cross-domain correctness breakage

- Severity: `note`
- Workstreams:
  - `domain correctness triage`
- Evidence:
  - the v1 semantic-tag spec explicitly states taxonomy only and says unknown tags must be tolerated and non-fatal at runtime
  - the failing tags are mostly parameter-level metadata, not core transport or scheduler semantics
  - the rest of the Phase 3 domain bundle is green
- Current read:
  - the tag failures mainly degrade metadata consumers, not live audio/media/MIDI execution behavior
  - they should be fixed because they affect product quality, but they do not justify reopening Phase 1 runtime-core stability or treating Phase 3 as a domain-pipeline instability phase

### 4. `rich_text_demo.json` no longer reads as a general GPU/runtime crash; it is currently a verification-gap issue

- Severity: `defer`
- Workstreams:
  - `GPU sidecar follow-up`
  - `verification infrastructure`
- Evidence:
  - filtered `./build/test_demo_graphs ./build/graphs rich_text_demo` still skips here for lack of a usable headless GPU adapter
  - the windowed runtime path now succeeds via:
    - `./build/vivid graphs/gpu/rich_text_demo.json --screenshot /tmp/rich_text_demo_windowed.png --screenshot-delay 20`
  - that run initialized the GPU context, loaded the graph cleanly, and wrote the screenshot without aborting
- Current classification:
  - this is no longer evidence of generalized GPU execution instability
  - current owner is `test infrastructure / environment coverage`, not `Phase 1 runtime-core`
  - keep a later GPU-capable follow-up only if a failing repro reappears in either headless or windowed form

## Required Fixes For Release

### Immediate release blockers

- None established by this phase.

- None remaining from Phase 3.

## Deferred Follow-Ups

Still explicitly deferred from this phase:

- deeper GPU-capable automation for `rich_text_demo.json` and similar GPU-only demo cases
- broader inspector/UI visual-quality work
- package/ecosystem conformance work that does not affect current domain correctness

## Signoff Status

- `pass with defer`

Reason:

- the main Phase 3 domain bundle is healthy
- the semantic-tag cleanup is now complete and validated
- the `Rich Text` sidecar has been narrowed to a verification-gap issue rather than a generalized runtime blocker
