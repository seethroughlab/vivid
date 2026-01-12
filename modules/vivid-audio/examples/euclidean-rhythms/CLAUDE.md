# Euclidean Rhythms

Polyrhythmic patterns using the Euclidean algorithm.

## Operators Used

- **Euclidean** - Euclidean rhythm generator
- **Clock** - Master tempo
- **Kick, Snare, HiHat, Clap** - Drum voices
- **AudioMixer** - Combine drums

## Key Concepts

### Euclidean Algorithm
Distributes K hits evenly across N steps. Many traditional rhythms are Euclidean:

| Pattern | Name | Genre |
|---------|------|-------|
| E(3,8) | Tresillo | Cuban, hip-hop |
| E(5,8) | Cinquillo | Cuban |
| E(5,16) | Bossa nova | Brazilian |
| E(7,16) | Samba | Brazilian |
| E(4,16) | Four-on-floor | House, disco |
| E(3,4) | Waltz | Classical |

### Basic Setup
```cpp
auto& eucl = chain.add<Euclidean>("eucl");
eucl.steps = 16;      // Total steps in cycle
eucl.hits = 5;        // Number of active hits
eucl.rotation = 0;    // Pattern offset
```

### Parameters
- `steps` (int, 2-16) - Total steps in the pattern
- `hits` (int, 1-16) - Number of active steps
- `rotation` (int, 0-15) - Rotate pattern forward

### Audio-Thread Triggering (Recommended)
Connect operators via `setTriggerSource()` for sample-accurate timing:
```cpp
// In setup():
auto& kick_eucl = chain.add<Euclidean>("kick_eucl");
kick_eucl.setTriggerSource("clock");  // Euclidean advances on clock
kick_eucl.steps = 16;
kick_eucl.hits = 4;

auto& kick = chain.add<Kick>("kick");
kick.setTriggerSource("kick_eucl");   // Drum triggers on Euclidean output

// In update() - just poll for visual feedback:
if (kick_eucl.triggered()) kickDecay = 1.0f;
```

### Polyrhythm Example
Layer multiple Euclidean patterns with different step counts:
```cpp
// 16-step kick
auto& kick_eucl = chain.add<Euclidean>("kick_eucl");
kick_eucl.setTriggerSource("clock");
kick_eucl.steps = 16;
kick_eucl.hits = 4;

// 8-step snare (2:1 ratio with kick)
auto& snare_eucl = chain.add<Euclidean>("snare_eucl");
snare_eucl.setTriggerSource("clock");
snare_eucl.steps = 8;
snare_eucl.hits = 3;  // Tresillo

// Connect drums to Euclidean patterns
kick.setTriggerSource("kick_eucl");
snare.setTriggerSource("snare_eucl");
```

### Legacy Main-Thread Pattern
Still works but has ~16ms timing jitter:
```cpp
if (clock.triggered()) {
    kick_eucl.advance();
    snare_eucl.advance();

    if (kick_eucl.triggered()) kick.trigger();
    if (snare_eucl.triggered()) snare.trigger();
}
```

### Pattern Rotation
Shift the pattern start point for groove variation:
```cpp
eucl.rotation = 2;  // Start 2 steps later
```

This changes where the "downbeat" falls, creating different feels.

## Controls

- **Mouse X** - BPM (80-160)
- **Mouse Y** - Pattern complexity (number of hits)

## Famous Euclidean Rhythms

### Tresillo E(3,8)
```cpp
eucl.steps = 8;
eucl.hits = 3;
// Pattern: X..X..X.
```

### Cinquillo E(5,8)
```cpp
eucl.steps = 8;
eucl.hits = 5;
// Pattern: X.XX.XX.
```

### Bossa Nova E(5,16)
```cpp
eucl.steps = 16;
eucl.hits = 5;
// Pattern: X..X..X..X..X...
```

### Four-on-Floor E(4,16)
```cpp
eucl.steps = 16;
eucl.hits = 4;
// Pattern: X...X...X...X...
```

## Visual Feedback
Concentric rings pulse with each drum hit:
```cpp
float kickEnv = kick.ampEnvelope();
kick_ring.size.set(0.3f + kickEnv * 0.15f, 0.3f + kickEnv * 0.15f);
```

The rings rotate at different speeds, creating a hypnotic visual that reinforces the polyrhythmic nature of the patterns.
