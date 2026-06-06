# Audit 09: Seed Operators & Shared Libraries

**Date:** 2026-06-26
**Status:** Round-1 audited 2026-06-05 (verify-gated; 9 candidates → 3 confirmed, 6 dismissed). Round-2 maintainability re-audit 2026-06-05 (3 candidates → 3 confirmed, all Low) — section at end.

## Purpose

Audit seed operators and shared operator libraries for consistency, reusable patterns, lane behavior, preset metadata, DSP/editor duplication, and gaps that make operator authoring harder.

## Strong Audit Mandate

This audit must include a full code-quality pass, not only a correctness/robustness pass. Give equal
weight to maintainability: structure, duplication, ownership boundaries, API clarity, dependency
direction, and ease of future change.

Do not mark the audit complete until every checklist item is annotated as `[x]` done, `[~]` partially
covered, or `[ ]` intentionally deferred with a short note. Findings must include both confirmed defects
and structural risks that make future defects likely.

## Scope

- `operators/audio/`
- `operators/control/`
- `operators/gpu/`
- `operators/shared/`
- `operators/CLAUDE.md`
- Operator API docs used by seed operators
- Operator, ops, audio, control, GPU, lane, and shared-library tests

## Primary Questions

- [ ] Do seed operators consistently follow the current operator contract and file conventions?
- [ ] Are parameters, semantic tags, ranges, presets, and docs consistent within each domain?
- [ ] Do operators handle scalar and lane inputs predictably?
- [ ] Are shared DSP, sequencer, plugin, movie, and editor libraries factored at the right level?
- [ ] Are domain-specific exceptions intentional and documented?
- [ ] Are large implementation headers or duplicated editor/DSP patterns hiding correctness risks?
- [ ] Do operator tests cover representative behavior rather than only construction/load success?

## Subsystem Checklist

- [ ] Sample operators from audio, control, GPU, and shared-heavy groups.
- [ ] Review factory presets for schema consistency and useful defaults.
- [ ] Check lane-aware operators for cardinality, reset, and per-lane state behavior.
- [ ] Inspect editor-backed operators for duplicated editor state, selection, and serialization logic.
- [ ] Review shared DSP/plugin/movie/sequencer libraries for ownership and domain leakage.
- [ ] Verify tests cover presets, invalid inputs, reset behavior, hot reload, and lane/vectorized cases.
- [ ] Identify candidate reusable helpers that would reduce future operator-authoring friction.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Identify oversized files, mixed responsibilities, fragile seams, and unclear ownership.
- [ ] Identify duplicated logic or repeated patterns that should be shared or intentionally documented.
- [ ] Check dependency direction and public/private API boundaries.
- [ ] Check whether tests make future refactors safe, not just whether they cover the latest fix.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Required Maintainability Review

- [x] Map per-domain operator responsibilities and identify operator families with duplicated implementation patterns. → the note/sequencer family is well-factored; the only remaining duplication is **editor-window boilerplate** across the 4 grid editors.
- [x] Look for duplicated DSP, editor, preset, lane, reset, metadata, plugin, movie, sequencer, and parameter-handling logic. → editor beat-separator/playhead draw is duplicated ×4 (09-R2-F1); note-emission, step-advance, MIDI-parse are **shared or genuinely per-operator** (not duplication).
- [x] Check whether shared libraries have clear ownership and avoid leaking one operator's assumptions into another. → **clean**: `tracker_data.h`/`arpeggiator_patterns.h`/`drum_sequencer_layout.h` are single-owner; `note_helpers.h`/`note_id_counter.h`/`voice_breakouts.h` are assumption-free shared utilities.
- [x] Check whether operators follow reusable conventions that make future LLM-generated operators easier. → **yes**: consistent `Param<T>` / `collect_params`/`collect_ports` / `<Name>Core` / ChildOp `_embeddable.cpp` / lane-behavior declarations; uniform `factory_presets.json`.
- [x] Identify code that is correct today but fragile under likely lane, preset, plugin, editor, or domain-expansion changes. → editor styling lives in 4 copies (09-R2-F1); a new grid-editor will copy-paste the pattern. Lane behavior is **deferred** to the lane-value clean-break.
- [x] Produce refactor candidates with priority and expected payoff, separate from bug fixes. → see Round-2 Refactor Candidates below.

## Findings

