# The classic → trunk platform gap

Companion to [ADR-0011](../decisions/ADR-0011-poc-to-product-architecture.md) and
[poc-to-product.md](poc-to-product.md), which set the strategy: **keep `app/` as the trunk and adopt
vivid-classic's platform by selective lift.** That document scoped the *engineering* lift (ABI,
loader, packages, tests, CI). This one scopes what remains: the **product** lift — the application-
level machinery and UX affordances that make the thing usable.

Date: 2026-07-14 · Scope: `app/` benchmarked against `vivid-classic`. (Originally assessed on branch
`feature/shaders-as-content`, since merged to `main`.)

> **Status update (2026-07-15).** Three of the five decisions have shipped to `main`: **ADR-0016**
> (shaders are content, PR #28), **ADR-0021** (content is browsable, PR #29), and **ADR-0017** (every
> edit is reversible — app-level undo/redo, PR #31). PR #30 added the curated VST3/CLAP param inspector
> + splitter/grid fixes. The rows below are annotated inline; the remaining gap is **ADR-0018
> (resilience) → ADR-0019 (error surfaces) → ADR-0020 (inner loop)**. See §3–§4.

**Filter applied.** Only features that (1) fit the split audio/visual graph model, (2) do **not**
expand the operator set, and (3) contribute to stability, functionality, or usability. That filter is
what makes this list actionable rather than a wish list — see [§5](#5-deliberately-excluded).

---

## 1. The finding: plumbing without presentation

The P0–P4 productization work landed. The trunk has the operator ABI and loader, the package system,
hot reload *with a working file watcher*, descriptor validation, named control-server error codes, a
runtime-health severity rollup, 46 headless tests, a production gate, and CI.

And yet: **the engine knows about failures it never tells the user about.** It knows a shader failed
to compile and why. It knows a graph node references an operator that no longer exists. It knows the
GPU has logged uncaptured errors. It rolls all of this up into a `Severity`. Then it writes to
`stderr` and answers MCP — and draws nothing.

Grepping `error|missing|broken` across `app/src/ui/node_graph.cpp` and `app/src/ui/session_view.cpp`
returns **nothing**.

That inversion — excellent plumbing, near-zero presentation — is the shape of the whole gap, and it
is good news: most of the work below is *surfacing* state that already exists, not building new
subsystems.

*(Update: content browsing (ADR-0021) and undo (ADR-0017) have since closed their part of this gap.
The finding still holds for the **error-surface** work — ADR-0019's health rollup, diagnostics, and
build console remain computed-but-undrawn.)*

### 1a. Already built, no UI (the highest-leverage list)

| Exists in the trunk | What it already knows | Who consumes it today |
|---|---|---|
| `app/src/app/runtime_health.h` | `gpu_ok`, `gpu_errors`, `gpu_last_error`, `missing_ops`, `packages_loaded`, `control_running` + a pure `severity()` rollup | MCP `get_health` only |
| `app/src/gpu/shader_library.h` | Per-shader `error` string; a failed shader **still gets a catalog row carrying why**; `Failed` reload status with last-good fallback; `fork()` | now drawn by `app/src/ui/shader_library_view.cpp` (ADR-0021) |
| `app/src/packages/hot_reload.*`, `hot_reload_manager.*`, `file_watcher.*` | Background recompile + main-thread dylib swap with param preservation | gated behind `$VIVID_WATCH_PACKAGE` (`app/src/main.cpp:190`) |
| `app/src/operator_api/operator_descriptor_validation.h` | Named codes (`kDuplicateParamName`, `kDuplicatePortName`, …), validated loudly at startup | `stderr` |
| `app/src/cli/control_errors.h` | A fixed error vocabulary (`bad_arg`, `out_of_range`, `not_found`, `invalid_descriptor`, …) | MCP responses |
| `app/src/ui/node_canvas.h` | The **shared** draw substrate — used by *both* `node_graph.cpp` and `audio_node_graph.cpp` | both graphs (so anything added here lands in both, once) |
| `app/src/persist.cpp` | Complete `session_to_json` / `session_from_json` in-memory round-trip, schema v2, tested migration | save/load + MCP |

The last two are load-bearing for the ADRs below: `persist.cpp` made snapshot undo nearly free — now
realized in `app/src/persist_undo.*` + `app/src/app/undo_manager.*` (ADR-0017, shipped) — and
`node_canvas.h` means badges, marquee, and multi-select are written once and appear in both graphs.

---

## 2. The gap, by category

Legend: **✅** solid · **◐** partial · **✗** absent.

### 2a. Graph editing — the largest gap

| | Trunk | Classic | Owner |
|---|---|---|---|
| Undo / redo | ✅ **Shipped (PR #31).** Whole-document undo via the `EditGateway` command sink (`app/src/app/edit_gateway.*` + `undo_manager.*` + `persist_undo.*`): every UI + MCP edit routes through it; smart tiered restore (a param undo never reloads plugins). Cmd+Z / Cmd+Shift+Z / Cmd+Y, native Edit menu, MCP `undo`/`redo`. | ✅ `src/runtime/core/undo_manager.{h,cpp}` — 200-deep labeled JSON snapshots, Cmd+Z, menu, HTTP `undo`/`redo` | [ADR-0017](../decisions/ADR-0017-every-edit-is-reversible.md) ✅ |
| Multi-select | ✗ Single selection only — `sel_op_` (`node_graph.h:147`) and `sel_node_` (`audio_node_graph.h:99`) are one `int` | ✅ `selected_node_ids_` set, marquee, group drag | ADR-0017 (deferred follow-up — 0017 shipped undo/redo only) |
| Copy / paste / duplicate | ✗ Notes only | ✅ `copy_selected_nodes` / `paste_copied_nodes`, Cmd+C/V | ADR-0017 (deferred follow-up) |
| Delete key on a graph | ✗ Click-target only; no Delete/Backspace binding | ✅ | ADR-0017 (deferred follow-up) |
| Auto-layout | ✅ `NodeGraph::layout_nodes()` + MCP `layout_graph` | ✅ | — |
| Tab palette / chooser | ✅ `app/src/ui/chooser.{h,cpp}` — shared by both graphs, ranked, badged, greys-out disabled rows *with reasons* | ✅ | — |
| Bypass / solo | ✗ | ✅ `B` / `S`, solo does a BFS over upstream | follow-on |
| Sticky notes | ✗ | ✅ used to teach in the shipped example graphs | follow-on |
| Node thumbnails | ✅ | ✅ | — |

### 2b. Failure legibility

| | Trunk | Classic | Owner |
|---|---|---|---|
| Node error badge / health color | ✗ | ✅ `src/ui/graph/health_color.h` | [ADR-0019](../decisions/ADR-0019-nothing-fails-silently.md) |
| Diagnostics panel | ✗ | ✅ `draw_diagnostics_panel()` | ADR-0019 |
| Status banner / toasts | ✗ | ✅ `draw_status_banner` | ADR-0019 |
| Log view | ✗ Everything is `fprintf(stderr, "[vivid] …")` — no levels, no file, no view | ✅ | ADR-0019 |
| Build console | ✗ | ✅ `src/runtime/core/build_console.h` + `src/ui/build_console_panel.{h,cpp}` | [ADR-0020](../decisions/ADR-0020-the-inner-loop-is-visible.md) |
| Named error codes | ✅ `control_errors.h` | ✅ | — |
| Descriptor validation | ✅ | ✅ | — |
| Health rollup | ◐ computed, never drawn | ✅ | ADR-0019 |
| Per-node timing / FPS | ✗ — **deliberately.** `runtime_health.h:14`: *"intentionally absent until there's a real source for them."* | ✅ | **declined** |

### 2c. Crash resilience & work safety

| | Trunk | Classic | Owner |
|---|---|---|---|
| Crash attribution | ✗ A segfault in a third-party dylib is an anonymous SIGSEGV | ✅ `crash_guard.h` — RAII + thread-local current-operator; the handler prints *"crashed in MyBrokenOp"* | [ADR-0018](../decisions/ADR-0018-a-bad-operator-must-not-cost-you-your-work.md) |
| Crash record / history | ✗ | ✅ `crash_recovery.{h,cpp}` — signal, operator, node, package + version; 20-entry history | ADR-0018 |
| Safe mode | ✗ | ✅ `safe_mode.h` + `--safe-mode` | ADR-0018 |
| Quarantine | ✗ | ✅ `quarantine.{h,cpp}` — 3 crashes in 24 h disables an operator type | ADR-0018 |
| Autosave | ✗ | ✗ (classic doesn't have it either) | ADR-0018 |
| Dirty flag / save-confirm | ✗ New/Open/Quit silently discard work (`dirty_` exists only in `ClipEditor`) | ✅ | ADR-0018 |
| Hot-reload rollback | ◐ `HotReloadCompat` classifies, but no explicit old-loader fallback | ✅ recreates instances from the *old* loader on failure | ADR-0020 |
| Plugin crash isolation | ✅ `--probe-plugin` re-exec subprocess | ✅ | — |

The crash chain matters concretely: a repeat-offender operator currently produces a **crash loop on
relaunch**, a failure mode this project has already hit.

### 2d. Content & authoring loop

| | Trunk | Classic | Owner |
|---|---|---|---|
| Asset library | ◐ deferred by design — 0021's "Correction" split the shared `AssetLibrary` off; content browsing shipped without it | ✅ `src/runtime/assets/asset_library*.cpp` — merged package+workspace scopes, hash dedupe, kind metadata | [ADR-0021](../decisions/ADR-0021-content-is-browsable.md) |
| Shader browser | ✅ **Shipped (PR #29).** `app/src/ui/shader_library_view.{h,cpp}` — the shader library view (answers the old `TODO.md` open question) | ✅ (via the asset browser) | ADR-0021 ✅ |
| Examples browser | ✅ **Shipped (PR #29).** File → Open Example picker (`app/src/app/examples.*`) over `examples/demos/projects/{pulse,drift,neon,grid}` | ✅ File → Open Example, tag/difficulty filters | ADR-0021 ✅ |
| File drop onto a graph | ✅ **Shipped (PR #29).** `app/src/gpu/file_drop_registry.{h,cpp}` — operators declare the extensions they handle | ✅ `file_drop_registry.{h,cpp}` — operators declare the extensions they handle | ADR-0021 ✅ |
| Node presets | ✅ **Shipped (PR #29).** Per-operator named param snapshots (`app/src/app/node_presets*.cpp`), distinct from plugin `list_presets`; PR #30 also added the curated VST3/CLAP param inspector | ✅ per-operator `OperatorPreset` + factory presets | ADR-0021 ✅ |
| Hot reload | ◐ real, but env-var-gated and invisible | ✅ default, with a console | ADR-0020 |
| Shader hot reload | ✅ always-on, mtime watch, last-good fallback | ✅ | — |
| Edit in IDE | ✗ | ✅ `Settings.editor_command` + right-click → Edit in IDE | ADR-0020 |
| Package manager | ◐ install only; no uninstall, versioning, registry, or UI | ✅ + a Package Browser (Cmd+Shift+P) | follow-on |

### 2e. Everything else (for the record)

**Solid in the trunk, no action:** the mapping bridge (`app/src/mapping.h`), the control server /
MCP (~85 methods + a Python bridge + eval harness), project save/load/recents with a native menu bar,
schema versioning + migration, the operator ABI + loader + descriptor validation, output identity as
params on the Output node (ADR-0014, including pop-out to a second display), the inspector with
display hints and semantic tags, operator custom editor windows, 46 headless tests + production gate
+ CI.

**Absent in both, or absent-and-deferred:** see [§5](#5-deliberately-excluded).

---

## 3. The five decisions

| ADR | Decision | Status / why now |
|---|---|---|
| [0017](../decisions/ADR-0017-every-edit-is-reversible.md) | Every edit is reversible | ✅ **Shipped (PR #31).** Foundational — undo intercepts every mutation site via the `EditGateway` command sink; 0018's dirty-flag/autosave rides the same sink. |
| [0018](../decisions/ADR-0018-a-bad-operator-must-not-cost-you-your-work.md) | A bad operator must not cost you your work | **Next.** Third-party dylibs are the extension model. Today one of them can crash-loop the app and lose the session. |
| [0019](../decisions/ADR-0019-nothing-fails-silently.md) | Nothing fails silently | Highest leverage of the remaining set: the data already exists, so this is presentation, not plumbing. |
| [0020](../decisions/ADR-0020-the-inner-loop-is-visible.md) | The inner loop is visible and always on | Completes ADR-0016 (shipped). A shader author with no compile-error surface cannot author a shader. |
| [0021](../decisions/ADR-0021-content-is-browsable.md) | Content is browsable | ✅ **Shipped (PR #29).** The other half of ADR-0016; answered the standing `TODO.md` question. |

---

## 4. Sequencing

**Done:** 0017 and 0021 shipped (and 0021 was pulled forward ahead of 0018, exactly as the degree of
freedom below anticipated). **Remaining: 0018 → 0019 → 0020**, with one *hard* dependency:

- **0019 precedes 0020**, and this is forced. The build console is an error surface; building it
  before the badge/toast substrate exists means writing that substrate twice.
- **0018 (resilience) is otherwise independent** and can lead — it rides the `EditGateway` that 0017
  already installed (its dirty-flag/autosave hook is the same sink), so the plumbing it depended on
  now exists.

*(Historical: the original plan was 0017 → 0018 → 0019 → 0020 → 0021, forcing 0017 first because undo
had to intercept every mutation site. That's done; 0021 was taken early since it was independent.)*

---

## 5. Deliberately excluded

| Excluded | Why |
|---|---|
| OSC, MIDI-learn, DMX | In classic these are **operators**. They fail the "don't expand the operator set" filter. (Hardware MIDI *note* input already exists: `app/src/platform/midi_input.mm`.) |
| Standalone binary export | Classic has it (`src/export/`), but it is an output feature, not a usability one — and a large one. Follow-on. |
| Recording / AV export | `capture_coordinator` + `av_exporter.mm`. Genuinely valuable — **the top follow-on** — but output-side rather than usability. You currently cannot get a video out of the app. |
| NDI / Spout / Syphon | Classic ships Syphon only; the rest it never had. Follow-on. |
| Multi-monitor / fullscreen | Largely already solved differently and better by ADR-0014 (pop-out output window with a `display` param). Only an explicit fullscreen toggle is missing. |
| Telemetry / crash reporting to a server | Classic deliberately keeps crash data local. Keep that. |
| Per-node timing, FPS counter | `runtime_health.h:14` declines these until there is a truthful source. ADR-0019 upholds that. |
| **Subgraphs / modules** (`.vivid-module.json`) | The biggest usability multiplier in classic, and the one feature that *reduces* operator-set pressure by letting users compose ops from graphs. Excluded here only because it is a rearchitecture, not a lift. **It should get its own ADR next.** |
