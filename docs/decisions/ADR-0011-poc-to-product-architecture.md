# ADR-0011: PoC → Product — Target the Platform Architecture; Choose the Trunk

Status: proposed

Date: 2026-06-29

Follows: [ADR-0010](ADR-0010-poc-proven-production-seed.md)

## Context

[ADR-0010](ADR-0010-poc-proven-production-seed.md) declared the C++ PoC (`app/`) proven and proposed
promoting it to the production seed. The next question is what "product" means and what it takes to ship
one. The decision: the product should be an **extensible platform** (operators-as-plugins, hot-reload,
packages) and should be **architected for cross-platform**, not macOS-only.

Two audits this session (recorded in [`../roadmap/poc-to-product.md`](../roadmap/poc-to-product.md))
establish the gap:

- The PoC validated the **product** (two best-in-class surfaces + a bidirectional bridge + MCP-native
  control) and several **subsystems** (VST3 host, master transport, the string-keyed mapping registry,
  the generation-counter/SPSC thread-safety discipline, the cpp-httplib control server, `ui_style`).
- But it is **not a product base**: `app/src/main.cpp` is a 1,213-line god file (15+ responsibilities,
  30+ globals); there are **no tests, no CI, no sanitizers**; error handling is sparse (the MCP control
  server passes unvalidated indices to the C API — crash vectors); `app/` has no README/ARCHITECTURE
  docs; and ~20% of the code is macOS-locked with no abstraction seams.

Crucially, the chosen target — *extensible + cross-platform* — **is essentially vivid-classic's
architecture**: an `operator_api`/ABI boundary, a graph compiler, codegen, hot-reload, a package system,
a cross-cadence bridge, test partitioning, a production gate, platform abstraction, and a deep docs
culture. **Classic already has all of this; the PoC has none of it.** So the central decision is not a
task list — it is *which codebase is the trunk*.

## Decision

1. **Target an extensible, cross-platform-capable platform** (ratified by the user). This commits us to
   classic-grade machinery: a plugin/ABI contract, codegen, hot-reload, packages, platform abstraction,
   tests/CI/sanitizers, a production gate, and a documentation culture.

2. **Choose the trunk** (the pivotal, still-open decision — see Alternatives). **Recommendation: Option
   B** — port the PoC's distinctive product layer onto classic's existing runtime — with **Option C**
   (hybrid lift) as the fallback. Rationale: don't re-pay the years of platform engineering classic has
   already proven; the PoC's unique value is the *product* (two-surface DAW-side UX, session/clip model,
   MCP-native control, `ui_style`), not the platform plumbing.

3. **Execute the P0–P4 roadmap** (detailed in [`../roadmap/poc-to-product.md`](../roadmap/poc-to-product.md)):
   P0 engineering hygiene (decompose `main.cpp`, headless tests + CI + sanitizers, index validation +
   named error codes, `app/` docs) → P1 layering + operator/ABI contract → P2 codegen + hot-reload +
   packages → P3 cross-platform seams → P4 release engineering + production gate. **P1+ are gated on the
   trunk decision; P0 is trunk-agnostic and may start immediately.**

## Alternatives Considered (the trunk decision)

- **Option A — Grow the PoC into the platform.** Reimplement the operator ABI, graph compiler, codegen,
  hot-reload, packages, and cross-platform backends inside `app/`, borrowing classic's *designs*. Keeps
  PoC product code central but re-pays years of platform engineering. Highest cost/risk. *Rejected as the
  default.*
- **Option B — Port the PoC's product layer onto classic's runtime (recommended).** Reuse classic's
  proven platform (operator_api / ABI 10, graph compiler, codegen, package system, AudioFrameBridge,
  production gate, CI); rebuild the two-surface UX, session/clip model, MCP surface, and `ui_style` as the
  product layer on top — much of it expressible as operators + a session/UI layer.
- **Option C — Hybrid lift.** Keep `app/` as trunk but vendor classic's hardest-won subsystems wholesale
  (`operator_api`, `tools/operator_codegen`, the package/loader, the cross-cadence bridge, the test/CI
  harness). Middle cost; risks a seam between two designs. *Fallback if B proves awkward.*

## Consequences

- **Positive:** under B/C we inherit classic's meticulous planning (layering, ABI, codegen, packages,
  production gate, docs) instead of recreating it; the PoC's validated product framing and subsystems
  carry forward as the differentiator.
- **Cost / risk:** this is a multi-month, rebuild-scale effort regardless of trunk. Under B, the risk is
  whether the PoC's product layer (DAW-style session view, the bridge UX, MCP-native authoring) maps
  cleanly onto classic's runtime; the roadmap front-loads a P1 spike to de-risk this before committing.
- **Carry-forward vs. rework:** mapping registry, transport, thread-safety patterns, the MCP
  control-server *shape*, and `ui_style` carry forward; `main.cpp`, the fixed `VisualGraph`, and the
  macOS-locked `.mm`/CFRunLoop code are reworked or replaced.
- **Follow-up:** the trunk decision (A/B/C) must be ratified before P1; this ADR flips to **accepted**
  once it is. P0 hygiene can proceed under any trunk.

## Status note

Marked **proposed**: the target (extensible + cross-platform) is ratified, but the **trunk decision
(A/B/C)** is the user's to make. Flip to **accepted** once the trunk is chosen.
