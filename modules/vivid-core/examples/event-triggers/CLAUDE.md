# Event Triggers

Demonstrates the Trigger operator for converting discrete events into smooth decay envelopes.

## Operators Used

- **Trigger** - Event-driven envelope generator with attack/decay

## Key Concepts

### What is Trigger?

Trigger is a utility operator that converts discrete events (button presses, MIDI notes, OSC messages) into smooth 0-1 values with configurable attack and decay. This is essential for:

- Making visual effects respond to beats/notes
- Creating punchy or sustained responses
- Driving any parameter from external events

### Basic Usage

```cpp
// Setup: Create a trigger
auto& trigger = chain.add<Trigger>("trigger");
trigger.decay = 0.92f;  // 0.8 = fast, 0.99 = slow

// Update: Fire the trigger
if (someEvent) {
    trigger.fire();           // Full intensity (1.0)
    trigger.fire(0.5f);       // Custom intensity
}

// Read the value (decays automatically each frame)
float val = trigger.value();  // 0.0 to 1.0
bool active = trigger.active();  // True if value > 0.001
```

### Parameters

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| `attack` | 0-1 | 0.0 | Attack time (0 = instant, 1 = ~1 second ramp) |
| `decay` | 0.5-0.999 | 0.92 | Decay rate per frame |

### Decay Rate Guide

| Decay | Feel | Use Case |
|-------|------|----------|
| 0.80-0.85 | Punchy, fast | Kick drums, sharp hits |
| 0.90-0.93 | Medium, balanced | Snares, general triggers |
| 0.95-0.97 | Sustained | Pads, cymbals, swells |
| 0.98-0.99 | Very slow | Atmosphere, long fades |

### Attack Parameter

Attack controls the ramp-up time when triggered:

```cpp
// Instant response (default)
trigger.attack = 0.0f;
trigger.fire();  // Immediately jumps to target

// Slow attack (swell effect)
trigger.attack = 0.5f;
trigger.fire();  // Gradually ramps up to target
```

### Driving Visual Parameters

```cpp
// Noise scale responds to trigger
noise.scale = 4.0f + trigger.value() * 8.0f;

// Bloom intensity
bloom.intensity = 0.3f + trigger.value() * 2.0f;

// Color alpha
shape.color = glm::vec4(1, 1, 1, trigger.value());

// Position offset
float offset = trigger.value() * 0.2f;
```

### Multiple Triggers

Use separate triggers for different events:

```cpp
auto& kickTrigger = chain.add<Trigger>("kick");
kickTrigger.decay = 0.85f;  // Punchy

auto& snareTrigger = chain.add<Trigger>("snare");
snareTrigger.decay = 0.92f;  // Medium

auto& hihatTrigger = chain.add<Trigger>("hihat");
hihatTrigger.decay = 0.97f;  // Sustained

// Drive different effects
noise.scale = 4.0f + kickTrigger.value() * 6.0f;
hsv.hueShift = snareTrigger.value() * 0.3f;
bloom.intensity = hihatTrigger.value();
```

### With MIDI Input

```cpp
auto& midiIn = chain.add<MidiIn>("midi");
auto& trigger = chain.add<Trigger>("trigger");

// In update:
for (const auto& e : midiIn.events()) {
    if (e.type == MidiEventType::NoteOn) {
        trigger.fire(e.velocity / 127.0f);
    }
}
```

### With OSC Input

```cpp
auto& osc = chain.add<OscIn>("osc");
auto& trigger = chain.add<Trigger>("trigger");

// In update:
if (osc.hasMessage("/trigger")) {
    trigger.fire(osc.getFloat("/trigger", 1.0f));
}
```

## Controls

| Key | Action |
|-----|--------|
| 1 | Fire fast trigger (decay=0.85) |
| 2 | Fire medium trigger (decay=0.92) |
| 3 | Fire slow trigger (decay=0.97) |
| 4 | Fire attack trigger (attack=0.5) |
| Q/W/E | Fire fast at low/med/high intensity |
| Space | Fire all triggers |
| R | Reset all triggers |

## Why Use Trigger Instead of Manual Decay?

Before Trigger, you'd write:

```cpp
// Manual decay (error-prone, verbose)
static float noteDisplay = 0.0f;
if (midiIn.noteOn()) {
    noteDisplay = midiIn.velocity();
}
noteDisplay *= 0.92f;
if (noteDisplay < 0.01f) noteDisplay = 0.0f;
```

With Trigger:

```cpp
// Clean, reusable, configurable
if (midiIn.noteOn()) {
    trigger.fire(midiIn.velocity());
}
float noteDisplay = trigger.value();
```

Benefits:
- No static variables cluttering code
- Decay rate is a tunable parameter
- Optional attack for swell effects
- `active()` method for clean conditionals
- `reset()` to clear state
- Visible in chain visualizer
