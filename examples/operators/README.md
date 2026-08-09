# Operator examples — operators you own

These aren't just demos of built-in nodes. **Each is an operator you can open, read, edit, and fork
into your own** — the whole point of Vivid. The built-ins exist to *teach* authoring, not to be the
value; the value is composing the core primitives and writing your own operators (which, in an
LLM-empowered tool, is a realistic default). See **ADR-0054**.

One minimal project per operator — "here's operator X in isolation" — as opposed to the polished demo
compositions in `examples/demos/projects/`. Open one via **File › Open Example › Operators** (or
`load_project`).

**When an example ships its operator's source** (the moved-out ops carry their `.cpp`/`.wgsl` right in
the folder), opening it toasts a hint, and you can **right-click the node → *Open source in editor*** to
read/edit it, or ***Fork & edit*** a built-in to start your own. Editing the source and reloading
recompiles it — that loop *is* how you author. Copy an example folder as the starting point for a new
operator.

## Regenerate (the graphs, not the source)

The example *graphs* are **generated** by `tools/operator_audit/gen_examples.py`, which reuses the
ADR-0042 audit scaffold (`scaffolds.build_scaffold`) and saves a minimal renderable graph instead of
capturing a preview. The carried operator *source* is the real thing — edit it freely.

```bash
# launch a lean app instance by DIRECT binary path with a control port:
VIVID_PORT=9877 app/build/vivid.app/Contents/MacOS/vivid &
uv run tools/operator_audit/gen_examples.py Render3D InstanceNoise   # or any op names
```

## Regenerate

```bash
# launch a lean app instance by DIRECT binary path with a control port:
VIVID_PORT=9877 app/build/vivid.app/Contents/MacOS/vivid &
uv run tools/operator_audit/gen_examples.py Render3D InstanceNoise   # or any op names
```

## Operators not in the lean core (ADR-0054)

An op that has been moved out of the default install (e.g. `InstanceNoise` → the `content-3d` package)
can't be assumed present. So its example folder **carries the op's package** (`vivid-package.json` +
source + any `vendor/` headers); opening the project compiles + registers the op via the project-local
operator path (`app/src/app/project_io.cpp`). The example therefore both *demonstrates* the op and
*carries* it — no dependency on the default catalog.

The compiled `.dylib` that appears in such a folder after a load is a regenerated build artifact
(`.gitignore`d), not source.
