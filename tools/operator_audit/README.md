# Operator audit (ADR-0042)

An advisory, per-operator health check. Drives the **running app** over the control server and measures
every registered operator against the four-dimension Definition of Done: **thumbnail**, **renders**,
**params affect output**, **perf**.

```sh
# 1. launch the app (a live GPU is required — this is not a headless CI check)
VIVID_NO_RECOVER=1 VIVID_PORT=9876 open app/build/vivid.app

# 2. run the audit (Vivid is auto-foregrounded per op — macOS pauses an occluded window's render loop)
uv run tools/operator_audit/audit.py            # whole catalog -> reports/audit-<ts>.json + console table
uv run tools/operator_audit/audit.py Shape3D    # a single operator
```

## Verdicts

- **PASS** — renders non-blank, has a thumbnail, every swept param changed the output.
- **WARN** — renders + thumbnail fine, but ≥1 param showed *no visible change* in the single-frame test
  graph. Often legitimate (rotating a symmetric object, a material param on a small object); review the
  `no_effect` list per op in the JSON.
- **NEED** (needs-input) — the op depends on a live input a synthetic scaffold can't supply (a played note
  stream, a loaded mesh/asset/video, authored text). Not a defect.
- **FAIL** — a genuine defect: renders blank when it shouldn't, or a blank node thumbnail.
- **aud** — audio op; not GPU-audited. `expected-manual` = a generator/modulator that should draw a
  thumbnail from its param snapshot; `exempt-effect` = a pure DSP effect (name-only cell is acceptable).

## How it works

`scaffolds.py` builds a minimal graph per op from its ports (`list_operators`): texture ops → Output;
scene ops → `SceneMerge(op, Shape3D, Light3D) → Render3D`; instances → `Instancer3D → Render3D`; lanes →
`InstancesFromLanes → Instancer3D → Render3D`; signal → `InstancesFromSignal → …`. `audit.py` captures via
`capture_frame`/`analyze_frame` (a combined hash + brightness/contrast/colour-spread signature, since the
8×8 hash alone is dominated by the background), sweeps each param min→max against an animation noise floor,
and samples `get_perf` for a frame-time delta.

Advisory only today; ADR-0042 Phase 4 promotes the render + param + thumbnail checks into the CI gate.
