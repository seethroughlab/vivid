# ADR-0025: C++17 Organization and Engineering Patterns

Status: proposed

Date: 2026-07-18

Extends [ADR-0011](ADR-0011-reboot-product-architecture.md),
[ADR-0017](ADR-0017-every-edit-is-reversible.md),
[ADR-0018](ADR-0018-a-bad-operator-must-not-cost-you-your-work.md),
[ADR-0019](ADR-0019-nothing-fails-silently.md),
[ADR-0022](ADR-0022-session-audio-graph.md), and
[ADR-0023](ADR-0023-shared-graph-ui-substrate.md).

## Context

Vivid's native app is built as C++17: `app/CMakeLists.txt` sets `CMAKE_CXX_STANDARD 17`
and requires it. The codebase now spans several hard runtime domains: real-time audio,
GPU rendering, native UI, plugin hosting, loadable operators, package/hot-reload workflows,
MCP control, persistence, undo/redo, and crash recovery.

The current organization is not a small-library shape. It is a native application shape:
`app/src/` is split by domain (`app`, `audio`, `gpu`, `ui`, `cli`, `platform`,
`packages`, `operator_api`, `midi`), with an explicit `App`/`Window` split documented in
`app/ARCHITECTURE.md`.

The question this ADR records is whether the codebase is organized around durable C++17
practices and recognizable patterns, and where the remaining pressure points are.

## Assessment

The codebase is reasonably well organized for a real-time native app. The strongest
parts are the explicit architecture docs, the App/Window split, the documented audio-thread
safety model, the stable control-server error vocabulary, and the focused headless tests.

Vivid follows many practical C++17 practices:

- **C++17 as the baseline.** The app requires C++17 in CMake.
- **Standard-library facilities.** The code uses `std::filesystem`, `std::optional`,
  `std::atomic`, `std::mutex`, `std::lock_guard`, `std::unique_ptr`, `std::shared_ptr`,
  `std::string_view`, and `std::clamp`.
- **Scoped enums and explicit modes.** UI modes, graph edge kinds, severity, fit modes,
  shader dialects, and operator states use `enum class` where that helps.
- **RAII and non-copyable wrappers around system resources.** GPU, renderer, operator-loader,
  platform, and plugin-facing types use destructors, deleted copy constructors, and move support
  where appropriate.
- **Explicit real-time threading rules.** The audio callback avoids blocking and allocation by
  using atomics, SPSC queues, generation counters, retired lists, and non-blocking `try_lock`.
- **Stable machine-readable errors.** The control surface reports stable `code` values and prose
  `error` strings, so agents and clients can branch on codes instead of text.
- **Focused tests at subsystem boundaries.** Mapping, graph topology, audio graph behavior,
  operator loading, package compile, undo, persistence, shader metadata, crash recovery, and
  control JSON all have headless coverage.

The code also uses well-known programming patterns:

- **Application/document plus window/view split.** `App` owns shared engine and document state;
  `Window` owns per-view interaction, layout, selection, and renderer/editor state.
- **Facade.** The MCP/control server and session C API expose smaller surfaces over complex
  subsystems.
- **Command sink.** `EditGateway` centralizes edit notification, undo snapshots, dirty state,
  and audit behavior.
- **Snapshot / memento.** `UndoManager` stores labeled canonical document snapshots for undo/redo.
- **Registry plus factory.** `OpRegistry` creates operators through factories returning
  `std::unique_ptr<OperatorBase>`.
- **Adapter / wrapper.** `GpuContext`, `OperatorLoader`, platform stubs, and plugin wrappers
  isolate C APIs and platform SDKs.
- **Producer/consumer queue.** The control server queues HTTP work onto the UI thread; audio param
  queues move UI edits to the audio thread.
- **Strategy by data and descriptors.** Operators, params, shader metadata, plugin catalogs, and
  semantic tags describe behavior so UI, MCP, and package flows can share one model.

## Decision

Keep C++17 as the current baseline and continue organizing the native app around the existing
domain modules and explicit runtime boundaries:

1. **Keep the App/Window split.** `App` remains shared engine/document state. `Window` remains
   per-view interaction and layout state.

2. **Keep mutations on the UI/main thread.** Control-server and MCP work must continue to enqueue
   and apply through the main-thread path.

3. **Keep real-time audio code allocation-free and non-blocking.** New audio-reachable code must
   follow `app/docs/thread-safety.md`: atomics, SPSC queues, generation-counter publication,
   retired lists, and `try_lock` skip-on-contention behavior.

4. **Keep stable control errors.** New control and MCP failures should use the named error-code
   vocabulary rather than ad hoc strings.

5. **Prefer RAII and explicit ownership at new boundaries.** Use `std::unique_ptr`, wrappers,
   non-copyable resource types, and `std::optional`/structured results where they fit. Raw pointers
   are acceptable for non-owning links and C/SDK interop, but the ownership rule must be obvious
   from the surrounding type or docs.

6. **Keep subsystem tests close to behavior.** New graph, audio, package, persistence, and MCP
   behavior should get headless tests unless it truly requires GUI/GPU/audio hardware.

7. **Reduce large state hubs incrementally.** Do not rewrite the app for style. Instead, split
   pressure points when adding related work.

## Pressure Points

The remaining organizational costs are concentrated rather than pervasive:

- **`audio/vst3_host.cpp` is too large.** It carries plugin hosting, session state, graph adaptation,
  CLAP/VST handling, dynamic tracks, audio-graph API, and real-time publication logic. It is the
  highest-value file to split by cohesive behavior.

- **`Window` is a large interaction-state bag.** It is understandable because much state is genuinely
  per-view, but it mixes clip-grid, plugin browser, audio graph, visual graph, popovers, diagnostics,
  typing, pop-out windows, and drag state. As ADR-0023's graph substrate matures, more interaction
  ownership should move into persistent view/controller objects.

- **Control handlers can become dense.** Handler-family files are a good direction, but the audio
  handlers in particular should keep moving validation, serialization, and graph/session helpers into
  testable local helpers.

- **Raw pointers remain common around the app shell.** Many are non-owning or SDK-driven and are
  acceptable, but new code should avoid adding ambiguous ownership.

## Alternatives Considered

- **Upgrade the baseline to C++20 now.** Rejected for now. C++17 is sufficient for the current
  architecture, dependencies, and CI posture. A C++20 move should be justified by concrete benefits
  such as `std::span`, concepts for operator descriptors, `std::jthread`, or better ranges usage.

- **Rewrite into a more object-oriented framework.** Rejected. The current domain split and C-style
  SDK interop are appropriate for audio, GPU, and plugin hosting. A broad rewrite would risk working
  real-time behavior and undo/control invariants.

- **Keep all large files as-is indefinitely.** Rejected. The system is coherent, but several files
  are past the point where newcomers can easily reason about them. Split them opportunistically along
  real ownership boundaries.

## Consequences

- **Positive:** The codebase keeps a clear native-app architecture without churning proven runtime
  paths.
- **Positive:** New work has a practical C++17 checklist: RAII, explicit ownership, stable errors,
  main-thread mutation, real-time safety, and focused tests.
- **Tradeoff:** Some raw-pointer and C API patterns remain necessary because the app integrates with
  GLFW, WebGPU, miniaudio, VST3, CLAP, Objective-C, and loadable dylibs.
- **Follow-up:** Use future work on audio graph, graph UI substrate, plugin hosting, and MCP tools
  to split the large files and move interaction state into smaller persistent owners.
