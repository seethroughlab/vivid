# MIDI Debug Values Feature

Add `ctx.debug(midiIn)` API to dump all active CC values to the debug panel.

## User-Facing API

```cpp
void update(Context& ctx) {
    auto& midi = chain.get<MidiIn>("midi");
    ctx.debug(midi);  // Shows "MidiIn.CC1", "MidiIn.CC19", etc. in debug panel (D key)
    chain.process(ctx);
}
```

## Design

Use a **Debuggable interface** so core remains addon-agnostic and the pattern is reusable for OscIn, SerialIn, etc.

## Files to Modify

### 1. NEW: `core/include/vivid/debuggable.h`

```cpp
#pragma once
#include <string>
#include <functional>

namespace vivid {

using DebugEmitter = std::function<void(const std::string& name, float value)>;

class Debuggable {
public:
    virtual ~Debuggable() = default;
    virtual void emitDebugValues(const std::string& prefix, DebugEmitter emit) const = 0;
};

} // namespace vivid
```

### 2. `core/include/vivid/context.h`

Add template overload after existing debug() methods (~line 490):

```cpp
#include <vivid/debuggable.h>
#include <type_traits>

template<typename T>
void debug(const T& op) {
    static_assert(std::is_base_of_v<Debuggable, T>,
        "Operator must implement Debuggable interface");
    op.emitDebugValues(op.name(), [this](const std::string& name, float value) {
        debug(name, value);
    });
}
```

### 3. `addons/vivid-midi/include/vivid/midi/midi_in.h`

- Add `#include <vivid/debuggable.h>`
- Inherit from `Debuggable`
- Add private member: `std::array<bool, 128> m_ccEverTouched{};`
- Implement `emitDebugValues()`:

```cpp
void emitDebugValues(const std::string& prefix, DebugEmitter emit) const override {
    for (int i = 0; i < 128; ++i) {
        if (m_ccEverTouched[i]) {
            char name[32];
            snprintf(name, sizeof(name), "%s.CC%d", prefix.c_str(), i);
            emit(name, m_ccValues[i]);
        }
    }
}
```

### 4. `addons/vivid-midi/src/midi_in.cpp`

- In constructor: `m_ccEverTouched.fill(false);`
- In `processMessage()` case 0xB0: `m_ccEverTouched[event.cc] = true;`

## Additional: Console Logging for AI Assistance

Add a `verbose` flag to MidiIn that prints CC changes to stdout:

### 5. `addons/vivid-midi/include/vivid/midi/midi_in.h`

Add public member:
```cpp
bool verbose = false;  ///< Print all incoming MIDI to console
```

### 6. `addons/vivid-midi/src/midi_in.cpp`

In `processMessage()` case 0xB0, after updating `m_ccValues`:
```cpp
if (verbose) {
    std::cout << "[MIDI] CC " << (int)event.cc << " = "
              << m_ccValues[event.cc] << std::endl;
}
```

### Usage for AI-assisted mapping:

```cpp
// In setup():
auto& midi = chain.add<MidiIn>("midi");
midi.openPortByName("MIDI Mix");
midi.verbose = true;  // Enable console output

// In update():
ctx.debug(midi);  // Also show in visual debug panel
```

When user moves a control, console shows:
```
[MIDI] CC 19 = 0.724
[MIDI] CC 19 = 0.756
```

Claude Code can then read the terminal output and say: "That control is CC 19. Let me update the mapping."

## Testing

Verify the implementation with `examples/showcase/prelinger-nostalgia`:

1. Add to chain.cpp setup:
   ```cpp
   midi.verbose = true;
   ```

2. Add to chain.cpp update:
   ```cpp
   ctx.debug(midi);
   ```

3. Run and verify:
   - Move faders/knobs on MIDI Mix
   - Check console shows `[MIDI] CC X = Y` for each control
   - Press D key, confirm CC values appear in debug panel with sparklines
   - Cross-reference CC numbers with `midi_mapping.h` constants
   - Confirm all 24 knobs + 8 faders + buttons show expected CC numbers

4. Remove verbose flag after testing (or leave for development)

## Result

1. **Visual**: Debug panel sparklines (press D key)
2. **Console**: `[MIDI] CC X = Y` output for AI/automation
3. Only shows CCs that have been touched
