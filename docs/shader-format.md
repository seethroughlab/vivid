# The Vivid shader format (v1)

A shader file **is** an operator. Drop a `.wgsl` (or `.glsl`) into a shaders directory and it appears
in the Tab chooser with its own name, its own params, and its own inputs — no C++, no compiler, no
dylib. See [ADR-0016](decisions/ADR-0016-shaders-are-content.md) for why.

## Where shaders live

Scanned in precedence order — **user > project > bundled** — so you can shadow a shipped shader by
putting one of the same name in your own directory:

| Tier | Location |
|---|---|
| user | `~/Library/Application Support/Vivid/shaders/` |
| project | `<project>/shaders/` |
| bundled | inside the app bundle (`Resources/shaders`; `VIVID_SHADERS_DIR` overrides, for development) |

## The file

A JSON object in a **leading block comment**, then the fragment body:

```wgsl
/*{
  "vivid": 1,
  "name": "Plasma",
  "summary": "Sine-field plasma generator.",
  "keywords": ["generator", "plasma"],
  "inputs": [],
  "params": [
    { "name": "warp",  "type": "float", "default": 0.5, "min": 0, "max": 1, "label": "Warp" },
    { "name": "mode",  "type": "int",   "default": 0, "choices": ["add", "mul", "screen"] },
    { "name": "tint",  "type": "color", "default": [1.0, 0.4, 0.8, 1.0] }
  ]
}*/
@fragment
fn fs_main(in: FullscreenOutput) -> @location(0) vec4f {
    let p = in.uv * 2.0 - 1.0;
    let v = sin(p.x * 8.0 + u.time) + sin(p.y * 8.0 * u.warp);
    return vec4f(u.tint.rgb * (v * 0.5 + 0.5), 1.0);
}
```

The header is the **single source of truth**. You declare what you want; Vivid generates the uniform
struct, the bindings, the sampler and the vertex stage to match, and prepends them to your body. You
never hand-pack a uniform buffer, and your declarations can never drift out of sync with your struct.

### `inputs` decides what kind of operator you are

| `inputs` | Kind | You get |
|---|---|---|
| `[]` | generator | no input textures |
| `["input"]` | filter | `input: texture_2d<f32>` |
| `["a", "b"]` | compositor | `a`, `b` |

Two input textures is the v1 maximum.

### `params`

| Field | Meaning |
|---|---|
| `name` | **required** — the uniform member name (`u.<name>`) and the graph param name |
| `type` | `float` (default), `int`, `bool`, `color`, `point2` |
| `default`, `min`, `max` | value and range; `color` takes `[r,g,b,a]`, `point2` takes `[x,y]` |
| `label`, `description` | shown in the inspector |
| `choices` | for `int` — makes it an enum with named options |
| `display` | widget hint: `knob`, `xy_pad`, `color`, `hidden` |

Param names are the **compatibility contract**: a project stores param values by name, and audio→visual
mappings target them by name (`"node:0.warp"`). Rename a param and existing projects lose that value.

### What the prelude gives you

Always available in the body, without declaring anything:

- `u.time` — seconds, and `u.res` — output resolution in pixels (`vec2f`)
- `u.<name>` — every param you declared, in the order you declared it
- `FullscreenOutput` with `in.uv` (0..1), and a `samp` sampler
- your input textures, named as in `inputs`

## Errors and hot-reload

- **Editing the body hot-reloads in place.** Editing the *header* (adding or renaming a param)
  re-registers the operator type and rebuilds the nodes using it, preserving values by name.
- **A shader that fails to compile keeps its last-good pipeline.** Saving a syntax error mid-set will
  not black out a live output; the node shows the error, and the previous version keeps rendering.
- **A malformed shader still appears in the catalog, with its error.** It is never silently dropped —
  a shader you wrote that doesn't show up at all would be worse than one that shows up broken.

## GLSL

`.glsl` files use the same header and the same param model; only the generated prelude differs. A
`.glsl` file with **no** header falls back to the legacy `CustomShader` contract (the fixed
`u_warp`/`u_hue`/`u_density`/`u_glow` uniform block) so existing projects keep working.

## Reserved for later

`"passes"` and `"buffers"` are **reserved in v1** and rejected with a clear message. Multi-pass
shaders (a proper separable gaussian, ping-pong buffers) are the next epic; reserving the keys now
means the shaders you write today stay valid when it lands.
