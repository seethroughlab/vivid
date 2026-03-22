# Inspector UI Audit and Redesign — Implementation Plan

## Context

The inspector has systemic layout problems: a 320px-wide panel with generic N-column packing that produces 66px columns (4-col at 288px content width), no runtime enforcement of minimum column widths, and a flat vertical stack with no visual separation between params, bindings, and technical sections. Role bindings exposed the issue, but the fix is a broader inspector redesign.

---

## Phase 1: Foundation — Widen Inspector + Layout Normalization

### 1.1 Update width constants
**File:** `src/ui/node_graph_constants.h`

- `kInspectorW`: 320 → 400
- `kInspContentW` auto-adjusts to 368 (computed from `kInspectorW - 2 * kInspPadX`)
- `kInspMinColW`: 110 → 140 (meaningful minimum for two-up knobs)

All downstream code (`inspector_x()`, `graph_right()`, scrollbar, hit-testing) derives from these constants — no manual fixups needed.

### 1.2 Add RowMode enum and normalization to InspectorLayout
**File:** `src/ui/inspector_layout.h`

Add:
```cpp
enum class RowMode : uint8_t { kFull, kTwoUp, kCompound };

static RowMode normalize_row(uint8_t layout_columns, uint8_t col_index,
                             VividDisplayHint hint, VividParamType type,
                             uint32_t choice_count);

void begin_param_normalized(uint8_t layout_columns, uint8_t col_index,
                            VividDisplayHint hint, VividParamType type,
                            uint32_t choice_count);
```

**Normalization rules:**
- `layout_columns < 2` → `kFull`
- `layout_columns == 2` + knob → `kTwoUp`
- `layout_columns == 2` + slider/dropdown → `kTwoUp` (content width / 2 > kInspMinColW)
- `layout_columns >= 3` + knob → `kTwoUp` (re-map indices: 0,1→row1 cols 0,1; 2,3→row2 cols 0,1; odd-out→full)
- `layout_columns >= 3` + slider/dropdown → `kFull` (each param gets own row)
- Color/XY_PAD hint → `kCompound` (full width)

`begin_param_normalized()` calls `normalize_row()`, re-maps column indices, then delegates to existing `begin_param()` with effective columns (1 or 2) and effective col_index (0 or 1).

---

## Phase 2: Section Structure and Visual Hierarchy

### 2.1 Add section separator constants + helper
**File:** `src/ui/node_graph_constants.h`
```cpp
static constexpr float kSectionSepH = 1.0f;
static constexpr float kSectionGapBefore = 12.0f;
static constexpr float kSectionGapAfter = 8.0f;
```

**File:** `src/ui/node_graph_draw.cpp`

Add `draw_section_separator(Renderer2D& tr, float px, float& py, const char* label)` — draws a horizontal rule + optional dim section label.

### 2.2 Restructure draw_inspector() section stack (lines 848-868)
**File:** `src/ui/node_graph_draw.cpp`

Insert section separators:
1. Header + Error banner (unchanged)
2. Params / Custom inspector (unchanged)
3. **Section separator** ("Bindings") — only if role_bindings or referenced_by exist
4. Role Bindings + Referenced By
5. **Section separator** ("Technical") — only if resolution, state presets, or outputs exist
6. Resolution + State Presets + Outputs

### 2.3 Give role bindings and referenced-by panel styling
**File:** `src/ui/node_graph_draw.cpp` — `draw_inspector_role_bindings()` and `draw_inspector_referenced_by()`

Wrap each section in a subtle background rect (slightly lighter than `kInspBg`) with a 2px left accent bar using domain color.

---

## Phase 3: Switch to Normalized Layout in Param Rendering

### 3.1 Replace begin_param with begin_param_normalized
**File:** `src/ui/node_graph_draw.cpp` line 1814

Replace:
```cpp
layout.begin_param(pd.layout_columns, pd.layout_column_index);
```
With:
```cpp
layout.begin_param_normalized(pd.layout_columns, pd.layout_column_index,
                               pd.display_hint, pd.type, pd.choice_count);
```

This is the single line that activates the entire normalization pipeline. Legacy 3/4-col declarations now render as two-up or full-width rows automatically.

