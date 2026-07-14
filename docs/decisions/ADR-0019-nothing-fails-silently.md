# ADR-0019: Nothing Fails Silently

Status: proposed

Date: 2026-07-14

Amends: [ADR-0013](ADR-0013-focus-first-strict-zone-ui.md) (the strict-zone UI), which specifies
where things live but never says where *failure* lives.

Decided: every failure the engine already detects gets a **UI surface**. A broken node **looks**
broken. A shader that failed to compile says why, on the node. The health rollup that already exists
drives a status indicator, with a diagnostics panel behind it. And we explicitly **decline** to
invent per-node timing and FPS metrics we cannot yet measure truthfully.

## Context

This is the strangest gap in the trunk, and the most valuable one to close, because **the work is
almost entirely already done.**

The engine knows a great deal about its own failures:

- **Shader errors.** `app/src/gpu/shader_library.h:30` — a shader that fails to parse *still gets a
  catalog row*, and that row carries `std::string error; // non-empty => not registered; this says
  why`. On a failed hot-reload edit, `Failed` status is set with an error and *"the last good version
  still runs"* (`shader_library.h:43`).
- **Missing operators.** `HealthSnapshot::missing_ops` — *"chain nodes whose op type isn't registered
  (BROKEN)"* (`app/src/app/runtime_health.h:27`).
- **GPU errors.** `gpu_ok`, `gpu_errors`, `gpu_last_error` (`runtime_health.h:20-22`).
- **Descriptor violations.** Named codes from `app/src/operator_api/operator_descriptor_validation.h`,
  validated loudly at startup.
- **A severity rollup.** `Severity severity(const HealthSnapshot&)` — *"Error if the GPU device is
  lost or the graph references a vanished op; Warning on uncaptured GPU errors or a down control
  server"* (`runtime_health.h:37`).

All of it is computed. All of it is correct. And all of it goes to **`stderr` and MCP, and nowhere
else.**

Grepping `error|missing|broken` across `app/src/ui/node_graph.cpp` and `app/src/ui/session_view.cpp`
returns nothing. There is no error badge, no toast, no status indicator, no diagnostics panel, no log
view. A user whose graph contains a node referencing an operator that no longer exists sees... a
normal-looking node that does nothing. A user whose shader has a syntax error sees the last good
version still running and **no indication that their edit didn't take.**

That last one is not a cosmetic problem. It is a *lie*. The app is showing output that does not
correspond to the code on disk, and saying nothing.

There is exactly one place the UI does surface a "you can't do that, here's why" today, and it is
good: the Tab chooser greys out disabled entries with a `disabled_note` explaining the reason
(`app/src/ui/chooser.h`). That is the pattern. It just needs to exist everywhere else.

### Why this ADR is a prerequisite

The build console in [ADR-0020](ADR-0020-the-inner-loop-is-visible.md) is, structurally, an error
surface. Node badges, a notification/toast channel, and a panel substrate are its dependencies.
Building the console first means building all of that anyway and building it twice. Hence the
ordering.

## Decision

