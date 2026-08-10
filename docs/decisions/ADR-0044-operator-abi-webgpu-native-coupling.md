# ADR-0044: The Operator ABI Is Coupled to the webgpu-native Handle Layout

Status: accepted (2026-08-09 — policy recorded; no code change, per the audit's "smallest
acceptable fix". See As Accepted.)

Date: 2026-07-31

> **Origin.** Raised by the first-release Code Audit, Phase 1 (Architecture & Ownership
> Boundaries), finding **P3-03**. See
> `docs/audits/07-31-2026/code/phase-01-architecture-and-ownership-boundaries.md`.

## Context

The operator ABI (`app/src/operator_api/operator.h` + `types.h`,
`VIVID_OPERATOR_ABI_VERSION 17u`, floor `MIN_LOADABLE 11u`) is the one genuinely public,
versioned surface third-party operator authors compile against. The Phase-1 audit
confirmed it is otherwise clean: it exposes only plain C ABI structs and function-pointer
tables, and **no first-party Vivid C++ type crosses the boundary**.

The single external coupling: `VividGpuContext`
(`app/src/operator_api/gpu_operator.h:35-42,72`) embeds webgpu handles **by value** —
`WGPUDevice`, `WGPUQueue`, `WGPUCommandEncoder`, `WGPUTexture`, `WGPUTextureView`,
`WGPUTextureFormat`, `WGPUBuffer` — from the public `<webgpu/webgpu.h>` C API
(`gpu_operator.h:5`). These handles are load-bearing: a GPU operator cannot render without
them.

Because they are structurally part of the ABI struct, a change to the webgpu-native
handle layout — a WGPU header ABI break, or Vivid migrating off webgpu-native to another
WebGPU implementation (e.g. Dawn) — would be a **non-additive** operator-ABI break. Per
the loader contract (`gpu/operator_loader.cpp:145`, additive-only, floor bump only for
non-additive changes), that would force a `MIN_LOADABLE` bump and **orphan every operator
dylib users have already installed**. The first release quietly commits to this coupling.

## Decision

Acknowledge the coupling as **intentional** for first release and record the policy:

1. **Accept the coupling.** Passing native WGPU handles is the pragmatic, zero-copy way to
   let operators issue GPU work; abstracting them behind a Vivid-owned indirection layer is
   not justified pre-release.
2. **Name the breakage class.** A WGPU handle-layout change is a `MIN_LOADABLE`-bump event,
   not an additive `VERSION` bump; it orphans installed operators and must be treated as a
   migration with user communication, per `docs/operator-api/abi-changelog.md`.
3. **(Optional, later)** If a WebGPU-backend migration is ever seriously considered,
   evaluate a thin Vivid-owned GPU-handle indirection *before* it, so the ABI can absorb
   the backend change additively.

## Consequences

- **Positive:** the ABI stays simple and zero-copy; operator authors use the standard
  WebGPU C API they already know.
- **Tradeoff:** Vivid's operator ABI stability is bounded by webgpu-native's ABI stability;
  a backend migration is a breaking, orphaning event by construction.
- **Non-blocking:** no code change for release — this ADR is the deliverable.

## Alternatives Considered

- **Wrap WGPU handles behind Vivid-owned opaque types now.** Rejected pre-release: adds an
  indirection layer and per-call cost for a migration that is not planned, and would itself
  be an ABI-breaking change to introduce.
- **Say nothing.** Rejected: the coupling is a real first-release constraint on a public
  surface and should be explicit precedent, not discovered later.

## As Accepted (2026-08-09)

Decision option 1 (accept the coupling) is adopted. Per the audit's "smallest acceptable
fix" this is a documentation deliverable with **no code change** to the coupling itself.
Landed:

1. **The breakage-class policy is recorded** in `docs/operator-api/abi-changelog.md` — a
   WGPU handle-layout change (a `<webgpu/webgpu.h>` ABI break, or a migration off
   webgpu-native) is a `MIN_LOADABLE`-bump event that orphans installed operator dylibs,
   not an additive `VERSION` bump. It is a user-communicated migration, not a silent floor
   move.
2. **The coupling is annotated at its site** — `gpu_operator.h` carries an inline pointer
   to this ADR at the WGPU handle block, so a contributor editing those handles meets the
   policy where the coupling lives rather than only in a doc they must know to open
   (guard-not-manifest).

Decision option 3 (a Vivid-owned GPU-handle indirection layer) remains **deferred**: it is
only justified *ahead of* a seriously-considered WebGPU-backend migration, and none is
planned. The ABI is at `v17` (floor `v11`); the coupling is unchanged since first release.
