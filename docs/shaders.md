# Writing a Vivid shader

A shader **file** is an operator. Drop a `.wgsl` into your shader folder and it appears in the
Tab chooser as a node you can spawn, wire, map to audio and save — with the params *it*
declares. No C++, no dylib, no build step. Save the file and the change is on screen.

This is [ADR-0016](decisions/ADR-0016-shaders-are-content.md).

## The shortest possible shader

```wgsl
/*{
  "name": "Flash",
  "summary": "A colour that pulses with the beat.",
  "inputs": [],
  "params": [
    {"name": "hue",  "type": "float", "default": 0.5, "min": 0, "max": 1, "display": "knob"},
    {"name": "gain", "type": "float", "default": 0.5, "min": 0, "max": 1, "display": "knob"}
  ]
}*/
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let c = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + u.hue * TAU);
    return vec4f(c * u.gain, 1.0);
}
```

Save it as `Flash.wgsl` in `~/Library/Application Support/Vivid/shaders/`, press Tab in the
visuals graph, type "flash". Wire a kick to `gain` and it flashes on the beat.

## The header declares, the host generates

The JSON block in the leading `/*{ … }*/` comment is the whole interface. From it Vivid
**generates** the uniform struct, the bindings, the sampler and the vertex stage, and prepends
them to your body. You never declare `struct U`, `@group(0) @binding(…)` or `vs_main` — and
you never pack a uniform buffer by hand, which is what used to make a mismatched field silently
render garbage.

What you get in the body:

| Name | What it is |
|---|---|
| `u.<param>` | every param you declared, by name |
| `u.res` | output size in pixels (`vec2f`) |
| `u.time` | seconds (`f32`) |
| `<input>` | one `texture_2d<f32>` per declared input, named as you named it |
| `samp` | the sampler (present when there is at least one input) |
| `inp.uv` | 0..1 UV of the fragment, top-left origin |
| `PI`, `TAU`, `E`, `PHI`, `SQRT2` | constants |

## Params

```json
{"name": "amount", "type": "float", "default": 0.5, "min": 0, "max": 1,
 "display": "knob", "description": "shown on hover", "group": "Shape",
 "semantic_intent": "warp amount", "semantic_tag": "phase_01"}
```

| `"type"` | In the shader | In the inspector |
|---|---|---|
| `float` *(default)* | `f32` | slider, or a knob with `"display": "knob"` |
| `int` | `i32` | stepper |
| `bool` | `i32` (0/1) | toggle |
| `color` | `vec3f` | a colour swatch (three params: `name_r`, `name_g`, `name_b`) |
| `point2` | `vec2f` | an XY pad (two params: `name_x`, `name_y`) |
| any + `"choices": [...]` | `i32` | a picker — the index IS the value |

`color` and `point2` are the only sugar: they occupy several *host* params (which is how the
inspector's compound widgets find them) but a single vector field in the shader. Everything a
param can be, a wire can drive — including from audio.

## Inputs

`"inputs": []` is a source. `["input"]` is a filter. `["A", "B"]` is a compositor. That is the
whole vocabulary: **a shader file is a fullscreen fragment pass over 0 to 2 input textures.**

```wgsl
/*{ "name": "Mix", "inputs": ["A", "B"],
    "params": [{"name": "amount", "type": "float", "default": 0.5, "min": 0, "max": 1}] }*/
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    return mix(textureSample(A, samp, inp.uv), textureSample(B, samp, inp.uv), u.amount);
}
```

Anything needing custom vertex data, CPU-side asset decode, cross-frame history or a non-texture
input is a compiled operator instead, not a shader file — that is the boundary rule, and it is
why `Feedback` (which owns a history texture) and `Mesh` (which owns vertex buffers) are still
C++ while `Plasma` and `Blur` are not.

## Where shaders live

Scanned in precedence order, **first one to claim a name wins**:

1. **user** — `~/Library/Application Support/Vivid/shaders/` (override with `$VIVID_SHADERS_DIR`)
2. **project** — `<project>/shaders/`
3. **bundled** — what ships with Vivid

So a user shader **shadows** a shipped one of the same name. That is an authoring affordance:
`fork_shader` copies a shipped shader into your folder under a new name and registers it live,
which is how you customize one without touching the original.

`list_shaders` shows every file Vivid found, registered or not — a file that fails to parse
appears **with its error** rather than vanishing.

## The edit loop

Save the file. That is it.

- **Body edit** → every live node recompiles in place. Nothing else moves.
- **Header edit** (a param added, removed, retyped) → the type re-registers and its nodes rebuild,
  **keeping their param values by name**.
- **A new file** in the folder → it registers itself; press Tab and it is there.
- **An edit that will not compile** → the error is reported and **the last good version keeps
  running**. Saving a syntax error mid-performance never blacks out a live output.
- **A node that has never compiled** → renders black if it is a source, or passes its input
  through if it is a filter. Never garbage. The node card shows the error.

One thing you cannot do while running: **rename** a shader. The name is the operator type, and
every node, wire and mapping points at it — so a rename is refused with a message rather than
silently orphaning your graph. Restart to pick it up.

## Reserved

`"passes"` and `"buffers"` are reserved: they parse and are **rejected with a message**, so that
when multi-pass shaders land, a v1 file gains a field rather than the format gaining a version.
Today's `Blur` is a single-pass box blur for exactly this reason — a real separable gaussian is
two passes, and that is the next epic, not a v1 feature.

## GLSL

`.glsl` works too (the prelude is generated in GLSL: `u`, `v_uv`, `o_color`). WGSL is the better
supported path; GLSL is there because the demo shaders were written in it.
