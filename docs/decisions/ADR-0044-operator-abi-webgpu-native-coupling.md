# ADR-0044: The Operator ABI Is Coupled to the webgpu-native Handle Layout

Status: proposed

Date: 2026-07-31

> **Origin.** Raised by the first-release Code Audit, Phase 1 (Architecture & Ownership
> Boundaries), finding **P3-03**. See
> `docs/audits/07-31-2026/code/phase-01-architecture-and-ownership-boundaries.md`.
> This is a stub capturing the decision to make; it is not yet accepted.

## Context

The operator ABI (`operator_api/operator.h` + `types.h`,
`VIVID_OPERATOR_ABI_VERSION 14u`, floor `MIN_LOADABLE 11u`) is the one genuinely public,
versioned surface third-party operator authors compile against. The Phase-1 audit
confirmed it is otherwise clean: it exposes only plain C ABI structs and function-pointer
tables, and **no first-party Vivid C++ type crosses the boundary**.

The single external coupling: `VividGpuContext` (`operator_api/gpu_operator.h:35-72`)
embeds webgpu handles **by value** — `WGPUDevice`, `WGPUQueue`, `WGPUCommandEncoder`,
`WGPUTexture`, `WGPUTextureView`, `WGPUTextureFormat`, `WGPUBuffer` — from the public
`<webgpu/webgpu.h>` C API (`gpu_operator.h:5`). These handles are load-bearing: a GPU
operator cannot render without them.

Because they are structurally part of the ABI struct, a change to the webgpu-native
handle layout — a WGPU header ABI break, or Vivid migrating off webgpu-native to another
WebGPU implementation (e.g. Dawn) — would be a **non-additive** operator-ABI break. Per
the loader contract (`gpu/operator_loader.cpp:145`, additive-only, floor bump only for
non-additive changes), that would force a `MIN_LOADABLE` bump and **orphan every operator
dylib users have already installed**. The first release quietly commits to this coupling.

## Decision (to be made)

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
