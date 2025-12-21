# Synth Preset System

## Goal
Add a preset system for synths that supports:
1. Built-in factory presets (like FMSynth's current hardcoded ones)
2. User-created presets saved to files
3. Shareable preset format (JSON)

## Current State
- FMSynth has hardcoded `loadPreset(FMPreset::EPiano)` etc.
- ParamRegistry provides `getRegisteredParam()` / `setRegisteredParam()` by name
- No file-based preset loading/saving exists

## Design Decisions

- **Factory presets**: JSON files shipped in presets/ directory (easier to edit/add)
- **Compatibility**: Synth-specific only (FMSynth presets only work with FMSynth)
- **Categories**: User-defined strings (no fixed set)

## Preset File Format (JSON)
```json
{
  "name": "Warm Pad",
  "author": "jeff",
  "category": "Pads",
  "synth": "FMSynth",
  "version": 1,
  "algorithm": "Stack4",
  "params": {
    "ratio1": 1.0,
    "ratio2": 2.0,
    "level1": 1.0,
    "feedback": 0.3,
    "attack": 0.5,
    "decay": 0.2,
    "sustain": 0.7,
    "release": 1.0
  }
}
```

## API Design

```cpp
// Base class for preset-capable synths
class PresetCapable {
public:
    // Save current state to file
    bool savePreset(const std::string& path);

    // Load preset from file
    bool loadPreset(const std::string& path);

    // List available presets (factory + user)
    static std::vector<PresetInfo> listPresets(const std::string& synthType);

    // Get preset directories
    static std::string factoryPresetDir();  // bundled with app
    static std::string userPresetDir();      // ~/.vivid/presets/

protected:
    // Override in subclass to handle non-param state (algorithm, waveform, etc.)
    virtual void serializeExtra(nlohmann::json& j) const {}
    virtual void deserializeExtra(const nlohmann::json& j) {}

    // Synth type identifier for file format
    virtual std::string synthType() const = 0;
};
```

## Directory Structure
```
~/.vivid/presets/
├── FMSynth/
│   ├── factory/           # Read-only, shipped with app
│   │   ├── EPiano.json
│   │   ├── Bass.json
│   │   └── ...
│   └── user/              # User-created
│       └── MyPad.json
├── PolySynth/
│   ├── factory/
│   └── user/
└── WavetableSynth/
    ├── factory/
    └── user/
```

## Implementation Steps

1. **Add PresetCapable mixin** (`core/include/vivid/preset.h`)
   - JSON serialization using nlohmann/json (already in deps?)
   - Save/load methods
   - Preset listing/discovery

2. **Make synths PresetCapable**
   - FMSynth: Override `serializeExtra` for algorithm
   - PolySynth: Override for waveform
   - WavetableSynth: Override for wavetable selection

3. **Convert FMSynth hardcoded presets to JSON files**
   - Move EPiano, Bass, Bell, etc. to factory preset files
   - Keep `loadPreset(FMPreset::)` for backwards compat, load from JSON

4. **Add preset browser UI** (optional, later)
   - ImGui panel for browsing/loading presets
   - Save dialog for new presets

## Files to Create/Modify

| File | Action |
|------|--------|
| `core/include/vivid/preset.h` | New - PresetCapable base class |
| `core/src/preset.cpp` | New - Implementation |
| `addons/vivid-audio/include/vivid/audio/fm_synth.h` | Inherit PresetCapable |
| `addons/vivid-audio/src/fm_synth.cpp` | Implement serialization |
| `addons/vivid-audio/presets/FMSynth/*.json` | New - Factory presets |
