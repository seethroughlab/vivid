# Plan: UI Visual Spec Alignment

## Context

PRD Section 6.6 defines a specific visual style for Vivid: "dark steel with colored accents," domain-colored elements, dashed cross-domain wires, a workspace grid, a transport bar, and modulation overlays. The current UI implements the core structure (node graph, inspector, thumbnails) well, but several spec items are missing or don't match.

This plan brings the UI into alignment with the PRD visual spec.

---

## 1. Domain-Colored Inspector Sliders

**Effort:** Tiny (one-line change)

### Problem
All inspector sliders use a fixed blue fill (`kSliderFill = {0.25, 0.42, 0.68}`), regardless of the node's domain. The PRD says "slider tracks are dark with a domain-colored fill."

### Fix
**File:** `src/ui/node_graph_draw.cpp`

In `draw_one_inspector_param()`, replace the slider fill color:

```cpp
// Before:
tr.draw_rect(sx, sy, sw * t, sh, kSliderFill[0], kSliderFill[1], kSliderFill[2]);

// After:
const float* slider_col = domain_color(node.domain);
tr.draw_rect(sx, sy, sw * t, sh, slider_col[0], slider_col[1], slider_col[2]);
```

The `node.domain` field is already available in the `NodeSnapshot` passed to the draw function. `domain_color()` already maps to cyan/amber/gray in `node_graph_constants.h`.

---

## 2. Workspace Grid

**Effort:** Small

### Problem
The node graph has a flat dark background. The PRD specifies "a subtle grid underlays the node graph — very low opacity, in the GPU accent color."

### Implementation

**File:** `src/ui/node_graph_constants.h` — add constants:
```cpp
static constexpr float kGridSpacing = 40.0f;
static constexpr float kGridLineAlpha = 0.06f;
```

**File:** `src/ui/node_graph.h` — add declaration:
```cpp
void draw_grid(Renderer2D& tr);
```

**File:** `src/ui/node_graph_draw.cpp` — add method:
- Draw vertical + horizontal lines in `kGpuAccent` (cyan) at `kGridLineAlpha` opacity
- Lines are 1px, spaced at `kGridSpacing` in graph space
- Respects zoom and pan (convert grid positions through `gx_to_sx`/`gy_to_sy`)
- Skip if screen-space spacing < 8px (prevents visual noise at extreme zoom-out)
- Called from `draw()` before `draw_graph()` — grid sits behind everything

---

## 3. Cross-Domain Wire Dashing

**Effort:** Medium

### Problem
The PRD says "cross-domain wires (Control→GPU, Control→Audio) are dashed to indicate the bridge crossing." Currently all wires are drawn solid.

### Implementation

**File:** `src/ui/node_graph_draw.cpp` (in `draw_connections()`)

Detection is straightforward — `from_rect.domain` and `to_rect.domain` are already available at the point where wires are drawn (around line 228). When they differ, the wire is cross-domain.

For dashed rendering, replace the solid `traverse_wire` + `draw_line` call with a dashing approach:
- Track cumulative length along the wire (bezier or z-route segments)
- Alternate between "on" (8px) and "off" (6px) regions
- Only emit `draw_line` calls during "on" regions
- The `traverse_wire` template already provides per-segment callbacks — subdivide each segment based on the dash pattern

```cpp
bool cross_domain = (from_rect.domain != to_rect.domain);

if (cross_domain) {
    float cum_len = 0.0f;
    constexpr float kDashOn = 8.0f, kDashOff = 6.0f;
    constexpr float kDashCycle = kDashOn + kDashOff;

    traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
        [&](float x0, float y0, float x1, float y1) {
            float seg_len = std::hypot(x1 - x0, y1 - y0);
            // Walk along segment emitting dash sub-segments...
            // (subdivide at dash boundaries, only draw "on" portions)
            cum_len += seg_len;
        });
} else {
    // Existing solid wire drawing
    traverse_wire(ssx, ssy, sex, sey, bezier_wires_,
        [&](float x0, float y0, float x1, float y1) {
            tr.draw_line(x0, y0, x1, y1, wire_th, cr, cg, cb, a);
        });
}
```

---

## 4. Transport Bar

**Effort:** Medium

### Problem
The PRD Section 6.6 specifies: "Beat position as filled/unfilled dots. BPM as a number. Current state name." No transport bar exists.

### Data Pipeline

**File:** `src/ui/graph_snapshot.h` — add to `GraphSnapshot`:
```cpp
float transport_bpm = 120.0f;
float transport_beat_phase = 0.0f;   // 0.0-1.0 within current beat
int   transport_beat_index = 0;      // 0-3 (current beat in 4/4 bar)
double transport_elapsed = 0.0;
bool  transport_has_clock = false;
```

**File:** `src/runtime/main.cpp` (snapshot builder) — scan for Clock node:
```cpp
for (const auto& sn : snap.nodes) {
    if (sn.type_name == "Clock") {
        snap.transport_has_clock = true;
        // Extract bpm param and beat_phase output from the node's snapshot data
        break;
    }
}
```

