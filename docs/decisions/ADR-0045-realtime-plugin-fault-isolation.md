# ADR-0045: Realtime Plugin Fault Isolation

Status: accepted (Tier 1 + Tier 2a implemented; Tier 2b deferred — see As Built)

Date: 2026-07-31

> **Origin.** Raised by the first-release Code Audit, Phase 2 (Realtime Audio & Thread
> Safety), finding **P0-01** — a release blocker. See
> `docs/audits/07-31-2026/code/phase-02-realtime-audio-and-thread-safety.md`.

## Context

Vivid hosts third-party VST3 and CLAP plugins and calls their `process` / `clap_run` on
the **real-time audio thread**. The Phase-2 audit found this path has **no fault
boundary**:

- The four plugin-process call sites — `audio/vst3_host_render.cpp:111` (VST3 instrument),
  `:154` (VST3 effect), `:175` (CLAP instrument), `:188` (CLAP effect) — are **not**
  wrapped in `CrashGuard`, unlike the native-op path (`audio_op_runtime.cpp:377-378`) and
  the visual-op path (`gpu/visual_graph.cpp:464`).
- `CrashGuard` (`app/crash_guard.h:44-59`) is **attribution-only**: it sets a thread-local
  "current operator" pointer and, on a fatal signal, writes a marker file naming the
  culprit, then re-raises `SIG_DFL`. The process still dies — but on next launch the marker
  drives crash-recovery + quarantine so the offender can be disabled.

Consequences today:
- A hosted plugin that **crashes** in `process` produces an *anonymous* SIGSEGV (the
  thread-local names no operator), so it **cannot be attributed** and the quarantine
  pipeline never disables it — the user can crash on the same project repeatedly.
- A C++ exception propagating out of `process` → `std::terminate`.
- A plugin that **hangs** in `process` (spinlock, disk IO, wavetable rescan) stalls the
  miniaudio callback — an **unbounded RT stall** with no watchdog and no recovery.

This violates the release acceptance criterion "Plugin failures cannot crash or corrupt
the host project." Out-of-process protection exists for plugin *scanning* (fd-3 verdict
probe, 30 s timeout → SIGKILL, crash-class cache, sentinel), but **not** for live
processing. Third-party plugin code is exactly the untrusted code most likely to fault, and
it runs on the hot path with no boundary.

## Decision (to be made)

Two tiers — the first is **release-gating**, the second is post-release policy:

1. **Release-gating minimum — attribute plugin crashes.** Wrap the four plugin-process
   call sites in a `CrashGuard` that names the plugin (id + format), so a crash-in-process
   becomes attributable and the existing crash-recovery → quarantine pipeline disables the
   offending plugin on relaunch. This is the smallest change that satisfies the acceptance
   criterion's "corrupt the host project" clause (the user is no longer stuck in a crash
   loop). It ships with a test asserting a deliberately-crashing fixture plugin is
   attributed.

2. **Post-release — contain crashes and hangs, don't just attribute them.** Evaluate, in
   priority order: (a) a **watchdog** that detects an over-budget `process` call and
   fails the plugin's audio out to silence + flags it, bounding the RT stall; (b) a
   **bounded-time / sandboxed** processing model for untrusted plugins (separate process or
   thread with a hard deadline) so a crash or hang degrades one track instead of the app.
   Each has real cost (latency, IPC, complexity) and is not first-release scope.

## As Built

- **Tier 1 (attribution) — shipped (#190).** The four plugin-process render sites
  (`audio/vst3_host_render.cpp`) are wrapped in a `CrashGuard` named by the plugin, so a crash in
  `process()` is attributed and the crash-recovery → quarantine pipeline disables it on relaunch.

- **Tier 2a (the watchdog) — shipped (#273 over-budget, + the hang monitor).** Each render site times its
  `process()` call (`audio/plugin_watchdog.h`). Over-budget strikes accumulate with decay; past a threshold
  the plugin latches `faulted` and `process_step` skips it — an instrument fails to silence, an effect
  passes through dry — bounding the *repeated* stall. A separate **hang monitor thread**
  (`audio/plugin_hang_monitor.h`) watches an in-flight beacon the RT thread publishes before each call; a
  call still running past the hang deadline (default 1 s) is named, latched `faulted`, and reported, so a
  *permanent* hang becomes a named, logged, auto-disabled fault instead of a silent freeze. Both paths push
  a lock-free ring the frame thread drains into a warning toast + a log entry. RT-safe (atomics + integer
  math only); audio-thread-sanitizer clean. Tunables are env-overridable
  (`VIVID_PLUGIN_BUDGET_MULT` / `_STRIKES` / `_HANG_MS`). Tests
  `test_plugin_watchdog` / `test_plugin_hang_monitor` drive both paths through the real render path.

- **Tier 2b (rescue a live hang) — deferred.** The watchdog can bound *repeated* stalls and *name* a
  permanent hang, but it **cannot keep audio alive through the buffer a plugin is currently frozen inside** —
  the RT thread is stuck in `process()` and cannot be safely interrupted. Only running `process()` on a
  worker thread with a hard deadline (so the RT thread can abandon a hung call and output silence) truly
  rescues it, at real latency/complexity cost (per-plugin thread, handoff, abandoned-worker lifetime). That
  remains deferred; it is the only path to true containment and should be its own ADR/effort when the cost
  is justified.

## Consequences

- **Positive:** the first release no longer silently loses a user's session to a crash it
  can't even name; a bad plugin gets quarantined like a bad native operator.
- **Tradeoff (tier 1):** attribution still means the process dies on a plugin crash — it is
  *recoverable*, not *prevented*. That matches how most in-process DAW hosting behaves and
  is an honest first-release stance, but it must be stated in the release notes.
- **Tradeoff (tier 2):** true containment (watchdog / sandbox) is a substantial engineering
  investment deferred past first release.

## Alternatives Considered

- **Do nothing / accept in-process risk unmitigated.** Rejected — the *unattributed* crash
  loop (tier-1 gap) is a concrete, reproducible way to lose work, not a theoretical risk.
- **Full out-of-process plugin hosting now.** Rejected as first-release scope — large
  latency/complexity cost; revisit as tier 2.
- **Catch C++ exceptions around `process` only.** Insufficient alone — most plugin faults
  are signals (SIGSEGV), not C++ exceptions, so `CrashGuard` attribution is the load-bearing
  part; exception-catching is a cheap add-on within the same wrap.