1. **Node error badges, in the shared canvas.** A node whose op type is missing, whose shader failed
   to compile, or whose descriptor is invalid draws an error state — badge plus health-tinted border.
   This goes in **`app/src/ui/node_canvas.h`**, which is included by both `node_graph.cpp` and
   `audio_node_graph.cpp`, so both graphs get it from one implementation. Clicking the badge shows
   the message the engine already has (`ShaderLibraryEntry::error`, the descriptor code, the missing
   op's type name).

2. **A health status indicator.** `severity()` already returns `Ok` / `Warning` / `Error`. Bind it to
   a dot in the header. Clicking it opens the diagnostics panel.

3. **A diagnostics panel.** The contents of the `HealthSnapshot`, presented — GPU state and last
   error, missing ops (as clickable node references), packages loaded, control-server liveness, app
   version. This is a presentation of `to_json(HealthSnapshot)`; no new collection.

4. **Notifications (toasts) and a log view.** A notification channel for transient failures (a load
   that failed, a reload that didn't apply), and an in-app log with levels. Today **everything** is
   `std::fprintf(stderr, "[vivid] …")` — no levels, no file, no view. Introduce a minimal
   leveled logger that both writes through to stderr *and* feeds the in-app view, so nothing that
   works today stops working.

5. **The failure vocabulary is the one that already exists.** `app/src/cli/control_errors.h` defines
   the stable code vocabulary (`bad_arg`, `not_found`, `invalid_descriptor`, …) and the comment in it
   explicitly cites this as the lesson learned from classic. The UI surfaces those same codes. One
   vocabulary, three consumers (MCP, log, UI).

### Boundary rule — what this is not

**We do not invent metrics we cannot measure truthfully.** `app/src/app/runtime_health.h:12` states
the standard already, and this ADR upholds it rather than overriding it:

> *"Honesty over coverage: we only carry signals we can read truthfully + cheaply from App today.
> Audio xrun metering and per-node timing are intentionally absent until there's a real source for
> them."*

So: **no FPS counter, no per-node timing, no profiler overlay, no audio xrun count** — not because
they are unwanted, but because there is no truthful source for them yet. Classic has them; we decline
them until we can compute them honestly. A fabricated number in a diagnostics panel is worse than an
absent one, because the user will believe it.

When a real source lands (a GPU timestamp query, an audio-callback xrun counter), extending
`HealthSnapshot` is a two-line change and the panel picks it up. That is the right order.

## Consequences

**Good.** The single highest ratio of user-visible value to engineering cost in this set — it is
presentation over data that already exists. It makes ADR-0016 (shaders as content) actually usable,
because a shader author who cannot see a compile error cannot author a shader. It gives ADR-0018 its
startup surface for crash reports. And it removes the app's current worst behavior: displaying stale
output while silently swallowing the reason.

**Costs.** A logging layer is new surface area, and a bad one is worse than `fprintf` — it must not
allocate on the audio thread, and it must not become a second source of truth for errors. Keep it
thin: a level, a message, a ring buffer, and a pass-through to stderr.

**Risk.** Badge/toast fatigue. If every transient hiccup toasts, users stop reading toasts. The
severity rollup already draws the line for us — `Error` interrupts, `Warning` is passive (a colored
dot), and informational events go only to the log.

## Implementation

### E1 — Node error badges

Error state in `app/src/ui/node_canvas.h`; sources wired from `ShaderLibraryEntry::error`,
`HealthSnapshot::missing_ops`, and descriptor validation codes. Click → message.

*Verify:* run the app. Load a project that references a deleted operator — the node must **look**
broken and say what's missing. Introduce a syntax error into a `.wgsl` in the shader library and
confirm the node badges with the compiler's message while the last-good version keeps rendering.
Confirm the badge appears in the audio graph too, with no audio-graph-specific code.

### E2 — Health status indicator

`severity()` → a header dot. Green/amber/red, with the rollup's own semantics.

*Verify:* force `gpu_errors > 0` and confirm amber; remove an operator dylib from a saved graph and
confirm red.

### E3 — Diagnostics panel

Render `HealthSnapshot`. Missing ops clickable → select the node.

*Verify:* open it against a healthy session and a broken one; confirm the panel content matches what
MCP `get_health` returns for the same state (one source, two views).

### E4 — Logger + toasts

A leveled logger feeding both stderr and a ring buffer; a log view; a toast channel gated on
severity. **RT-safety:** the audio thread must not allocate or lock — it publishes into a lock-free
ring, exactly as the existing MIDI input path does (`app/src/platform/midi_input.h`).

*Verify:* run under the audio RT test to confirm no allocation on the audio path. Then run the app,
trigger a failed project load, and confirm a toast appears *and* the log records it *and* stderr
still shows it.
