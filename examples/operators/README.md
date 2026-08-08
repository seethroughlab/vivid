# Operator examples

Minimal, one-per-operator example projects — "here's operator X in isolation" — as opposed to the
polished demo compositions in `examples/demos/projects/`. Each is a folder project you can open with
`load_project` (and, once wired, **File › Open Example › Operators**).

These are **generated**, not hand-authored, by `tools/operator_audit/gen_examples.py`, which reuses the
ADR-0042 audit scaffold (`scaffolds.build_scaffold` — it already builds a minimal renderable graph for
any op from its ports) and saves it as a folder project instead of capturing a preview.

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