---

## Phase 4: Clipping and Truncation Safety Net

### 4.1 Add truncation utility
**File:** `src/ui/node_graph_draw.cpp` (file-local helper)

Extract the existing truncation pattern (lines 1513-1515) into:
```cpp
static std::string truncate_text(Renderer2D& tr, const std::string& text,
                                 float max_w, float scale = 1.0f);
```

### 4.2 Apply truncation in draw_one_inspector_param
**File:** `src/ui/node_graph_draw.cpp`

- Param labels (lines 1444/1446): truncate to `panel_w * 0.55f`
- Source labels (line 1651): truncate to `panel_w`
- Semantic hints (line 1590): truncate to `panel_w`

### 4.3 Add clip rects for knob labels in two-up columns
**File:** `src/ui/node_graph_draw.cpp` — `draw_inspector_knob()`

Push/pop clip rect `[layout.x, layout.x + layout.col_w]` around knob label + value text.

---

## Phase 5: Audit and Convert Dense Operators

Update `collect_params()` in each operator to use declarations that map cleanly to the new model. Changes per operator:

| Operator | File | Current | New |
|---|---|---|---|
| **Envelope** | `operators/control/envelope/envelope.h` | 4-col ADSR knobs | Two 2-col rows: A+D, S+R |
| **LFO** | `operators/control/lfo/lfo.h` | 4-col mixed | 2-col knob pairs, full-width sliders |
| **StepSeq** | `operators/control/step_seq/step_seq.h` | `layout_row(*, 4, 0/1)` | `layout_row(*, 2, 0/1)` |
| **RandomSH** | `operators/control/random_sh/random_sh.h` | `layout_row(*, 4, 0/1)` knobs | `layout_row(*, 2, 0/1)` |
| **MSEG** | `operators/control/mseg/mseg.h` | `layout_row(total_time, 4, 0)` | Remove layout_row (full-width) |
| **Macro** | `operators/control/macro/macro.h` | `layout_row(value, 4, 0)` | Remove layout_row (full-width) |
| **Particles** | `operators/gpu/particles/particles.cpp` | Mixed 2/3/4-col | 2-col pairs, COLOR hint for RGB |
| **Instanced Shapes** | `operators/gpu/instanced_shapes/instanced_shapes.cpp` | Mixed 2/3-col | 2-col, COLOR hint already set |
| **Flocking** | `operators/gpu/flocking/` | Check for 3/4-col | Normalize to 2-col or full |

Note: The Phase 3 normalization handles these automatically at render time, so Phase 5 is cleanup for clarity — not a correctness requirement.

---

## Phase 6: Verification

### Visual checks
Run with these graphs/operators and confirm no overlap, improved readability:
- `graphs/gpu/instanced_shapes_demo.json`
- LFO, Envelope, MSEG, StepSeq
- Any GPU operator with role bindings + referenced-by content

### Automated checks
- Build: `vivid build`
- Run existing tests: `ctest` (role binding tests, hot reload tests)
- Verify scrollbar and hit-testing work at new width
- Verify custom inspector content_width picks up 368px
- Check `src/export/standalone_main.cpp` for hardcoded width references

---

## Dependency Order

```
Phase 1 (constants + normalization) ──→ Phase 3 (switch to normalized layout) ──→ Phase 5 (operator audit)
                                    ╲
Phase 2 (section separators)         ──→ Phase 6 (verification)
                                    ╱
Phase 4 (clipping/truncation) ──────╱
```

Phases 2 and 4 are independent of each other and of Phase 3. Phase 5 depends on Phase 3 being stable.

---

## Critical Files
- `src/ui/inspector_layout.h` — RowMode enum, normalization logic, begin_param_normalized()
- `src/ui/node_graph_constants.h` — kInspectorW, kInspContentW, kInspMinColW, section gap constants
- `src/ui/node_graph_draw.cpp` — Section stack, param rendering, section separators, clipping
- `src/ui/graph_snapshot.h` — ParamInfo (carries layout_columns/display_hint; read-only)
- `src/operator_api/types.h` — VividDisplayHint, VividParamType enums (read-only)
- Operator files listed in Phase 5 table
