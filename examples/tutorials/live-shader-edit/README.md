# Live Shader Edit

ADR-0040 tutorial **tier 2** — the first-class beginner walkthrough for Vivid's core promise:
*author creative code, change it live, inspect and recover it, all over MCP.*

Tier 1 (`mcp-native-first-project/`) builds a first audiovisual project. This tier zooms in on the
creative-coding loop itself — editing a project-local shader while Vivid is running — and it is
**self-contained**: the builder scaffolds its own tiny shader-only project, so you need **no synth**
(no Surge XT) to complete it.

## What You Learn

1. **Discover** a shader operator and its backing file over MCP — without guessing.
2. **Edit** the `.wgsl` and reload it live; the node keeps its identity and params.
3. **Break** the shader on purpose and **recover** using Vivid's own diagnostics.
4. **Verify** the output is real and **save** a reusable project.

## Before You Start

Launch Vivid with the control server listening on `127.0.0.1:9876` (set `VIVID_PORT` to change it).
For a disposable run:

```sh
VIVID_DISCARD_RECOVERY=1 ./build/Vivid.app/Contents/MacOS/Vivid
```

## Run It

```sh
uv run examples/tutorials/live-shader-edit/build.py
```

The builder performs every step below and writes `project/live-edit-proof.json` +
`project/FRICTION-LOG.md`. Read on to do the same steps by hand over MCP.

## Step 1 — Discover: node → operator → file

Ask Vivid what visual nodes exist. The scaffolded project has a `PulseField` node:

```json
{"method": "inspect_signal_flow"}   // → visuals.ops[] includes {"op": "PulseField", ...}
```

Now find out what `PulseField` *is* and where its code lives. The operator catalog is
self-describing for shader-backed ops:

```json
{"method": "find_operators", "query": "PulseField", "domain": "visual"}
```

```json
{
  "name": "PulseField",
  "format": "shader_file",
  "source": { "path": ".../project/shaders/pulse_field.wgsl", "tier": "project" }
}
```

`list_shaders` is the authoritative `name → path → error` view (it also shows a shader that failed to
register, so it is the place to look when an operator is *missing*):

```json
{"method": "list_shaders"}
```

## Step 2 — Edit the shader live

Open `project/shaders/pulse_field.wgsl` and change something visible — e.g. brighten it:

```wgsl
// was: color = color * vignette * (0.65 + u.glow * 1.2);
color = color * vignette * (1.05 + u.glow * 1.9);
```

Then tell Vivid to reload project files:

```json
{"method": "reload_project_files"}
// → "reloaded project files: 1 shader op(s), 1 shader node(s) rebuilt, ..."
```

`reload_project_files` re-registers the project shader tier **and rebuilds the live nodes** using it,
so your edit reaches the running `PulseField` node immediately while it keeps its node id, its wired
inputs, and its parameter values (matched by name). Confirm the picture changed:

```json
{"method": "capture_frame", "path": ".../after.png"}   // → {captured, is_blank:false, brightness}
```

## Step 3 — Break it, then recover (a real failure mode)

Creative coding means you *will* write a broken shader. Introduce a WGSL error on purpose — reference
an identifier that does not exist:

```wgsl
return vec4f(color * this_symbol_does_not_exist, 1.0);
```

Reload, render once, then ask Vivid what is wrong:

```json
{"method": "reload_project_files"}
{"method": "capture_frame"}          // the WGSL body compiles on first draw
{"method": "validate_project"}
```

`validate_project` names the failure — it does not fail silently:

```json
{
  "degraded": true,
  "issues": [{
    "level": "error",
    "issue": "visual operator failed to compile",
    "node_id": 1, "op": "PulseField",
    "detail": "no definition in scope for identifier: `this_symbol_does_not_exist`",
    "suggestion": "Fix the shader/operator source for 'PulseField', then call reload_project_files."
  }]
}
```

The same broken op appears in `inspect_signal_flow` under `visuals.broken_ops[]` with its error text.
The last good pipeline keeps rendering, so the app never goes black on a typo.

Now fix the file and reload — `validate_project` returns to clean (`degraded:false`, no visual issues)
and `run_quality_check name=all` passes.

## Step 4 — Save

```json
{"method": "save_project", "path": ".../project"}
```

You now have a reusable folder project whose creative material is project-local shader code you edited
live over MCP.

## Proof & Friction

- `project/live-edit-proof.json` — every step's response (discovery, before/after frames, the compile
  error and its recovery, final quality check).
- `project/FRICTION-LOG.md` — what worked, what needed MCP help, and which product gaps this tutorial
  drove to fixes.

## What This Tutorial Fixed In Vivid

Building this walkthrough exposed two real gaps, now fixed (ADR-0040):

- The operator **catalog didn't reveal that an op was shader-backed or where its file was** — a
  beginner had to know to call `list_shaders`. `find_operators` / `list_operator_catalog` now carry
  `format: "shader_file"` and `source: {path, tier}` for shader ops.
- `reload_project_files` re-registered the shader *type* but **did not rebuild the live nodes**, so a
  body edit (and a newly-broken body) never reached the running node over MCP — the live-edit promise
  silently failed for agents. It now rebuilds the live nodes of re-registered project shader types, so
  both the creative edit and its compile errors are deterministic over MCP.
