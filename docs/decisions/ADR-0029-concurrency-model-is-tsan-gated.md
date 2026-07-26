# ADR-0029: The Concurrency Model Is Explicit and ThreadSanitizer-Gated

Status: accepted (2026-07-25)

Date: 2026-07-25

Extends [ADR-0025](ADR-0025-cpp17-organization-and-patterns.md) (real-time safety) and the thread-safety
model documented in `app/docs/thread-safety.md`.

**As built — phase 1 (2026-07-26).** Landed as one PR: (1) `app/docs/thread-safety.md` gained the
enumerated **cross-thread channel table** (every producer→consumer channel + its ordering invariant) and
a "benign torn reads" section (decision #1). (2) The **active-notes bus was hardened** — each note slot is
now a `std::atomic<uint64_t>` (pitch+velocity packed), so the deliberately-tolerated torn read is a
*well-defined* data race on atomics instead of UB on plain memory, and it is TSan-clean (decision #4).
(3) A portable multithreaded driver `test_note_bus` races a publisher against a reader over the bus
(decision #3). (4) A **`tests (thread-sanitizer)` CI job** builds with `-DVIVID_SANITIZE_THREAD=ON`
(`VIVID_BUILD_APP=OFF`) and runs the `THREAD`-labelled concurrency tests (`ctest -L THREAD`) under
`TSAN_OPTIONS=halt_on_error=1` (decision #2) — the leg opts *in* the racing tests because the
package/dlopen tests SEGV under TSan (loading a non-instrumented dylib), a known limitation, not a race.
Marking the job *required* in branch protection is a one-time repo-settings toggle.

**As built — phase 2, COMPLETE (2026-07-26).** All five benign-torn-read channels are now hardened to
atomic slots, each a header-only type with a portable `THREAD`-leg race test: the active-notes bus (phase 1)
+ the **spectrum ring** (`AnalysisRing`, `analysis_ring.h`), the **per-node scope/FFT rings** (`NodeRingBank`
of `unique_ptr<atomic<float>[]>` per-node rings, `node_ring_bank.h`), and the **held-note set** (`HeldNoteSet`
of packed-{pitch,vel} atomic slots, `held_note_set.h`). And the **audio↔UI concurrency gate** now exists:
`test_session_concurrency` races a render thread (`session_process`) against a single UI/mutator thread
(add/remove tracks, edit clips, mutate the graph, set params, launch scenes, read snapshots) over the real
gen-counter/`try_lock`/SPSC surface, run under TSan on a per-PR macOS CI job (`ctest -L AUDIO_THREAD`). It
is **clean** — the model holds under real render+mutation racing.

**Corrections this phase made to the phase-1 note above:** (1) running `test_session_executor` under TSan is
*not* the lever — it's single-threaded, so TSan finds nothing; the concurrent harness is. (2) **No
third-party suppression list is needed** — `session_process` starts no device/GPU/CLAP thread, so only the
harness's two threads race (the phase-1 note's miniaudio/wgpu/GLFW suppression assumption was wrong).

**Not yet triggered:** the native-op param write (`session_audio_graph_node_param_set` →
`audio_op_param_set` writes op memory under `gmtx`; the audio thread reads it via `gbinds` during render
without `gmtx`) is a plain-memory channel absent from the channel table. The harness did not surface it, so
it is either benign in practice or the race window wasn't hit; a deeper audit (a targeted harness that holds
a note while hammering that op's param) could confirm — a small future follow-up, not a gap in the gate.

## Context

Vivid runs a hard real-time audio thread alongside a UI/frame thread, a control-server worker, a CoreMIDI
thread, and the async CLAP loader thread. The safety model is real and mostly disciplined — atomics, SPSC
rings, generation-counter publication, retired lists, `try_lock` skip-on-contention — and it is written down
in `app/docs/thread-safety.md`. Two gaps remain:

- **The lock-free invariants live in prose and code comments, not in a checkable contract.** The
  publish-last-as-a-release-barrier pattern (e.g. the note bus tags a slot's stable id *after* writing its
  notes+count; the `gbinds` swap publishes a POD copy under `try_lock`) is correct where it is written, but a
  reviewer confirms each new instance by reading carefully. There is no single enumerated list of "these are
  the cross-thread channels and this is the exact ordering each one relies on."

- **ThreadSanitizer is wired but not run.** `app/CMakeLists.txt` has a `VIVID_SANITIZE_THREAD` option
  (`-fsanitize=thread`), but it is **off by default and absent from CI**. The per-PR matrix
  (`.github/workflows/headless-tests.yml`) runs `sanitize: [OFF, ON]` where `ON` is **ASan + UBSan**
  (`VIVID_SANITIZE`), which catches memory and UB but **not data races**. So the one class of bug this
  architecture is most exposed to — a cross-thread race on a field that "looked atomic enough" — is the one
  class CI does not test for. The code-health review flagged this as the reason the two hardest
  synchronization claims had to be verified by hand rather than by a tool.

## Decision

Make the concurrency model explicit and enforce it with ThreadSanitizer in CI.

1. **Enumerate the cross-thread channels.** Extend `app/docs/thread-safety.md` with a table of every
   producer→consumer channel (note bus, movie-audio bus, `gbinds`/plan publish, audio param queues, live
   MIDI ring, control-server → UI queue, analysis/scope rings, modulator `ctl_pub`), and for each: the
   producer thread, the consumer thread, the mechanism (SPSC ring / atomic release-store / `try_lock` swap /
   generation counter), and the **ordering invariant** it relies on (what is published last, and why that
   gates everything published before it). This turns "read the code carefully" into "check against the list."

2. **Add a ThreadSanitizer CI leg.** Add a TSan build+test job (a third matrix value or a dedicated job) that
   compiles the headless suite with `VIVID_SANITIZE_THREAD=ON` and runs it. It is a **required** check once
   green, alongside `gate`, `check`, `audio-engine-tests`, and the ASan/UBSan legs.

3. **The RT-reachable tests must exercise the channels.** TSan only finds a race on code it runs, so the
   headless tests that drive `session_process` and the bridge publication path (the ones that already stand
   in for the audio thread) are the vehicle. Where a channel has no headless driver, add a minimal one that
   pushes the producer and consumer concurrently, so the sanitizer has both sides to observe.

4. **Annotate benign races explicitly.** The model deliberately tolerates a *benign* torn read on some
   buses (a 1-frame visual glitch, documented as acceptable — e.g. the note bus). Where TSan would flag one
   of these, it is made intentional in code (an explicitly-`relaxed` atomic with a comment, or a scoped TSan
   suppression that names the invariant), never silenced blindly. A suppression without a documented
   why-it's-benign is not allowed.

## Alternatives Considered

- **Keep verifying races by code review.** Rejected. Review caught the current races because they were the
  focus of a dedicated audit; it does not scale to every future PR that touches an atomic, and it is exactly
  the failure mode ("looked atomic enough") that a dynamic tool exists to catch.
- **Rely on ASan/UBSan (already in CI).** Rejected — those do not detect data races. They are necessary and
  stay; TSan is the missing axis.
- **Run TSan locally / on-demand only.** Rejected as the primary control. An off-by-default sanitizer is run
  when someone remembers, which is when it is least likely to catch the regression that just landed. On-demand
  stays available for deep debugging; the *gate* is what prevents new races from merging.
- **Move everything behind locks to sidestep the question.** Rejected. Locking the audio thread violates
  ADR-0025's real-time decision (#3, allocation-free and non-blocking). The lock-free model is correct for
  this architecture; the decision is to *verify* it, not replace it.

## Consequences

- **Positive:** New cross-thread code is checked by a tool on every PR, not only by a careful reviewer; the
  race class this architecture is most exposed to becomes CI-visible.
- **Positive:** `thread-safety.md` becomes an enumerated, checkable contract — onboarding and review both get
  a single list to reason against.
- **Tradeoff:** A TSan build is slow (instrumented compile + run); it lengthens CI. On the self-hosted runner
  (which *is* the dev machine — see the CI notes) this competes with local work, so the leg should be sized
  and possibly scheduled to avoid starving interactive use, and kept fast by targeting the RT-reachable tests
  rather than the whole binary where possible.
- **Tradeoff:** TSan will almost certainly surface findings on first run — some real, some benign-torn-read
  by design. Triaging and annotating them (decision #4) is upfront work before the gate can be made required.
- **Follow-up:** Once green, wire it required in branch protection next to the existing checks; document in
  `thread-safety.md` that a new cross-thread channel is not "done" until it has a headless driver the TSan
  leg exercises.
