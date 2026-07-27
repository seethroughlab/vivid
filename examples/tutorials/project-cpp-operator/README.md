# Project-local C++ Operator

ADR-0040 tutorial **tier 3** — authoring a *compiled* operator that lives in your project. Where a
project-local shader is a `.wgsl` file (tier 2), a project-local operator is a C++ `.cpp` + a
`vivid-package.json` manifest that Vivid compiles with a real `clang++` into an operator scoped to the
folder project: registered on load, retired on New / project switch (the `examples/song-sketch` model).

It is self-contained — the builder scaffolds its own operator, so you write no C++ by hand — and it
teaches the full compiled-operator loop over MCP, including the extra safety rails the C++ path adds
beyond shaders: a real compiler, ABI checks, and crash quarantine.

## Prerequisites

- Vivid running — a signed release (`/Applications/Vivid.app`) or a dev build — with the control server
  on `127.0.0.1:9876` (`VIVID_DISCARD_RECOVERY=1` for a disposable run; set `VIVID_PORT` to change it).
- **Xcode Command Line Tools** (`xcode-select --install`) — Vivid builds project-local C++ operators
  with the **system** `clang++`; the compiler is **not** bundled in the signed app (this matches how
  Vivid has always built operator packages — the shader tutorials, by contrast, need no toolchain).

Check readiness before you start — the preflight verifies clang++ **and** that the app bundle ships the
operator headers + `libwgpu_native`:

```json
{"method": "check_tutorial_prereqs", "tutorial": "project_cpp_operator"}
// → { "ready": true, "checks": [ {"name":"cxx_compiler","status":"pass", ...}, ... ] }
```

If a piece is missing it reports the gap + a `next_actions` install hint (e.g. `xcode-select --install`)
before you scaffold or build anything.

## Run It

```sh
uv run examples/tutorials/project-cpp-operator/build.py
```

It performs every step below and writes `project/cpp-op-proof.json` + `project/FRICTION-LOG.md`.

## Step 1 — Scaffold a project operator package

```json
{"method": "scaffold_operator_package", "name": "GlowPulse", "kind": "gpu_visual",
 "path": "<project_dir>"}
```

This writes two files into your project folder and self-validates them:

- `GlowPulse.cpp` — a known-good starter: a GPU generator (`OperatorBase` + `GpuProcessable`) with WGSL
  and two params (`hue`, `bright`).
- `vivid-package.json` — the manifest naming the operator and its source.

## Step 2 — Validate + build (real clang++)

```json
{"method": "validate_operator_package", "path": "<project_dir>"}   // sources exist on disk?
{"method": "build_operator_package",    "path": "<project_dir>"}   // compiles with clang++
```

`build_operator_package` returns per-operator `{name, compiled, dylib | error}` and `ok_all`. Then
register it *into the project* so it is scoped to this folder and marked as project-owned:

```json
{"method": "reload_project_files"}   // compiles the package + registers newly-authored ops
```

## Step 3 — Use it + discover its origin

```json
{"method": "add_node", "op": "GlowPulse"}     // then connect -> Output, set_active_output
```

Discover what `GlowPulse` is over MCP. The catalog now marks a compiled project operator's origin, so
it is not indistinguishable from a built-in:

```json
{"method": "find_operators", "query": "GlowPulse", "domain": "visual"}
// → { "name":"GlowPulse", "format":"compiled_operator", "source":{"tier":"project"}, ... }
```

Its `.cpp` source is enumerable via `list_project_assets`, and `validate_operator_package` maps each
operator name to its source file.

## Step 4 — Break it, then recover (the C++ failure mode)

Compiled code fails to compile — that is the failure mode this tier covers. Introduce a C++ error in
`GlowPulse.cpp` (e.g. call an undeclared function), then build:

```json
{"method": "build_operator_package", "path": "<project_dir>"}
```

The response reports it, with the **verbatim clang++ output** in the per-operator `error` field:

```json
{ "ok_all": false,
  "operators": [ { "name": "GlowPulse", "compiled": false,
                   "error": "GlowPulse.cpp:97:9: error: use of undeclared identifier ..." } ] }
```

Fix the source and build again — `ok_all` returns to `true`. And `run_quality_check` with
`no_quarantined_operators` confirms nothing crashed into quarantine (a repeatedly-crashing operator is
quarantined after ADR-0018 and skipped on load, so a bad operator never costs you your project).

## Step 5 — Edit + recompile on load

Compiled code's deterministic live-edit path is to edit the source and reload the *project* — Vivid
recompiles from source and rebuilds the nodes:

```json
{"method": "save_project", "path": "<project_dir>"}
{"method": "load_project", "path": "<project_dir>"}
// → project_package.operators: [ { "name":"GlowPulse", "compiled":true, "registered":true } ]
```

On load, the operator is compiled from source **before** the graph restores, so a node naming it
resolves. (`AuroraField.dylib` in `song-sketch` is a git-ignored build artifact for the same reason —
the project stays portable.)

## Proof & Friction

- `project/cpp-op-proof.json` — every step's response (scaffold, validate, build, register, catalog
  origin, the compile error + its fix, recompile-on-load, quarantine check).
- `project/FRICTION-LOG.md` — what worked and the known gaps.

## Known Gaps (honest notes)

- **Hot-swapping an already-live compiled operator is not done over MCP.** `reload_operator_package`
  and `reload_project_files` re-register the *type* but do not rebuild an already-live compiled-op
  node in place — unlike the shader tier, where `reload_project_files` rebuilds live shader nodes.
  Hot-swapping a live dylib safely (RTLD lifecycle, and real-time audio operators) is a separate,
  riskier change. The deterministic way to see a compiled edit today is `load_project` (recompiles +
  rebuilds nodes) or the source file-watcher in a focused session.
- **Visual verification is best-effort in a headless run.** A freshly-recompiled dylib operator
  initializes its GPU pipeline on first draw; when the app is driven headlessly by rapid synchronous
  control calls, the main-thread frame loop is starved and a capture can read blank even though the
  operator renders correctly once the app is idle/focused. The builder therefore asserts the
  compiled-operator *loop* (build/register/recover/recompile) hard, and treats the rendered frame as
  best-effort evidence.

## Why This Is The Last Tutorial Tier

Shaders (tier 2) prove the creative-code loop with no toolchain. This tier adds the compiler, ABI
guard (currently v14, floor v11), and crash quarantine — everything a project needs to ship its own
compiled operators safely.