### Drawing

**File:** `src/ui/node_graph_constants.h` — add:
```cpp
static constexpr float kTransportBarH = 28.0f;
static constexpr float kTransportDotSize = 8.0f;
static constexpr float kTransportDotSpacing = 16.0f;
```

**File:** `src/ui/node_graph.h` — add declaration:
```cpp
void draw_transport_bar(Renderer2D& tr);
```

**File:** `src/ui/node_graph_draw.cpp` — new `draw_transport_bar()`:
- Horizontal strip at the bottom of the window
- Only drawn when `snap_.transport_has_clock` is true
- Shows:
  - 4 beat dots (filled square = current beat, unfilled = other beats)
  - BPM as a number (e.g., "120 BPM")
  - Elapsed time (e.g., "3:42")
- Style: dark background matching perf bar, domain-neutral accent color for dots
- Called from `draw()` after other elements

---

## 5. Modulation Range Overlays

**Effort:** Medium

### Problem
The PRD says "modulation range overlays appear as subtle highlights showing the modulated range" on inspector sliders, inspired by Bitwig's modulation display. When a parameter is driven by an incoming wire, the user should see the range of the modulation.

### Data Pipeline

Need to identify which params have incoming wires and what range they span.

**File:** `src/ui/node_graph.h` — add private member:
```cpp
// key: "param_name", value: current modulation source value
std::unordered_map<std::string, float> wired_param_values_;
```

**File:** `src/ui/node_graph.cpp` (or wherever `update()` is called) — compute wired params for the selected node:
```cpp
wired_param_values_.clear();
if (!selected_node_id_.empty()) {
    for (const auto& c : snap_.connections) {
        if (c.to_node != selected_node_id_) continue;
        // Check if to_port is a param (not an input port)
        const auto* target_node = snap_.find_node(c.to_node);
        if (!target_node) continue;
        if (target_node->param_indices.count(c.to_port) == 0) continue;
        // Look up source output value
        const auto* src_node = snap_.find_node(c.from_node);
        if (!src_node) continue;
        auto src_it = src_node->output_port_indices.find(c.from_port);
        if (src_it == src_node->output_port_indices.end()) continue;
        if (src_it->second < src_node->output_values.size()) {
            wired_param_values_[c.to_port] = src_node->output_values[src_it->second];
        }
    }
}
```

### Drawing

**File:** `src/ui/node_graph_draw.cpp` (in `draw_one_inspector_param()`)

After drawing the slider fill, check if this param is wired:
```cpp
auto mod_it = wired_param_values_.find(pd.name);
if (mod_it != wired_param_values_.end()) {
    const float* mod_col = domain_color(node.domain);
    float mod_val = mod_it->second;
    float mod_t = (range > 0) ? (mod_val - pd.min_value) / range : 0.0f;
    mod_t = std::clamp(mod_t, 0.0f, 1.0f);
    float t_min = std::min(t, mod_t);
    float t_max = std::max(t, mod_t);
    // Semi-transparent overlay between static value and modulated value
    tr.draw_rect(sx + sw * t_min, sy, sw * (t_max - t_min), sh,
                 mod_col[0], mod_col[1], mod_col[2], 0.20f);
}
```

This shows the "sweep range" of the modulation as a subtle domain-colored highlight on the slider track.

---

## Implementation Order

| Order | Item | Effort | Dependencies |
|-------|------|--------|--------------|
| 1 | Domain-colored sliders | Tiny | None |
| 2 | Workspace grid | Small | None |
| 3 | Cross-domain wire dashing | Medium | None |
| 4 | Transport bar | Medium | None |
| 5 | Modulation range overlays | Medium | None |

All items are independent and can be done in any order. The suggested order is from simplest to most complex.

## Verification

Visual inspection against PRD Section 6.6:
1. **Sliders:** Inspector sliders use cyan fill for GPU nodes, amber for audio, gray for control
2. **Grid:** Subtle cyan grid visible behind nodes at default zoom; disappears at extreme zoom-out
3. **Wires:** Same-domain wires are solid; cross-domain wires (e.g., Clock→Noise) are dashed
4. **Transport:** When a Clock node is in the graph, a transport bar appears at the bottom showing beat dots, BPM, and elapsed time
5. **Modulation:** When a parameter has an incoming wire, the slider shows a semi-transparent overlay indicating the modulation range

## Key Reference Files

- `src/ui/node_graph_draw.cpp` — all drawing code
- `src/ui/node_graph_constants.h` — colors, dimensions, `domain_color()`, `traverse_wire()`
- `src/ui/node_graph.h` — UI state and method declarations
- `src/ui/graph_snapshot.h` — snapshot data structures
- `src/ui/ui_style.h` — style definitions (dark_bg, accent colors, etc.)
- `src/runtime/main.cpp` — snapshot builder (`build_graph_snapshot`)
