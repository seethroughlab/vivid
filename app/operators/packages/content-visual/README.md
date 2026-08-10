# content-visual

Single-look visual **content operators** that ship as an installable example package rather than in
the lean core (ADR-0054). Each is a nice-to-have look a user can also rebuild from core primitives, so
it earns its place as an *example*, not a bundled default.

| Operator | What it does |
|---|---|
| `TimeMachine` | Frame-history echo/trails effect (temporal feedback look). |
| `CosinePalette` | IQ-style cosine color palette — cycles hues from a phase input. |
| `Emitter` | Recipe (ADR-0046): a `VividSignal` → aged particles, laid out + coloured + rendered in one node. |
| `Instancer` | Recipe (ADR-0046): a `VividSignal` → instanced shapes, layout + material + render in one node. |
| `Solids` | Recipe (ADR-0046): a `VividSignal` → solid 3D forms, layout + material + render in one node. |

These are pure `operator_api` + WebGPU ops (no external libraries), compiled on install by the package
compiler — no app rebuild. `Emitter`/`Instancer`/`Solids` are ADR-0046 *recipes* (they bundle a whole
workflow into one node); the composable path is `Signal → lane primitives → InstancesFromLanes →
Instancer3D`.

## Install

```
install_operator_package  <abs path to this directory>     # MCP
```

or point Vivid at this folder from a source checkout. Once installed they register through the same
`OpRegistry` as the built-ins and appear in `list_operators` / the add-node chooser.

## Why they're not in core

ADR-0054: the core catalog is a lean spine (render / reactivity / audio spine + composable
primitives). Single-look content ops are examples of recombination, not spine — so they live here.
