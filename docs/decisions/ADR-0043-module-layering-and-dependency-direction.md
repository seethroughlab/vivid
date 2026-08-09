# ADR-0043: Module Layering and Dependency Direction

Status: accepted

Date: 2026-07-31 (accepted 2026-08-09)

> **Origin.** Raised by the first-release Code Audit, Phase 1 (Architecture & Ownership
> Boundaries), finding **P3-01**. See
> `docs/audits/07-31-2026/code/phase-01-architecture-and-ownership-boundaries.md`.

## Accepted decision (2026-08-09)

The candidate stack below is ratified, and enforcement **option (c)** — a lightweight CI
include-linter — is now in place (`tools/check_module_layering.py`, wired into
`version-guard.yml`). Option (b), the CMake object-library split, remains deferred (high churn,
low current value) and can layer on top later without changing this policy.

**Layer stack** (higher = higher in the stack; an `#include "<module>/…"` is legal iff it points to
an **equal-or-lower** rank — edges point down):

| Rank | Module(s) | Role |
|---:|---|---|
| 50 | `cli` | control adapter (top) |
| 40 | `app` | composition / shell |
| 30 | `ui` | view |
| 20 | `gpu`, `audio` | engine (peers) |
| 15 | `packages` | package loader/compiler service |
| 10 | `platform` | downward-only OS seam |
| 0 | `operator_api`, `midi` | leaf SDK / leaf |

`packages` is placed just **below** the engine — the one rank the original stub left open. This is
deliberate: it makes the reported `gpu ↔ packages` cycle resolve cleanly as `packages → gpu` being
the back-edge to remove (`gpu` may use `packages/file_watcher.h` downward; `packages` must not reach
up into `gpu`).

**Enforcement is a ratchet, not a big-bang cleanup.** The 28 pre-existing upward includes the audit
flagged (`ui → app` ×12, `audio → app` ×4, `app → cli` ×4, `packages → gpu` ×3, `gpu → app` ×2,
`gpu → ui`, `packages → app`, `ui → cli`) are grandfathered in `tools/module_layering_baseline.txt`.
The linter fails only on a **new** wrong-direction include; fixing an existing edge and deleting its
baseline line shrinks the debt. Decision #2 below (break cycles opportunistically) is unchanged — the
baseline is what "opportunistically" is measured against.

---

_Original stub (the decision to make), retained for context:_

## Context

`app/src/` is split into domain modules (`app audio gpu ui cli platform midi packages
operator_api`), but the split is **directory convention only**. The Phase-1 audit found:

- **Mutually-recursive core:** `app/ ↔ ui/ ↔ gpu/` cycle at the header level in both
  directions (e.g. `ui/node_graph.cpp:2` → `app/edit_gateway.h`; `gpu/splash.cpp:4` →
  `ui/renderer_2d.h`; `gpu/visual_graph.cpp:2` → `app/crash_guard.h`).
- **`gpu/ ↔ packages/` header cycle:** `gpu/shader_library.h:10` includes
  `packages/file_watcher.h`, so the cycle propagates transitively to every includer of
  `shader_library.h` (ui, app, cli).
- **`audio/ → app/` upward include:** `audio/audio_callback.cpp:3` `#include "app/app.h"`
  — intentional (the audio thread's `device->pUserData` is an `App*`), but it pulls the
  full `App` header into the RT translation unit.
- **A `cli/` second hub:** `control_handlers_internal.h:8` hard-includes
  `audio/vst3_host.h`, and handlers reach directly into audio/gpu/ui/app internals.
- **One monolithic build target:** `add_executable(vivid ...)` (`app/CMakeLists.txt:198`,
  ~125 sources) with wide-open include dirs (`:389`). There is **no compiler- or
  link-level enforcement** of any boundary.

Consequence: a small change can transitively pull in unrelated modules, and nothing
prevents a new "wrong-direction" edge from being added. This is maintainability debt, not
a correctness bug — no user impact — but the first release is the moment to decide the
intended layering before more edges accrete.

## Decision (to be made)

Codify the allowed dependency direction and decide whether/how to enforce it. Candidate
shape (to be ratified):

1. **Named layers, one direction.** e.g. `operator_api`, `midi` = leaves; `platform` =
   downward-only seam; `gpu`, `audio` = engine; `ui` = view; `app` = composition/shell;
   `cli` = control adapter. Edges point down the layer stack only.
2. **Break the specific reported cycles opportunistically** (per ADR-0025 Decision #7 —
   split when a file is next touched, not as a style rewrite): move the small shared
   headers (`app/crash_guard.h`, `app/log.h`, `packages/file_watcher.h`) into a lower
   shared/foundation layer so the `→ app/` and `gpu/ ↔ packages/` back-edges disappear.
3. **Enforcement — pick one:** (a) leave as convention + a documented layer diagram; (b)
   split modules into CMake `OBJECT`/`STATIC` libraries with explicit `target_link_libraries`
   so wrong-direction edges fail to link; (c) a lightweight CI include-linter. Option (b)
   is the strongest but the most churn.

## Consequences

- **Positive:** newcomers get an enforced mental model; release fixes stay local.
- **Tradeoff:** a CMake object-library split is real churn against a working build and
  must not disturb the RT audio / undo / control invariants; likely staged, not one PR.
- **Non-blocking:** first release can ship on convention + this ADR; enforcement can land
  incrementally afterward.

## Alternatives Considered

- **Do nothing / keep convention only.** Viable for release, but re-litigated each time a
  new cross-module edge is proposed; the audit recommends at least a written layer policy.
- **Full CMake library split now.** Rejected as first-release scope — high churn, low
  user value at this moment; revisit post-release.
