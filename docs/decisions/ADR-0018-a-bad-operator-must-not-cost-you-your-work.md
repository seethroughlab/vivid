# ADR-0018: A Bad Operator Must Not Cost You Your Work

Status: accepted (implemented — R1..R4, 2026-07-16; three PRs)

Date: 2026-07-14

## As built (2026-07-16)

Landed as three sequential PRs, in the reverse order of the resilience chain (highest everyday value
first):

- **R4 — Never lose work (PR 1).** An app-level dirty flag on the ADR-0017 `EditGateway` (set in
  `push_snapshot`/`restore`, cleared on save/load/new), the macOS window edited-dot, a Save/Don't
  Save/Cancel confirm on New/Open/Quit, a periodic **autosave** to one well-known slot under
  `user_data_dir()/autosave/` (the trunk doesn't reload the last project, so a per-project sidecar
  wouldn't be found), and a **recovery** offer on launch that restores it.
- **R1 — Attribute (PR 2).** `crash_guard.h`: a `CrashGuard` sets a thread-local current-operator name
  around each `process_gpu()`/`process_audio()` call (RT-safe — a pointer store to an owned string;
  the zero-alloc audio test stays green), and an **async-signal-safe** handler
  (SIGSEGV/BUS/ILL/FPE/ABRT) names the operator and writes a pre-formatted marker with only
  `open/write/close/raise`. Installed in `main.cpp`; a no-op under sanitizers.
- **R2 — Record (PR 2).** A warm node↔operator snapshot written each frame; on launch a marker
  reconstructs a `CrashRecord` (operator, node, signal, time) into a capped-20 history under
  `crashes/`, surfaced via `VLOG_ERR` → the ADR-0019 auto-toast. The record/history logic is App-free
  (headless-tested); the snapshot writer is the App-linked half (same split as `runtime_health`).
- **R3 — Safe mode + quarantine (PR 3).** A **stateless** `quarantine.{h,cpp}`: ≥3 crashes in 24h
  (recomputed from the history, keyed on op `type_name`) disables an operator at load — it is skipped
  in `load_and_register_operator`, so its persisted nodes load as `op_missing()` (ADR-0019, for free)
  and it is **greyed in the Tab chooser with a reason** (`disabled_note`). `--safe-mode` additionally
  disables the last crasher. Visible + reversible: `list_quarantine` / `unquarantine` MCP endpoints
  (clear an op's crash history to re-enable on restart).

Boundary held: not a sandbox (operators still run in-process; crashes are attributable + survivable,
not harmless), not crash reporting (no telemetry), no watchdog/supervisor binary (safe mode is a flag
+ the auto-quarantine, not an auto-relaunch).

Amends: [ADR-0011](ADR-0011-poc-to-product-architecture.md), which made **the third-party dylib the
unit of extension** — and did not say what happens when one of them segfaults.

Decided: adopt classic's four-link resilience chain — **attribute → record → safe mode →
quarantine** — so that a crashing operator is named, survivable, and eventually disabled rather than
anonymous, fatal, and repeat-fatal. Add, on top of it, the thing classic never had: **a dirty flag,
autosave, and recovery of unsaved work.**

## Context

The extension model is `dlopen`. Operators are third-party C++ compiled into dylibs and called on the
frame and audio threads (`app/src/gpu/op_runtime.cpp`, `app/src/gpu/visual_graph.cpp`,
`app/src/audio/audio_op_runtime.cpp`). ADR-0016 (shipped) widened that further, turning shader files
into operators authored by people who are not C++ programmers.

Which means: **operator code will crash.** That is not a defect in the plan; it is the plan. The
question an extensible platform has to answer is what the app does about it, and right now the answer
is: nothing.

A segfault inside an operator today produces an anonymous `SIGSEGV`. There is no record of which
operator it was, no way to start the app without it, and no memory that it has done this before. The
resulting failure mode is not "the app crashed" — it is **the app crash-loops**, because the graph
that killed it is the graph it reloads on launch. This project has already hit exactly that.

And orthogonally, work is losable by ordinary means. There is no app-level dirty flag — `dirty_`
exists only inside `ClipEditor` (`app/src/ui/clip_editor.h`). Nothing guards New, Open, or Quit. A
misclick discards an unsaved session with no prompt, and a crash discards it with no trace.

### What classic built

Four pieces, each small, each doing one thing:

1. **`crash_guard.h`** — an RAII guard around every `process_frame` / `process_audio` / `process_gpu`
   call that sets a thread-local "current operator" name. The signal handler reads it and prints
   *"crashed in MyBrokenOp"* before re-raising. This is the cheapest, highest-value piece in the
   entire chain: it converts an anonymous crash into an attributed one for the cost of a thread-local
   store per operator call.
2. **`crash_recovery.{h,cpp}`** — the handler writes a marker; the *next* launch reads it and
   reconstructs a `CrashRecord` (timestamp, signal, operator name, node id, node type, package name
   and version, reload serial, audio buffer size). Kept as a 20-entry history in
   `{config_dir}/crashes/`.
3. **`safe_mode.h`** + `--safe-mode` — relaunch with the offending node replaced by a placeholder, so
   you can open the session, delete the bad node, and save.
4. **`quarantine.{h,cpp}`** — an operator identity that crashes **3 times in 24 hours** is disabled
   by default on subsequent launches. Stateless: recomputed on every launch from the crash history,
   so there is no quarantine file to corrupt or migrate.

Together these turn a crash loop into a bounded, self-healing sequence: crash → *"that was
`bad_op`"* → relaunch safely without it → after the third time, stop loading it at all.

### Where we should beat classic

Classic's crash snapshot is **diagnostic, not a document**. It tells you what died; it does not give
you back the session you were editing. Neither does classic have autosave. That gap is ours to close,
and it costs almost nothing extra because the mechanism already exists: ADR-0017 (shipped) installs the
**`EditGateway` command sink** — one object every document mutation (UI *and* MCP) routes through. A
dirty flag and a periodic autosave ride that same sink for free.

That shared sink is why ADR-0017 came first — not a preference about ordering. It has landed (PR #31),
so this dependency is already satisfied.

## Decision

1. **Attribute.** A `CrashGuard` RAII wrapper around every operator entry point, setting a
   thread-local current-operator/node. A signal handler for `SIGSEGV` / `SIGBUS` / `SIGILL` /
   `SIGFPE` that reports the attribution and re-raises. **Async-signal-safety is mandatory** in the
   handler: no allocation, no `printf`, no locks — write pre-formatted bytes to a pre-opened fd. (The
   codebase already knows this discipline; see the plugin-probe verdict channel, which writes to fd
   3 precisely because stdout is unsafe/noisy.)

2. **Record.** On the next launch, reconstruct a `CrashRecord` from the marker and append it to a
   capped history in the app config dir. Surface it at startup — this is the first customer of
   [ADR-0019](ADR-0019-nothing-fails-silently.md)'s notification surface.

3. **Safe mode.** A `--safe-mode` flag, and an *offer* of safe mode when the previous run crashed.
   The crashing node loads as a placeholder that preserves its id, params, and wires — so the graph
   shape survives, the node just does nothing. The user deletes it and saves.

4. **Quarantine.** Three crashes attributed to one operator identity (`type_name` + `pkg_name`)
   within 24 hours disables it by default. Recomputed from the crash history on each launch; no
   separate state file. Surfaced clearly — a quarantined operator says so in the chooser, which
   already supports greying a row *with a reason* (`app/src/ui/chooser.h`).

5. **Never lose work.** On ADR-0017's `EditGateway` command sink: an app-level `dirty` flag; `macos_set_document_edited`;
   a save-confirm dialog on New / Open / Quit; and a periodic **autosave** to a sidecar file. On
   launch, if a sidecar is newer than its project, offer to recover. Reuse
   `app/src/app/file_actions.{h,cpp}` and `project_io.*` — the save/load/recents machinery is already
   solid and needs no rework.

### Boundary rule — what this is not

- **Not a sandbox.** We are not isolating operators into subprocesses or restricting what they can
  do. An operator that corrupts memory can still corrupt memory; we make it *attributable* and
  *survivable*, not *harmless*. (The VST3 plugin probe is subprocess-isolated because plugins are
  opaque third-party binaries we merely scan; operators are called per-frame on the hot path and
  cannot pay that cost.)
- **Not crash reporting.** No server, no telemetry, no upload. Crash data stays on the user's
  machine, as it does in classic.
- **Not a promise to survive audio-thread corruption.** A crash guard on the audio thread attributes
  the crash; it does not make the audio thread fault-tolerant.

## Consequences

**Good.** The worst failure mode the extension model has — a third-party dylib that bricks the app on
every launch — becomes bounded and self-healing. Operator authors get a named crash instead of a
stack trace into `dlopen`'d code. And unsaved work stops being one misclick or one segfault from
gone.

**Costs.** A thread-local store per operator call, on the frame and audio hot paths. It is one store
to TLS, but it is *on the audio thread*, so it must be measured, not assumed — see
`docs/thread-safety.md` before touching `audio_op_runtime.cpp`. Signal handlers are also genuinely
easy to get wrong; the async-signal-safety constraint above is not advisory.

**Risk.** Quarantine can be wrong. If an operator crashes for a reason that isn't its fault (a bad
GPU driver, a corrupt asset), quarantining it is a confusing false positive. This is why quarantine
must be *visible and reversible* — the user has to be able to see "this is quarantined, here's why"
and un-quarantine it.

## Implementation

### R1 — Crash guard + attribution

`app/src/app/crash_guard.h` (RAII, thread-local) wrapped around the operator calls in
`app/src/gpu/op_runtime.cpp`, `app/src/gpu/visual_graph.cpp`, `app/src/audio/audio_op_runtime.cpp`.
Signal handler installed in `main.cpp`, async-signal-safe, writing a pre-formatted marker.

*Verify:* build a deliberately-crashing test operator (a null deref behind a param), add it to a
graph, trigger it. The app must die **naming that operator**. Then measure: run the audio RT test
(`app/tests/test_audio_op_rt.cpp`) and confirm no regression on the audio path.

### R2 — Crash record + history

Marker → `CrashRecord` on next launch. Capped history in `{config_dir}/crashes/*.json`. Surface at
startup.

*Verify:* crash the app via R1's test operator, relaunch, confirm the record names the operator, node
id, and package, and that a JSON file landed in the crash dir.

### R3 — Safe mode + quarantine

`--safe-mode` and the placeholder-ization path. Quarantine recomputed from the history (3 in 24 h).
Quarantined ops greyed in the chooser with a reason, and un-quarantinable.

*Verify:* crash the same operator three times; confirm the fourth launch loads the graph with that
node placeholdered and the rest of the graph *running*. Confirm the chooser explains why. Confirm
un-quarantining restores it.

### R4 — Dirty flag, autosave, recovery

The dirty flag on ADR-0017's `EditGateway` command sink. Save-confirm on New/Open/Quit. Periodic autosave sidecar.
Recovery offer on launch.

*Verify:* run the app — edit a graph, `kill -9` the process, relaunch, and confirm the edit is
offered back. Then edit and hit Cmd+N and confirm the save-confirm dialog appears. (Note: `kill -9`
on macOS has previously interacted badly with saved-state restoration in this project — if a relaunch
loop appears, that is the known saved-state issue, not this feature.)
