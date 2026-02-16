# Stage-Optimized Performer Control Surface

## Context

The `vivid-agent-ready-architecture.md` plan identified this as remaining work: "The plan described large, high-contrast, purpose-grouped parameter controls for dark rooms. Current InspectorPanel is functional but not performance-optimized for stage use." The current InspectorPanel renders parameters with 20px sliders in a 280px floating panel — fine for development, unusable on stage in dim lighting with gloves or from 2+ meters away.

This plan also covers housekeeping: deleting the two completed plan files (`dev/plans/telegram-setup-plan.md` and `dev/plans/vivid-agent-ready-architecture.md`).

---

## Design Decisions

**New `PerformerPanel` class** (not a mode on InspectorPanel). The Inspector is tied to single-operator selection via node clicks. The performer surface shows cross-operator grouped params simultaneously — fundamentally different data flow and layout.

**External grouping config** (`vivid-performer.json`), not core C++ changes. Adding a `group` field to `ParamDecl` would touch every param type across `param.h`. Grouping is a UI concern that belongs in per-project config. Auto-grouping heuristic when no config exists.

**Tabbed groups** (not scrolling). Scrolling is error-prone on stage. Large tab buttons (60px) for group switching with a single tap.

**Stage mode** = fullscreen takeover. Hides all other panels, near-black background, multi-column grid layout, optional cursor hiding.

---

## Implementation

### Phase 1: PerformerPanel with Large Controls

#### 1a. Load performer font (slot 2)

**`modules/vivid-devtools/include/vivid/devtools/devtools.h`**
- Change `m_fonts[2]` → `m_fonts[3]` (OverlayCanvas already supports 3 slots)

**`modules/vivid-devtools/src/devtools.cpp`** (after existing font loading ~line 116)
- Load 28px logical font into slot 2:
  ```cpp
  m_fonts[2] = std::make_unique<FontAtlas>();
  if (m_fonts[2]->load(ctx, fontPath, 28.0f * scale)) {
      m_canvas->setFont(2, m_fonts[2].get());
  }
  ```

#### 1b. Create PerformerPanel

**New: `modules/vivid-devtools/include/vivid/devtools/panels/performer_panel.h`**

```cpp
struct PerformerGroup {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params; // {opName, paramName}
};

struct PerformerLayout {
    std::vector<PerformerGroup> groups;
    std::set<std::string> locked;  // "op.param" keys
    std::set<std::string> hidden;
    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

class PerformerPanel : public Panel {
    // Panel interface: init, shutdown, render, handleInput, onKeyDown
    // Performer API: setChain, setFullscreen, toggleLock, onParamChange
};
```

**New: `modules/vivid-devtools/src/panels/performer_panel.cpp`** (~600-800 lines)

Core rendering approach:
- Custom `GuiStyle`: `widgetHeight=48`, `padding=16`, `cornerRadius=8`, `labelPosition=Above`, `valuePosition=Right`
- Performer theme colors: amber slider fill (`1.0, 0.6, 0.0`), cyan accent (`0.0, 1.0, 0.8`), near-black bg (`0.02, 0.02, 0.04`)
- Font slot 2 (28px) for labels/values
- Collects all params from chain on `setChain()`, caches them
- Renders group tabs at top (60px height), params below using `gui.sliderEx()` / `gui.xyPadEx()` / etc.
- Reuses existing `Gui` widget library as-is — just larger style values

Auto-grouping heuristic when no `vivid-performer.json`:
- Color: `ParamType::Color` params
- Audio: operators with `OutputKind::Audio` or `OutputKind::AudioValue`
- Motion: params named speed/rate/decay/time/velocity
- Other: everything else

#### 1c. Register panel

**`modules/vivid-devtools/CMakeLists.txt`**
- Add `src/panels/performer_panel.cpp` to `DEVTOOLS_SOURCES`

**`modules/vivid-devtools/src/devtools.cpp`**
- Include header, create panel after presets (~line 207)
- Forward `onParamChange` callback (same pattern as InspectorPanel)
- Add `hidePanel("performer")` in initial visibility block (~line 242)
- In `setChain()`: forward chain/projectDir to PerformerPanel (loads `vivid-performer.json`)
- In `registerDefaultShortcuts()`: add Cmd+4 to toggle performer panel

