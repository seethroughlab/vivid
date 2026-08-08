# content-3d

3D single-look **content operators** that ship as an installable example package rather than in the
lean core (ADR-0054). Unlike the core-visuals content ops, these depend on the vivid-3d scene-graph
header shim (`operator_api/gpu_3d.h`, `thumbnail_3d.h`, …), which is **not** part of the shared
`operator_api/`. So the package **vendors** those headers (see `vendor/`) and declares them via
`dependencies.vendor` (ADR-0054 Stage 1) — the package compiler then adds `-I vendor` and the op
builds on install with no app rebuild and no external libraries.

| Operator | What it does |
|---|---|
| `InstanceNoise` | Lays out an `InstanceArray3D` on a value-noise field (single-look 3D layout). |

## Vendored headers — keep in sync

`vendor/operator_api/*.h` and `vendor/linmath.h` are **copies** of the vivid-3d package-local 3D
headers (`app/operators/packages/vivid-3d/operator_api/` + `linmath.h`). If that 3D header set changes
(e.g. an ABI bump), re-copy it here and rebuild the package (`install_operator_package`). This is the
classic package model: packages carry their vendored deps and are rebuilt when the core moves. The
shared `operator_api/` headers (`operator.h`, `gpu_operator.h`, `gpu_common.h`, `type_id.h`) are NOT
vendored — the compiler always puts the real `operator_api/` on the include path.

## Install

```
install_operator_package  <abs path to this directory>     # MCP
```

## Why it's not in core

ADR-0054: the core catalog is a lean spine. `InstanceNoise` is an unused single-look layout variant —
a good example of recombination, not spine. Its vivid-3d siblings `InstanceGrid` / `Deformer` /
`Particles3D` are *also* content candidates but currently back shipped demos (lattice / crystal /
blob / storm), so they stay in core until those demos are migrated.
