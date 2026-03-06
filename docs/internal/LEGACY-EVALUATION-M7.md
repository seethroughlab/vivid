# Milestone 7: Legacy Branch Evaluation (Conservative, Package-First)

This document is the decision record for Milestone 7. It evaluates legacy-branch capabilities for possible adoption into current repos, using a conservative gate and package-first order.

No runtime/API behavior is changed by this milestone. This is a planning and selection artifact only.

## Evaluation Method

- Order: `vivid-3d` -> `vivid-drums` -> `vivid-glitch` -> `vivid-sequencers` -> `vivid-wavetable` -> `core`
- Conservative gate for `adopt_next`:
  - clear user-facing payoff for current product direction
  - strong fit with current architecture/contracts
  - low-to-moderate implementation and maintenance risk
  - no major dependency burden or cross-cutting churn
- Decision values:
  - `adopt_next`: schedule in next implementation queue
  - `defer`: not now; requires explicit unblock condition
  - `reject`: out of scope or architecture mismatch for current roadmap

## Decision Table

| candidate_id | legacy_source | target_repo | decision | user_value | architecture_fit | complexity_risk | dependency_impact | why_now_or_not_now | estimated_test_surface | prerequisites | priority_rank | expected_effort | blocking_risk |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| M7-3D-01 | `legacy:modules/vivid-render3d/src/gltf_loader.cpp` | `vivid-3d` | adopt_next | H | H | M | low | Better import diagnostics and fallback behavior directly improve package usability without core churn. | Import error-path fixtures; malformed/missing-texture cases; smoke graph with broken asset refs. | Existing `mesh_import` operator baseline. | 4 | M | low |
| M7-3D-02 | `legacy:modules/vivid-render3d/src/ibl_environment.cpp` | `vivid-3d` | adopt_next | M | H | M | low | Lightweight IBL quality improvements fit package scope and avoid core dependency growth. | Visual regression fixture set; parameter round-trip checks. | Keep current package shader/runtime boundary. | 7 | M | low |
| M7-3D-03 | `legacy:modules/vivid-render3d/src/shadow_manager.cpp` | `vivid-3d` | defer | M | M | H | low | Useful, but tuning surface is large. Unblock: define perf budget + parameter budget for live use. | Multi-light stress scenes + frame-time assertions. | Perf threshold doc for package CI/manual matrix. | - | - | med |
| M7-3D-04 | `legacy:modules/vivid-render3d/src/scene_composer.cpp` | `vivid-3d` | reject | L | L | H | med | Monolithic scene-composer pattern conflicts with current operator-first graph model. | N/A | N/A | - | - | high |
| M7-DR-01 | `legacy:modules/vivid-audio/src/drum_stack.cpp` | `vivid-drums` | adopt_next | M | H | S | low | Add package-owned macro examples/presets inspired by drum-stack workflow, no core changes required. | Graph + preset fixtures; install/rebuild smoke. | Keep implementation package-local. | 8 | S | low |
| M7-DR-02 | `legacy:modules/vivid-audio/src/drum_kit.cpp` | `vivid-drums` | defer | H | M | M | low | Velocity-layered drum behavior is valuable, but depends on reliable velocity lanes from sequencing/control flows. Unblock: sequencer velocity lane contract. | Velocity mapping tests across drum operators; preset compatibility checks. | Sequencer velocity output contract. | - | - | med |
| M7-DR-03 | `legacy:modules/vivid-audio/src/fm_drum.cpp` | `vivid-drums` | reject | M | M | H | low | New synthesis family is feature expansion, not a stabilization priority for current roadmap stage. | N/A | N/A | - | - | high |
| M7-GL-01 | `legacy:modules/vivid-audio/include/vivid/audio/glitch/rate_utils.h` | `vivid-glitch` | adopt_next | H | H | S | low | Tempo-locked rate helpers improve musical reliability of existing glitch operators with low risk. | Deterministic tempo fixture tests for beat_repeat/stutter/reverse. | None beyond current glitch package test harness. | 2 | S | low |
| M7-GL-02 | `legacy:modules/vivid-audio/src/glitch/reverse.cpp` | `vivid-glitch` | adopt_next | H | H | S | low | Anti-click transitions are immediately audible quality wins for reverse/scratch workflows. | Audio click-detection regression clips + subjective AB checks. | Existing reverse operator code path. | 3 | S | low |
| M7-GL-03 | `legacy:modules/vivid-audio/src/glitch/stretch.cpp` | `vivid-glitch` | defer | M | M | H | low | Higher-quality stretch is desirable but algorithmic risk is high for current cycle. Unblock: bounded quality/perf target and acceptance corpus. | Long-form stretch corpus + CPU usage thresholds. | Approved perf budget and fixture corpus. | - | - | med |
| M7-GL-04 | `legacy:modules/vivid-audio/src/tape_effect.cpp` | `vivid-glitch` | defer | M | M | M | low | Worthwhile color effect, but parameter semantics overlap existing operators. Unblock: avoid duplicate UX and define minimal parameter set. | Behavior tests for wow/flutter stability; preset round-trips. | Consolidated parameter spec vs existing tape_stop/scratch. | - | - | med |
| M7-SQ-01 | `legacy:modules/vivid-audio/src/sequencer.cpp` | `vivid-sequencers` | adopt_next | H | H | M | low | Step probability/ratchet patterns are high-value composition features with contained package scope. | Deterministic sequence fixture matrix; timing consistency checks. | Keep outputs compatible with existing control spread semantics. | 1 | M | low |
| M7-SQ-02 | `legacy:modules/vivid-audio/src/song.cpp` | `vivid-sequencers` | defer | H | M | H | low | Section/song arrangements are useful but imply broader timeline model. Unblock: explicit section model decision in roadmap. | Section transition fixtures + save/load compatibility tests. | Approved section/timeline model. | - | - | high |
| M7-SQ-03 | `legacy:modules/vivid-audio/src/arpeggiator.cpp` | `vivid-sequencers` | adopt_next | M | H | S | low | Additional arp modes and pattern quality improvements are low-risk and package-local. | Mode behavior fixtures; regression against existing arp behavior. | None. | 6 | S | low |
| M7-SQ-04 | `legacy:modules/vivid-audio/src/clock.cpp` | `vivid-sequencers` | defer | M | M | M | low | Humanization/swing refinements need precise event timestamp handling beyond current tests. Unblock: timing-jitter contract + validation harness. | Jitter/swing statistical tests over long runs. | Event timing contract for control outputs. | - | - | med |
| M7-WV-01 | `legacy:modules/vivid-audio/src/wavetable_synth.cpp` | `vivid-wavetable` | adopt_next | H | H | M | low | Wavetable morph interpolation quality is a direct payoff for existing package users. | Morph continuity tests; preset compatibility and audio artifact checks. | Preserve current preset schema compatibility. | 5 | M | low |
| M7-WV-02 | `legacy:modules/vivid-audio/src/poly_synth.cpp` | `vivid-wavetable` | defer | H | M | H | low | Voice-allocation enhancements are valuable but require broader polyphony contract decisions. Unblock: define poly voice lifecycle API at package boundary. | Voice-steal/voice-count stress tests; note-off correctness. | Poly voice lifecycle contract. | - | - | high |
| M7-WV-03 | `legacy:modules/vivid-audio/src/multi_sampler.cpp` | `vivid-wavetable` | reject | L | L | M | low | Multi-sampler capability is out of scope for wavetable package mission. | N/A | N/A | - | - | med |
| M7-WV-04 | `legacy:modules/vivid-audio/include/vivid/audio/wavetable_synth.h` | `vivid-wavetable` | defer | M | M | M | low | Runtime table-edit regeneration features need an editing UX contract first. Unblock: decide table-edit workflow and persistence format. | Table edit + reload + preset round-trip tests. | Table-edit UX/persistence decision. | - | - | med |
| M7-CORE-01 | `legacy:modules/vivid-core/src/hot_reload.cpp` | core | adopt_next | H | H | M | low | Incremental/debounced hot-reload robustness improves inner/outer loop directly. | Hot-reload reliability matrix (edit bursts, compile failures, recovery). | Maintain current package-aware reload flow. | 9 | M | low |
| M7-CORE-02 | `legacy:modules/vivid-core/src/shader_preprocessor.cpp` | core | adopt_next | M | H | S | low | Better include-chain diagnostics reduce shader/operator iteration friction with minimal architectural risk. | Shader compile error fixture set; diagnostic message assertions. | None. | 10 | S | low |
| M7-CORE-03 | `legacy:modules/vivid-core/src/av_analysis.cpp` | core | defer | M | M | M | low | Extended analysis is useful but not blocking current 1.0 release path. Unblock: milestone-level requirement tied to concrete UX/tooling gap. | Analysis payload regression and MCP integration checks. | Product requirement linking analysis depth to user workflow. | - | - | med |
| M7-CORE-04 | `legacy:modules/vivid-core/src/module_manager.cpp` | core | reject | L | L | H | med | Legacy module-manager architecture conflicts with current package/runtime boundaries. | N/A | N/A | - | - | high |
| M7-CORE-05 | `legacy:modules/vivid-core/src/video_exporter.mm` | core | defer | M | M | H | low | Section-aware export is interesting but depends on section/timeline model not yet adopted. Unblock: section model approved. | Export correctness matrix with section boundaries and AV sync checks. | Section/timeline model decision. | - | - | high |
| M7-CORE-06 | `legacy:docs/WEBSOCKET_API.md` | core | reject | M | M | H | med | Adds parallel control surface and maintenance burden; current control-server + MCP path is sufficient for 1.0. | N/A | N/A | - | - | high |

## Ranked Adopt-Next Shortlist

1. `M7-SQ-01` (`vivid-sequencers`) - step probability + ratchet support  
2. `M7-GL-01` (`vivid-glitch`) - tempo-locked rate helpers  
3. `M7-GL-02` (`vivid-glitch`) - anti-click reverse transitions  
4. `M7-3D-01` (`vivid-3d`) - GLTF diagnostics/fallback handling  
5. `M7-WV-01` (`vivid-wavetable`) - wavetable morph quality improvements  
6. `M7-SQ-03` (`vivid-sequencers`) - expanded arpeggiator modes  
7. `M7-3D-02` (`vivid-3d`) - IBL environment improvements  
8. `M7-DR-01` (`vivid-drums`) - package macro examples/presets from drum-stack workflow  
9. `M7-CORE-01` (core) - hot-reload robustness improvements  
10. `M7-CORE-02` (core) - shader-preprocessor diagnostics

## Deferred/Rejected Coverage Check

- Every `defer` entry includes a concrete unblock condition in `why_now_or_not_now` and/or `prerequisites`.
- No entry is left undecided.
- All `adopt_next` entries satisfy the conservative acceptance gate.
