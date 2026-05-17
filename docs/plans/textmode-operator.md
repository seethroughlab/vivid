# Plan: Textmode Operator

## Context

[code.textmode.art](https://code.textmode.art) is a JavaScript library (`textmode.js`) for creating real-time ASCII/textmode art in browsers. Programs define a `tm.draw(() => { ... })` callback that iterates over a character grid and calls methods like `tm.translate(x,y)`, `tm.char('*')`, `tm.charColor(r,g,b)`, `tm.point()` to paint cells. The output is a colored character grid rendered via WebGL2.

The goal is a Vivid GPU operator that:
1. Accepts a JS program as a `TextValue` param
2. Evaluates it each frame via embedded QuickJS, building a CPU-side cell grid
3. Renders the grid to a `gpu_texture` output using a WGSL shader + monospace font atlas

---

## Architecture

### Cell grid model
Each cell: `struct Cell { uint8_t ch; uint8_t r, g, b; uint8_t bg_r, bg_g, bg_b; uint8_t _pad; }` (8 bytes, 4-byte aligned). Storage buffer on GPU: `array<Cell, cols*rows>`.

### Font atlas (fixed-grid layout)
- Load JetBrainsMono-Regular.ttf via stb_truetype (same path logic as `text_2d.cpp`)
- Rasterize ASCII 32–126 (95 chars) into 1024×1024 R8Unorm atlas
- **Fixed-grid layout**: 16 columns × 8 rows = 128 slots, each 64×128px
- Fixed grid = simple shader math: `atlas_uv = (vec2f(ch%16, ch/16) + subcell_uv) / vec2f(16,8)`
- Upload once to GPU; never changes

### WGSL shader (fullscreen pass)
Bindings:
- `@binding(0)` uniform buffer: `{ resolution: vec2f, cols: u32, rows: u32 }`
- `@binding(1)` storage buffer: cells packed as `array<u32>` pairs `{ fg: u32, bg: u32 }` per cell
- `@binding(2)` font atlas texture (R8Unorm)
- `@binding(3)` sampler

Per-fragment logic:
1. `col = u32(uv.x * f32(cols))`, `row = u32(uv.y * f32(rows))`
2. Clamp to grid; look up `cells[row * cols + col]`
3. Compute sub-cell UV (fractional part within the cell)
4. Sample atlas at glyph UV for that character
5. `output = mix(bg_color, fg_color, atlas_alpha)`

### QuickJS runtime
- One `JSRuntime` + `JSContext` per operator instance (created on first `process_gpu`)
- Expose global `tm` object with:
  ```javascript
  tm.frameCount         // int
  tm.time               // float (seconds)
  tm.grid.cols          // int
  tm.grid.rows          // int
  tm.background(r,g,b)  // fill all cells with space + set bg color
  tm.push() / tm.pop()  // save/restore cursor+color state
  tm.translate(x, y)    // set cursor (integer grid coords)
  tm.char(c)            // set current char (single-char string or char code)
  tm.charColor(r, g, b) // set fg color (0–1 floats)
  tm.bgColor(r, g, b)   // set cell bg color (0–1 floats)
  tm.point()            // write current char+colors at cursor
  ```
- Program detection: if the script defines a function named `draw`, call `draw()` each frame; otherwise re-evaluate the full script each frame.
- On program change: recreate `JSContext` to clear stale definitions, re-eval to discover `draw`.
- Errors: set `ctx->operator_errored = 1` + `operator_error_msg` with the JS exception text.

### Params
| Param | Type | Default | Range |
|-------|------|---------|-------|
| `program` | TextValue | (wave example below) | — |
| `cols` | int | 80 | 4–240 |
| `rows` | int | 40 | 4–120 |
| `bg_r/g/b` | float | 0.05/0.05/0.08 | 0–1 |

Output: single `gpu_texture` port.

---

## Implementation Steps

### 1. Vendor QuickJS
Clone `https://github.com/bellard/quickjs.git` (shallow) and copy into `deps/quickjs/`:
- `quickjs.h`, `quickjs.c`, `quickjs-atom.h`, `quickjs-opcode.h`
- `libregexp.h`, `libregexp.c`, `libregexp-opcode.h`
- `libunicode.h`, `libunicode.c`, `libunicode-table.h`
- `dtoa.h`, `dtoa.c` (current QuickJS uses dtoa instead of the older libbf)
- `cutils.h`, `cutils.c`, `list.h`

Do **not** copy: `quickjs-libc.h/c` (OS bindings), CLI tools (`qjs.c`, `qjsc.c`), test runner.

Add to `cmake/dependencies.cmake`:
```cmake
add_library(quickjs STATIC
    deps/quickjs/quickjs.c
    deps/quickjs/libregexp.c
    deps/quickjs/libunicode.c
    deps/quickjs/dtoa.c
    deps/quickjs/cutils.c)
target_include_directories(quickjs PUBLIC deps/quickjs)
target_compile_options(quickjs PRIVATE -w)   # suppress third-party warnings
set_target_properties(quickjs PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON)
```

### 2. Register the operator
Add to `cmake/operators.cmake` (GPU operators section, after `text_2d`):
```cmake
add_vivid_operator(textmode operators/gpu/textmode/textmode.cpp
                   CODEGEN EXTRA_LIBS webgpu stb_truetype quickjs)
```

### 3. Create `operators/gpu/textmode/textmode.cpp`

High-level structure:
```
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "quickjs.h"
#include "stb_truetype.h"

// Embedded WGSL shader (kTextmodeShader)
// GpuCell struct { uint32_t fg, bg; }  — packed cell data for GPU
// Cell struct { uint8_t ch,r,g,b,bg_r,bg_g,bg_b,_pad; }  — CPU-side cell
// TextmodeUniforms struct { float res[2]; uint32_t cols, rows; }

// Forward-declared JS C-function callbacks (access operator via JS_GetContextOpaque)

struct TextmodeOp : vivid::OperatorBase, vivid::GpuProcessable {
    // --- Params ---
    vivid::Param<vivid::TextValue> program
    vivid::Param<int> cols, rows
    vivid::Param<float> bg_r, bg_g, bg_b

    // --- GPU resources ---
    WGPURenderPipeline pipeline_
    WGPUBindGroupLayout bind_layout_
    WGPUPipelineLayout pipe_layout_
    WGPUBindGroup bind_group_
    WGPUShaderModule shader_
    WGPUBuffer uniform_buf_, cell_buf_
    uint32_t cell_buf_cap_
    WGPUTexture atlas_tex_
    WGPUTextureView atlas_view_
    WGPUSampler sampler_

    // --- JS runtime ---
    JSRuntime* js_rt_
    JSContext* js_ctx_
    bool js_has_draw_fn_, js_initialized_
    std::string last_program_, js_error_msg_

    // --- CPU cell grid ---
    std::vector<Cell> cells_
    std::vector<GpuCell> cells_gpu_
    int cell_cols_, cell_rows_

    // --- Draw cursor state (shared with JS callbacks) ---
    uint8_t cur_ch_, cur_r_, cur_g_, cur_b_, cur_bg_r_, cur_bg_g_, cur_bg_b_
    int cur_x_, cur_y_
    struct DrawState { ... }
    std::vector<DrawState> state_stack_

    // --- process_gpu flow ---
    // 1. ensure_atlas(ctx)      — stb_truetype into 1024×1024 fixed-grid atlas, upload once
    // 2. init_pipeline(ctx)     — compile shader, BGL (uniform+storage+tex+sampler), pipeline
    // 3. ensure_js()            — JSRuntime + JSContext + tm global with all API methods
    // 4. resize_cells(ctx,c,r)  — grow cell buffers + recreate bind group when dims change
    // 5. run_js_frame(t, frame) — reinit context if program changed; update frameCount/time;
    //                            call draw() or re-eval; report JS errors
    // 6. pack cells_ → cells_gpu_
    // 7. wgpuQueueWriteBuffer(cell_buf_, cells_gpu_)
    // 8. wgpuQueueWriteBuffer(uniform_buf_, uniforms)
    // 9. vivid::gpu::run_pass(...)

    ~TextmodeOp() — JS_FreeContext/Runtime + vivid::gpu::release(all resources)
};

// JS callback implementations (translate, char, charColor, bgColor, background, push, pop, point)
VIVID_DEFINE_OP(Textmode) { display_name="Textmode"; keywords={"ascii","text","code"}; }
```

### 4. Default program
```javascript
function draw() {
  tm.background(0, 0, 0);
  const cx = tm.grid.cols / 2;
  const cy = tm.grid.rows / 2;
  for (let y = 0; y < tm.grid.rows; y++) {
    for (let x = 0; x < tm.grid.cols; x++) {
      const dx = x - cx, dy = y - cy;
      const dist = Math.sqrt(dx*dx + dy*dy);
      const wave = Math.sin(dist * 0.4 - tm.time * 3.0);
      const t = (wave + 1) * 0.5;
      const chars = ' .:-=+*#%@';
      tm.translate(x, y);
      tm.char(chars[Math.floor(t * (chars.length - 1))]);
      tm.charColor(t, t * 0.5, 1.0 - t);
      tm.point();
    }
  }
}
```

---

## Key Implementation Details

### Atlas building (stb_truetype)
Same font-path resolution as `text_2d.cpp` (JetBrainsMono-Regular.ttf via `dladdr`). Use `stbtt_GetCodepointBitmap` per glyph, place each glyph centered horizontally in its 64×128px slot with baseline at ~80% slot height. Bounds-check every pixel write. Atlas is R8Unorm, uploaded via `wgpuQueueWriteTexture`.

### Bind group layout (4 entries)
```
binding 0: uniform buffer   (vertex+fragment visibility)
binding 1: read-only storage buffer (fragment)
binding 2: texture_2d<f32>  (fragment, R8Unorm, non-multisampled)
binding 3: sampler filtering (fragment)
```
Recreate the bind group (not the layout or pipeline) when `cell_buf_` is resized.

### JS context lifecycle
- `JS_SetContextOpaque(ctx, this)` so C callbacks can recover the operator pointer.
- `JS_SetMemoryLimit(rt, 64MB)` + `JS_SetMaxStackSize(rt, 4MB)`.
- On program change: `JS_FreeContext` + `JS_FreeRuntime` + re-`init_js()` → clean slate.
- After eval/call: check `JS_IsException`, extract message with `JS_GetException` + `JS_ToCString`, clear with `JS_FreeValue`.

### GpuCell packing
```cpp
struct GpuCell {
    uint32_t fg;  // ch | (r<<8) | (g<<16) | (b<<24)
    uint32_t bg;  // bg_r | (bg_g<<8) | (bg_b<<16)
};
```
WGSL unpacks with bit shifts and `f32(...) / 255.0`.

---

## Critical Files

| File | Action |
|------|--------|
| `deps/quickjs/` | Create — vendor QuickJS C sources |
| `cmake/dependencies.cmake` | Edit — add `quickjs` static lib target |
| `cmake/operators.cmake` | Edit — register `textmode` operator (after `text_2d` line) |
| `operators/gpu/textmode/textmode.cpp` | Create — the operator |
| `operators/gpu/text_2d/text_2d.cpp` | Reference — stb_truetype atlas + font path resolution |
| `operators/gpu/noise/noise.cpp` | Reference — fullscreen shader pipeline pattern |

---

## Verification

1. `vivid build textmode` — operator dylib compiles cleanly
2. Add `Textmode → video_out` in a graph; `capture_frame` — should show the default wave pattern as colored ASCII art
3. `set_string_param <node> program "<custom JS>"` — verify live hot-update
4. Submit a program with a syntax error — operator shows error state without crashing
5. Change `cols` or `rows` — verify GPU buffer resize + bind group recreate works
