# ADR-0020: The Inner Loop Is Visible and Always On

Status: proposed

Date: 2026-07-14

Amends: [ADR-0016](ADR-0016-shaders-are-content.md) (a shader file is an operator) by supplying the
authoring loop it presupposes. Depends on [ADR-0019](ADR-0019-nothing-fails-silently.md) — the build
console *is* an error surface.

Decided: hot reload stops being an env-var-gated developer secret and becomes **the default,
visible authoring loop**. The file watcher runs always. A **build console** shows what is compiling
and what went wrong. A failed reload **rolls back to the last good version** rather than degrading.
And "Edit in IDE" is one click from any node.

## Context

ADR-0016 makes a `.wgsl` file an operator, so that *"authoring an effect requires a compiler"* stops
being true and shader authors — most of whom are not C++ programmers — can extend Vivid.

But a shader author needs a loop: **edit → see it → see the error when it's wrong.** Without that,
"shaders are content" delivers a file format and no way to work in it.

Two-thirds of that loop already exists in the trunk, and the third is invisible.

**What works.** `app/src/gpu/shader_library.cpp` already does the right thing, and it is the model
for everything else here: it watches file mtimes, reloads on edit, distinguishes a body edit from an
interface edit, and — crucially — on a failed edit sets `Failed` status with an error string while
*"the last good version still runs"* (`shader_library.h:43`). It even has `fork()`, to copy a shipped
shader into a user-editable one. That is a well-designed live-coding substrate.

**What is hidden.** The `Failed` status and the error string it carries are shown to **nobody**. ADR-0019
fixes the display; this ADR makes sure there is something worth displaying for the C++ path too.

**What is switched off.** The full operator hot-reload machinery is *built and tested* —
`app/src/packages/hot_reload.{h,cpp}`, `hot_reload_manager.{h,cpp}`, `file_watcher.{h,cpp}`, with
background compile, main-thread dylib swap, param preservation across reload, and a passing
`app/tests/test_hot_reload.cpp`. And it is reachable only like this:

```cpp
// app/src/main.cpp:186
// Hot-reload (opt-in/dev): point VIVID_WATCH_PACKAGE at a package dir to install
if (const char* wp = std::getenv("VIVID_WATCH_PACKAGE")) { ... }
```

An environment variable. No UI, no menu, no indication it exists or is running. A capability the team
built, tested, and then hid from its users.

**What is missing entirely.** Where compile output goes. Right now, a package rebuild's stdout and
stderr go to the terminal — which, for a user who launched Vivid from Finder, is *nowhere*. Classic
solved this with `src/runtime/core/build_console.h` (streamed stdout/stderr per build task, with
task kinds and states) and `src/ui/build_console_panel.{h,cpp}` (auto-surfaces on build start,
extracts the top error lines).

## Decision

1. **The watcher is on by default.** Remove the `$VIVID_WATCH_PACKAGE` gate. Vivid watches the
   package source dirs and the shader library at all times. The env var survives only as an override
   for pointing at a source tree outside the standard locations.

2. **Rollback-first is the contract.** A reload that fails must leave the running graph **exactly as
   it was** — the old dylib's instances keep running, with their params. This is what
   `ShaderLibrary` already does for shaders and what classic does for dylibs (recreating instances
   from the *old* loader on failure). The trunk has the classification
   (`HotReloadCompat{Compatible, RecompileRequired, Incompatible}` in
   `app/src/gpu/operator_loader.h:23`) but should adopt the explicit old-loader fallback. **A failed
   edit must never be able to break a working session.**

3. **A build console.** Lift classic's model: a `BuildTask` with a kind (hot-reload, package build,
   package install), a state (running / succeeded / failed), and streamed stdout/stderr. A panel that
   auto-surfaces when a build starts and fails, bound to Cmd+Shift+B. Error lines extracted and
   routed to the offending node's badge (ADR-0019's E1) so the failure appears **where the user is
   looking**, not only in a panel they'd have to know to open.

4. **Edit in IDE.** A settings-held editor command with a `{file}` template (classic:
   `Settings.editor_command`, `open_in_editor()`). Right-click a node → open its source. For a
   shader-file operator that is the `.wgsl`; for a compiled operator it is the `.cpp`. Pairs directly
   with `ShaderLibrary::fork()`, which already exists and has no caller: **fork a shipped shader,
   open it in your editor, save, see it live.** That is the loop, and every piece of it is already in
   the tree except the two clicks that connect them.

### Boundary rule — what this is not

- **Not an in-app code editor.** Vivid opens your editor. It does not become one. (Classic's
  `Live REPL` was deferred past 1.0 for the same reason.)
- **Not hot reload of the app itself.** Operators and shaders reload; the host does not.
- **Not a guarantee that any edit is safe.** An `Incompatible` descriptor change still requires a
  restart. The contract is that we *detect* it and say so — not that we hot-swap anything.

## Consequences

**Good.** This is what makes ADR-0016 real. It also un-hides a capability the team already paid for:
the hot-reload machinery is built, tested, and currently benefiting nobody. And the rollback-first
contract means the live-coding loop is *safe* — you cannot lose a working session to a typo.

**Costs.** Turning the watcher on by default means it runs in every session, including for users who
never author an operator. It must therefore be cheap (the shader watcher is already an mtime stat per
frame, which is the right budget) and it must never surface noise to a user who isn't authoring —
a build console that pops up unprompted for a non-author is a bug.

**Risk.** The main one is a reload storm: an editor that writes a file in multiple chunks, or a build
system touching many files, can trigger repeated recompiles. Classic handles this with dedup and
deferred requeue in the build queue. Adopt that, don't rediscover it.

## Implementation

### L1 — Watcher on by default + rollback-first reload

Remove the `$VIVID_WATCH_PACKAGE` gate in `app/src/main.cpp:190` (keep it as an override). Adopt the
explicit old-loader fallback in `app/src/gpu/operator_loader.cpp` so a failed reload preserves the
running instances. Dedup / deferred-requeue in the build queue.

*Verify:* run the app with a graph using a package operator. Introduce a **compile error** in its
source, save. The app must keep rendering the last good version, unchanged, with its params intact —
and must not spin on repeated rebuild attempts. Then fix the error, save, and confirm it swaps in
live with params preserved. Extend `app/tests/test_hot_reload.cpp` to cover the failure→rollback
path.

### L2 — Build console

`app/src/app/build_console.h` (task kind, state, streamed output — pure data, headless-testable) +
a panel in `app/src/ui/`. Auto-surface on failure. Cmd+Shift+B.

*Verify:* headless test over the model (task lifecycle, output accumulation, error-line extraction).
Then run the app, break a package source, and confirm the console surfaces itself with the compiler's
actual message.

### L3 — Compile errors on the node badge

Route extracted error lines to ADR-0019's node error badges, so a broken operator is visibly broken
in the graph, not only in a panel.

*Verify:* break a shader; confirm the node badges with the WGSL compiler's message *and* the console
holds the full output. Confirm the same for a C++ operator.

### L4 — Edit in IDE + fork-to-edit

Editor command in settings; right-click → Edit Source. Wire `ShaderLibrary::fork()` to a "Fork to
edit" action on a shipped shader node.

*Verify:* run the app — right-click a shipped shader node, fork it, confirm the new `.wgsl` lands in
the user shader dir, opens in the configured editor, and that saving an edit to it live-updates the
node. That is the whole loop, end to end; making it work completes the authoring story ADR-0016
(shipped) began.
