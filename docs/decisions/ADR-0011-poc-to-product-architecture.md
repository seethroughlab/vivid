# ADR-0011: PoC → Product — Keep Our Trunk; Adopt Classic's Platform by Selective Lift

Status: accepted

Date: 2026-06-29

Follows: [ADR-0010](ADR-0010-poc-proven-production-seed.md)

Decided: **the PoC codebase (`app/`) stays the trunk**; we adopt vivid-classic's platform machinery by
**selective lift** (not a whole-trunk swap), and **build a right-sized graph model fresh**.

## Context

[ADR-0010](ADR-0010-poc-proven-production-seed.md) declared the PoC proven and promoted it to the seed.
The target (ratified): an **extensible, cross-platform-capable platform** — which ≈ vivid-classic's
architecture (operator/ABI boundary, graph model, codegen, hot-reload, packages, cross-cadence bridge,
test partitioning, production gate, docs culture). Classic has all of it; the PoC has little of it.

The initial recommendation (an earlier draft of this ADR) was **Option B — make classic the trunk and
port the PoC's product layer onto it.** That was reconsidered: B would inherit classic's ~4,500-file
codebase (including the large parts irrelevant to this product) and would **discard the PoC's hard-won
low-level work** — the `Renderer2D` drawing library, `ui_style`, the GPU/audio stacks, the thread-safety
discipline — because classic has its own equivalents. That maximizes both inherited cruft *and* lost work.

So we ran an **entanglement audit** (three read-only probes of vivid-classic, recorded in
[`../roadmap/poc-to-product.md`](../roadmap/poc-to-product.md) §1d) to answer the deciding question: *can
classic's valuable subsystems be lifted cleanly, or are they welded to its runtime?* The answer is that
classic's strict dependency direction makes the important pieces **lift cleanly** — which collapses the
A/B/C choice.

## Decision

1. **Target:** an extensible, cross-platform-capable platform (ratified).

2. **Trunk: our codebase (`app/`) stays the trunk.** We do **not** swap to classic's runtime (Option B
   rejected — see Alternatives). This keeps the PoC's validated low-level work — `Renderer2D`/`ui_style`,
   the GPU/audio stacks, the thread-safety discipline, the mapping bridge, the two-surface product — and
   avoids inheriting classic's irrelevant breadth as cruft.

3. **Strategy: selective lift, evidence-based.** Adopt classic's hardest-won, cleanly-separable platform
   machinery by lifting its actual code; build the rest fresh; adopt its engineering practices as
   patterns. The per-subsystem disposition (from the entanglement audit) is the binding part of this
   decision:

   | Subsystem (vivid-classic) | Disposition | Why / cost |
   |---|---|---|
   | `operator_api/` — the operator ABI + **type-erased draw table** | **LIFT-CLEAN** | 37 headers, `operator_api/**`+stdlib+WebGPU only, **zero** `runtime/` coupling; the `void* opaque` draw table means **our `Renderer2D` stays the host impl** — the ABI does not drag classic's renderer in. ~<1hr. |
   | Operator **loader + hot-reload** (loader, `HotReloader`, `FileWatcher`) | **LIFT-CLEAN** | pure dlopen/ABI wrapper + build queue + file-event pump; graph compiler is *late-bound* (only at a recompile decision). ~15 files / 3k LoC; drag = operator_api + stdlib. |
   | **Package system — core** (manager, compiler, manifest, scan/install) | **LIFT-CLEAN** | depends only on the operator registry; clang++/cmake invocation + JSON manifest. ~25 files / 8k LoC. |
   | `tools/operator_codegen` (tree-sitter) | **LIFT-ADAPT** | self-contained parser; generated code references `operator_api` types only. Retarget the descriptor-emit template to our descriptor shape. ~4–6hr. |
   | `AudioFrameBridge` (cross-cadence snapshot) | **LIFT-ADAPT** | lock-free double-buffer; reads `CompiledGraph` struct fields only. Refactor `build(CompiledGraph&)` → a lean layout struct. ~1.2k LoC. |
   | Test tiers / production-gate / sanitizer flags / CI workflows / `test_helpers` | **ADOPT-PATTERN** | re-author for our targets; `-DVIVID_SANITIZE[_THREAD]` flags copy verbatim. |
   | **Graph model / compiler** (the 7-pass lane/multiplicity engine) | **BUILD-FRESH, right-sized** | the deliberate no-cruft call: it's more than this product needs, and **nothing above forces it in**. Our `VisualGraph` executor is the seed. |
   | Package **lockfile** (Phase 6a strict-mode) | **BUILD-FRESH / defer** | the one piece wedged into compiled-graph state; rebuild against our graph model when needed. |
   | `Renderer2D`, `ui_style`/theme, `vivid_runtime_testlib` | **KEEP OURS / DON'T LIFT** | classic's are full WGPU-coupled *replacements*; adopting them would discard our drawing library and pull the whole runtime. |

## Alternatives Considered

- **Option A — Grow the PoC, reimplement classic's designs.** Rejected: needlessly *reimplements* code
  (operator ABI, loader, packages) that the audit shows lifts cleanly — slower, and re-derives subtle
  lessons we can just copy.
- **Option B — Make classic the trunk; port the product layer onto it.** *Rejected.* Inherits classic's
  full breadth as cruft and discards the PoC's low-level work (renderer, style, GPU/audio, thread-safety)
  in favor of classic's equivalents. Optimizes against both of the user's stated goals.
- **Option C — Selective lift onto our trunk (chosen, refined by the audit).** Keep `app/` as trunk; lift
  the cleanly-separable platform pieces; build the graph model fresh; adopt practices as patterns. Gains
  classic's proven machinery *and* keeps our work *and* avoids cruft.

## Consequences

- **Positive:** we keep every validated PoC asset and inherit classic's proven ABI/loader/packages with
  minimal drag; no ~4,500-file cruft; the graph model is built to this product's actual needs.
- **Cost / risk:** integration **seams** where lifted code meets ours — chiefly the `operator_api`
  descriptor model ↔ our (new, right-sized) graph model, the `operator_codegen` template retarget, and
  the `AudioFrameBridge` signature refactor. These are bounded and identified, not open-ended.
- **Sequencing implication:** because the trunk is ours, P0 hygiene (decompose `main.cpp`, tests/CI/
  sanitizers, control-server validation, docs) is **real, non-throwaway work on the trunk** and should
  start first; the operator-ABI lift (P1) is the first platform step and front-loads the
  graph-model/ABI-seam spike to de-risk it.
- **Follow-up:** the roadmap in [`../roadmap/poc-to-product.md`](../roadmap/poc-to-product.md) executes
  P0 → P4; P0 starts first as real trunk work.

## Status note

**Accepted (2026-06-29): keep our trunk + selective lift.** This reverses the earlier lean toward Option B
(swap to classic's runtime), on the evidence of the §1d entanglement audit. The per-subsystem disposition
table above is binding. Next: P0 hygiene on the trunk.