### Phase 2: Locking, MIDI, Snapshots

#### 2a. Lock system
- Per-param lock toggle via right-click on slider
- Locked params render dimmed with lock icon, ignore mouse input
- Lock state persists to `vivid-performer.json` `locked` array
- MIDI CC still applies to locked params (intentional — hardware override)

#### 2b. Enhanced MIDI badges
- Scale from 30x16px → 60x32px for stage-friendly hit targets
- Reuse existing `MidiMapStore::startLearn()` / `completeLearn()` — same store as InspectorPanel
- Font slot 2 for badge text ("CC 64")

#### 2c. Snapshot strip
- Bottom of panel: 9 horizontal snapshot buttons (80x60px each)
- Active snapshot highlighted with accent border
- Tap to recall (using existing `SnapshotStore::recall()` with crossfade)
- Crossfade progress bar below strip
- Same store as PresetPanel — `chain.snapshots()`

### Phase 3: Stage Mode

#### 3a. Fullscreen takeover

**`modules/vivid-devtools/include/vivid/devtools/devtools.h`**
- Add: `void enterStageMode()`, `void exitStageMode()`, `bool inStageMode() const`
- Add: `m_stageMode` bool, `m_preStagePanelVisibility` map

**`modules/vivid-devtools/src/devtools.cpp`**
- `enterStageMode()`: save panel visibility, hide all panels, show performer as fullscreen, set grid opacity 0
- `exitStageMode()`: restore previous visibility
- Shortcut: Cmd+Shift+F or F11 toggles stage mode. Escape exits.

#### 3b. Multi-column grid layout
- When fullscreen and window > 800px: 2-3 columns (~300-400px each)
- Params flow left-to-right, top-to-bottom within active tab
- Maximizes visible params without scrolling

### Housekeeping

- Delete `dev/plans/telegram-setup-plan.md`
- Delete `dev/plans/vivid-agent-ready-architecture.md`

---

## Files Summary

**New files (2):**
- `modules/vivid-devtools/include/vivid/devtools/panels/performer_panel.h`
- `modules/vivid-devtools/src/panels/performer_panel.cpp`

**Modified files (3):**
- `modules/vivid-devtools/CMakeLists.txt` — add source
- `modules/vivid-devtools/src/devtools.cpp` — register panel, font slot 2, shortcut, setChain forwarding, stage mode
- `modules/vivid-devtools/include/vivid/devtools/devtools.h` — `m_fonts[3]`, stage mode API

**Deleted files (2):**
- `dev/plans/telegram-setup-plan.md`
- `dev/plans/vivid-agent-ready-architecture.md`

**Not modified (core stability preserved):**
- `param.h` — no group field on ParamDecl
- `operator.h` — ParamDecl unchanged
- `gui.h` — widget library reused as-is
- `snapshot.h` / `midi_map.h` — reused directly

---

## Key Reference Files

- `modules/vivid-devtools/src/panels/inspector_panel.cpp` — closest existing panel; param iteration loop, MIDI badge rendering, drag callbacks, Gui usage all directly reusable as patterns
- `modules/vivid-core/include/vivid/gui/gui.h` — `GuiStyle` struct for scaling, `sliderEx()` / `xyPadEx()` / `colorPickerHSV()` / `adsrEnvelope()` widgets
- `modules/vivid-core/include/vivid/gui/ui_style.h` — `createHighContrastTheme()` as starting point for performer colors
- `modules/vivid-core/include/vivid/gui/overlay_canvas.h` — 3 font slots confirmed (`m_fonts[3]`)

---

## Verification

1. **Build**: `cmake --build build` — compiles with new panel
2. **Toggle**: Run project with `--show-ui`, press Cmd+4 — performer panel appears with large controls
3. **Grouping**: Add `vivid-performer.json` to a project, verify tabs match groups
4. **Lock**: Right-click a slider, verify it dims and ignores mouse input
5. **MIDI**: Click large MIDI badge, turn CC knob, verify mapping with oversized badge
6. **Snapshots**: Press 1-9, verify snapshot recall from performer strip
7. **Stage mode**: Cmd+Shift+F, verify all panels hidden, performer fills screen, Escape exits
8. **Param changes**: Adjust slider, verify `onParamChange` callback fires (Claude MCP workflow intact)