Even under the strong maintainability mandate, this subsystem audited **clean**: the verify pass refuted
6 of 9 candidates, including **4 maintainability/docs findings that were factually wrong** — an invented
`*BaseOp` naming "convention" (the real, consistent convention is `<Name>Core`), a `granular_synth`
ChildOp that doesn't exist, an `audio_clip` "piano roll" misquote (it's a "WAV file player"), and a macro
base64-serialization migration risk that has no basis in the code. **No per-operator bugs** — operators
consistently follow the contract (OperatorBase + domain mixin, `<Name>Core` split, stable `kName`,
consistent semantic tags). The 3 confirmed findings are **1 shared-library design issue + 2 test-gaps**.

### Shared-library design

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 09-F1 | Medium | Maintainability | Base64 encode/decode is **byte-identical-triplicated** across the VST3/CLAP/AU plugin hosts (incl. the full 256-byte decode table), in three independently-edited anonymous-namespace copies that all feed plugin **state serialization** | `operators/shared/vst3_host/vst3_host_common.h:48-101`, `clap_host/clap_host_common.h:69-120`, `au_host/au_host_common.h:29-80` |

### Test gaps (per operator family)

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 09-F5 | Low | Test gap | No content-validation test for the **30 shipped `factory_presets.json`** — the loader stores params as an untyped name→float map with no check that keys exist on the operator or that enum indices are in range (a typo'd preset silently loads defaults) | `operators/**/factory_presets.json`; `operator_registry_scan.cpp:631-710` |
| 09-F6 | Low | Test gap | `Envelope`'s **scalar fallback path** (`scalar_state_`, lines 42-58) is never exercised — the only envelope test always supplies lanes, so a scalar-mode regression (Clock/LFO gate, no NoteBreakout) wouldn't be caught | `operators/control/envelope/envelope.cpp:42-58`; `tests/audio/test_envelope_poly_release.cpp` |

### Evidence & Recommendation

**09-F1 — base64 triplicated across plugin hosts** (Medium, Maintainability · shared-library — *refactor candidate*)
- *Evidence:* `vst3_b64_encode/decode` (`vst3_host_common.h:51-101`), `b64_encode/decode`
  (`clap_host_common.h:72-120`), `au_b64_encode/decode` (`au_host_common.h:32-80`) are character-identical
  apart from the symbol/table-name prefixes — same alphabet, same encode loop, same 256-entry decode
  lookup table, same accumulator. The AU header even comments "same as clap_host_common.h". All three feed
  `*_save_state`/`*_load_state` (e.g. `au_host_common.h:260-268`). (This precisely confirms the base64
  half of the deferred 05-F9.)
- *Impact:* No live bug (the three copies are currently identical), but three independently-maintained
  implementations of state-serialization-critical code invite future divergence (especially decode edge
  cases) and triple every fix. Maintainability, not correctness.
- *Recommendation:* Extract a canonical RFC 4648 base64 into **`operators/shared/plugin_common/base64.h`**
  (the dir already exists and already hosts deduplicated plugin helpers — `macro_bank.h`,
  `direct_param_queue.h` — so this matches the established pattern), point all three hosts at it, and add a
  round-trip unit test. **Priority: medium; payoff: high** (removes a class of cross-host drift on
  serialization paths). *Resolves the deferred 05-F9 base64 portion.*

**09-F5 — factory presets unvalidated** (Low, Test gap · all preset-bearing operators)
- *Evidence:* 30 real `factory_presets.json` files; the loader (`operator_registry_scan.cpp:631-710`) is
  crash-safe (malformed JSON / missing `presets` array are skipped with a warning) but parses params as an
  untyped `name→float` map with **no** validation that a key names a real param or that a value is a valid
  enum index — so a preset referencing a renamed param / out-of-range enum silently stores a dead entry
  ("loads but uses defaults"). *(Note: the load **mechanism** is unit-tested —
  `tests/ops/test_operator_loader.cpp` Test 25 — contrary to part of the finding; the gap is **content**
  validation of shipped presets.)*
- *Recommendation:* one `test_factory_presets_validity.cpp` that discovers every `factory_presets.json`,
  instantiates the operator, loads each preset, and asserts param keys exist on the descriptor + enum
  indices are in range + values round-trip. Covers all 30 in one test.

**09-F6 — Envelope scalar fallback untested** (Low, Test gap · lane-aware operators)
- *Evidence:* `envelope.cpp` has two divergent paths — scalar fallback (`scalar_state_`, 42-58) and
  polyphonic (60-100). `test_envelope_poly_release.cpp`'s harness always binds `input_lanes[0]` and
  supplies ≥1 voice, so the scalar path never runs. *(The finding's LFO extension is weak — `lfo.cpp` has
  no dual-path branching and is already covered by `test_child_op` / `test_scalar_port`.)*
- *Recommendation:* `test_envelope_scalar_vs_poly.cpp` — run Envelope scalar-mode vs single-lane and
  compare frame-by-frame (gate + phase-sync). Envelope only.

### Test Gaps (broader, by family)
- Factory-preset content validation — **all 30 preset-bearing operators** (09-F5).
- Scalar-vs-lane equivalence for **lane-aware control ops** (Envelope confirmed; audit others).
- AudioClip/MidiClip file-load error handling (missing file / unsupported codec / decode failure).
- Plugin-host macro resolution + audio-thread param emission under rapid reassignment.
- Editor clipboard paste edge cases (paste-larger-than-grid, empty-selection) for the 2 grid editors that
  *do* have clipboards (sequencer, drum_sequencer).

### Docs to Update
- `operators/CLAUDE.md` — note the base64 triplication + the `plugin_common/base64.h` extract plan (09-F1).
  *(ChildOp embedding is **already** documented at CLAUDE.md:35-44 — dismissed 09-F4.)*

## Follow-up

**Immediate** — none. No correctness defect; operators follow the contract.

**Near-term** — ✅ **DONE 2026-06-05** (build + test green)
- 09-F1: extracted `operators/shared/plugin_common/base64.h` (canonical RFC 4648) and pointed all three
  hosts at it via thin wrappers (zero call-site churn); added `tests/operators/test_plugin_base64.cpp`
  (round-trip 0..64 + full byte range + known vectors). Closes the 05-F9 base64 item.
  **Bonus correctness fix:** the three old copies shared a decode-table typo — `'='` mapped to `0`
  instead of being skipped, appending spurious trailing zero bytes when decoding non-3-aligned plugin
  state. The shared impl decodes correctly (the test's `decode("TQ==") == "M"` guards it).

**Backlog**
- 09-F5: `test_factory_presets_validity.cpp` (all 30 presets).
- 09-F6: `test_envelope_scalar_vs_poly.cpp`.
- The broader family test gaps above.
- The remaining 05-F9 plugin-host dedup (per-path ref-count maps) — separate from base64.

### Refactor Candidates (maintainability, separate from bug fixes)
1. **`plugin_common/base64.h`** (09-F1) — priority medium, payoff high. The one concrete refactor.
2. Per-path ref-count maps across the 3 plugin hosts (from 05-F9) — priority low, payoff medium; revisit
   with the sibling-migration plugin work.
- *Explicitly NOT refactor candidates (verify-refuted):* a shared editor-clipboard base (only 2 ops have
  clipboards, with genuinely different payloads — 09-F2), and a unified sequencer-core base/naming (the
  `<Name>Core` convention is already consistent across 10 ops — 09-F7).

### Dismissed (verification-refuted)

Six candidates were refuted (4 were maintainability/docs claims contradicted by the code):

- **09-F2** (no shared editor clipboard, "5×") — refuted: 3 of 5 cited files have **no** clipboard
  (`midi_clip`/`audio_clip` editors are JSON/waveform, not grids); only `sequencer` + `drum_sequencer` have
  one, and they already share `editor_ui/selection.h`. Miscounted ~2.5×.
- **09-F3** (MidiClip concurrency race / untested) — refuted: standard monotonic-generation-counter +
  mutex-guarded copy; the generation is *compared* every callback (can't be "missed"); `test_midi_clip.cpp`
  (1057 lines) exists. No race.
- **09-F4** (ChildOp undocumented) — refuted: `operators/CLAUDE.md:35-44` documents it incl. the
  `modulated_gain` call site; the finding's `granular_synth` example doesn't use ChildOp at all.
- **09-F7** (sequencer `*Core` naming inconsistent) — refuted: `<Name>Core` **is** the consistent
  convention (10 control ops); the `*BaseOp` ideal it faults the code against doesn't exist anywhere.
- **09-F8** (audio_clip warp/slice underdocumented) — refuted: per-param `description()` strings exist
  (`audio_clip.cpp:63-88`) and surface via `operator_docs`; the finding misquoted the summary.
- **09-F9** (`macro_bank` kMaxMacros migration risk) — refuted: macros are plain `Param<float>` saved by
  stable name; there is no base64/JSON macro serialization to migrate, and raise-only is already documented.

## Completion Criteria

- [x] Findings table is filled in or explicitly marked with no findings.
- [x] Findings distinguish per-operator bugs from shared-library design problems. *(No per-operator bugs;
  09-F1 is a shared-library design issue; 09-F5/F6 are test-gaps.)*
- [x] Parameter/preset consistency issues include domain examples. *(Params/semantic-tags consistent per
  domain — frequency_hz/Hz/KNOB, amplitude_linear, time_seconds/s; the only preset issue is the unvalidated
  factory-preset content, 09-F5.)*
- [x] Test gaps identify which operators or operator families need coverage.
- [x] Follow-up work is grouped into immediate, near-term, and backlog. *(+ refactor candidates listed
  separately from bug fixes per the mandate.)*

---

# Maintainability Re-Audit (Round 2) — 2026-06-05

Verify-gated maintainability pass per the Strong Audit Mandate. **3 candidates → 3 confirmed (all Low), 0
dismissed.** Low yield — the seed-operator layer is **well-factored**: note-emission is 100% shared
(`note_helpers.h` / `note_id_counter.h`, adopted by every note emitter — no hand-rolling); the
`shared/sequencer/` modules have **clean single-owner ownership** (no cross-operator leaks); step-advance and
MIDI-parse are genuinely **per-operator-semantic** (not duplication); the ChildOp `_embeddable.cpp`
convention, `factory_presets.json` shape, and authoring conventions (`Param<T>`/`collect_params`/`<Name>Core`)
are consistent and LLM-friendly. All findings cluster in the **editor-window boilerplate**, and all are
explicitly **low-ROI** (the grid editors already share `ui_step_grid`/`GridState`/`draw_ui_helpers`/
`editor_keys`/`Selection`; only ~15–20% per-file boilerplate remains, the other ~80% is operator-specific
rendering).

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 09-R2-F1 | Low | Maintainability | The **beat-separator** (every-4-steps loop) + **playhead column-highlight** draw is duplicated across the 4 grid editors (minor alpha/style variance) | `drum_sequencer_editor.cpp:436-451`, `sequencer_editor.cpp:285-302`, `pattern_seq_editor.cpp:294-308`, `arpeggiator_editor.cpp:392-399` |
| 09-R2-F2 | Low | Maintainability | Piano-roll coord helpers (`pitch_from_y`/`y_from_pitch`/`beat_from_x`) are file-local `static` in `midi_clip_editor` — **single adopter** (audio_clip is a waveform editor, not piano-roll) | `midi_clip_editor.cpp:55-68` |
| 09-R2-F3 | Low | Test gap | Editor tests are per-operator happy-path; **no cross-editor meta-test** would catch a regression if the shared beat-separator/playhead helper (F1) were extracted | `tests/operators/test_{drum_sequencer,sequencer,pattern_seq,arpeggiator}_editor.cpp` |

### Refactor Candidates (priority + payoff — separate from bug fixes)
1. **`draw_beat_separators()` + playhead helper** (09-R2-F1) — **priority low, payoff low.** Extract a small
   inline helper (in `src/operator_api/editor_ui/`) taking grid rect + step count + style; migrate the 4
   editors. Marginal ROI (~15-20 lines/editor); do only when touching editor styling. If done, pair with the
   F3 cross-editor meta-test.
2. **Piano-roll coord helpers** (09-R2-F2) — **no action now**; extract only if a 2nd piano-roll operator
   arrives (single adopter today).

### Dismissed (verification-refuted)
- None this round. The recon was accurate and the finder appropriately scoped F1 as low-ROI rather than
  inflating it. (Confirmed clean and **not** filed: note-emission sharing, shared-module ownership,
  step-advance per-operator semantics, MIDI-parse, ChildOp convention, preset structure, authoring
  conventions.)

### Out of scope
- Round-1 09-F1 (base64 triplication) is **already fixed** (`plugin_common/base64.h`). Round-1 09-F5
  (preset content-validation test) + 09-F6 (Envelope scalar-fallback test) remain backlog test gaps.
- Lane behavior (the lane-heavy control ops) is **deferred** to the queued lane-value clean-break.

## Round-2 Follow-up
- **Backlog (all low-priority):** 09-R2-F1 (`draw_beat_separators` helper, when touching editor styling) +
  09-R2-F3 (its cross-editor meta-test); 09-R2-F2 is no-action. Nothing cheap-and-urgent — the family is
  clean.
