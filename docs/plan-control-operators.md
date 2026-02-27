# Plan: Missing Control Operators

## Context

PRD Section 4.3 defines the "minimum viable set" for the orchestration layer — all automation and logic should be expressible as visible Control operators in the graph, with no hidden scripting layer. The PRD lists: LFO, Clock, Sequencer, Pattern, Envelope, Math, Logic, Gate, Random, Smooth/Lerp.

We have the first 6 (plus NotePattern, ChordProgression, Arpeggiator, FFTAnalysis, MidiInput — well beyond the minimum). But 4 core primitives are missing: **Logic, Gate, Random, Smooth**. A general-purpose **Sequencer** is also absent (NotePattern is chord-focused, not a generic step sequencer).

Without these, certain common automation patterns require workarounds or can't be expressed at all:
- "Only pass audio reactivity through when MIDI gate is held" → needs Gate + Logic
- "Smoothly interpolate a jumpy MIDI CC value" → needs Smooth
- "Add controlled randomness to particle sizes" → needs Random
- "Step through 8 brightness levels on each beat" → needs Sequencer

## Operators to Build

### 1. Logic
**File:** `operators/control/logic/logic.cpp`

**Params:**
| Name | Type | Default | Notes |
|------|------|---------|-------|
| `operation` | `Param<int>` (enum) | 0 | Labels: `{"AND", "OR", "XOR", "NOT", "NAND", "NOR"}` |

**Ports:**
| Name | Type | Direction |
|------|------|-----------|
| `a` | CONTROL_FLOAT | INPUT |
| `b` | CONTROL_FLOAT | INPUT |
| `result` | CONTROL_FLOAT | OUTPUT |

**Behavior:** Inputs treated as bool (>0.5 = true). Output is 0.0 or 1.0. NOT ignores input `b`. No state, no time dependency. Direct pattern match to Math operator.

**Pattern to follow:** `operators/control/math/math.cpp`

---

### 2. Gate
**File:** `operators/control/gate/gate.cpp`

**Params:**
| Name | Type | Default | Min | Max |
|------|------|---------|-----|-----|
| `threshold` | `Param<float>` | 0.5 | 0.0 | 10000.0 |
| `invert` | `Param<bool>` | false | — | — |

**Ports:**
| Name | Type | Direction |
|------|------|-----------|
| `signal` | CONTROL_FLOAT | INPUT |
| `gate` | CONTROL_FLOAT | INPUT |
| `value` | CONTROL_FLOAT | OUTPUT |
| `open` | CONTROL_FLOAT | OUTPUT |

**Behavior:** When `gate > threshold`, pass `signal` through to `value`; else output 0. `open` outputs 1.0/0.0 gate state (useful for driving other operators). `invert` flips the logic. No state, no time dependency.

**Pattern to follow:** `operators/control/math/math.cpp`

---

### 3. Random
**File:** `operators/control/random/random.cpp`

**Params:**
| Name | Type | Default | Min | Max |
|------|------|---------|-----|-----|
| `min` | `Param<float>` | 0.0 | -10000.0 | 10000.0 |
| `max` | `Param<float>` | 1.0 | -10000.0 | 10000.0 |
| `distribution` | `Param<int>` (enum) | 0 | — | — |
| `seed` | `Param<int>` | 12345 | 1 | 99999 |
| `free_run` | `Param<bool>` | true | — | — |

Distribution labels: `{"uniform", "gaussian"}`

**Ports:**
| Name | Type | Direction |
|------|------|-----------|
| `trigger` | CONTROL_FLOAT | INPUT |
| `value` | CONTROL_FLOAT | OUTPUT |

**Internal state:** xorshift32 RNG state, current held value, previous trigger for edge detection, last seed for change detection.

**Behavior:**
- `free_run = true`: generate new random value every frame
- `free_run = false`: generate only on rising edge of `trigger` input
- Uniform: map xorshift output to [min, max]
- Gaussian: Box-Muller transform, mean = midpoint, stddev = (max-min)/6, clamped to [min, max]
- Seed changes reset the RNG state (for reproducibility)

**Pattern to follow:** `operators/control/lfo/lfo.cpp` (stateful), `operators/control/arpeggiator/arpeggiator.cpp` (xorshift32 RNG)

---

### 4. Smooth
**File:** `operators/control/smooth/smooth.cpp`

**Params:**
| Name | Type | Default | Min | Max |
|------|------|---------|-----|-----|
| `rise_time` | `Param<float>` | 0.1 | 0.0 | 10.0 |
| `fall_time` | `Param<float>` | 0.1 | 0.0 | 10.0 |

**Ports:**
| Name | Type | Direction |
|------|------|-----------|
| `input` | CONTROL_FLOAT | INPUT |
| `value` | CONTROL_FLOAT | OUTPUT |

**Internal state:** `float smoothed_`, `bool first_frame_`

**Behavior:**
- Exponential moving average: `coeff = 1 - exp(-dt / tau)`
- Asymmetric: use `rise_time` when target > current, `fall_time` when target < current
- If tau <= 0 or dt <= 0: snap instantly
- First frame: snap to input (no startup ramp from zero)
- `kTimeDependent = true` (reads `ctx->delta_time`)
- Framerate-independent (the exp formula handles variable dt correctly)

**Pattern to follow:** `operators/control/lfo/lfo.cpp` (time-dependent, stateful)

---

### 5. Sequencer
**File:** `operators/control/sequencer/sequencer.cpp`

**Params:**
| Name | Type | Default | Min | Max |
|------|------|---------|-----|-----|
| `steps` | `Param<int>` | 8 | 1 | 16 |
| `val_0` .. `val_15` | `Param<float>` | 0.0 | -10000.0 | 10000.0 |

**Ports:**
| Name | Type | Direction |
|------|------|-----------|
| `phase` | CONTROL_FLOAT | INPUT |
| `reset` | CONTROL_FLOAT | INPUT |
| `value` | CONTROL_FLOAT | OUTPUT |
| `step` | CONTROL_FLOAT | OUTPUT |
| `trigger` | CONTROL_FLOAT | OUTPUT |

**Internal state:** `int prev_step_`, `bool prev_reset_`, `float phase_offset_`

**Behavior:**
- Phase-driven: `current_step = int(effective_phase * steps)`, clamped
- Read value from `ctx->param_values[1 + current_step]` (step params follow `steps` param)
- `trigger` output = 1.0 on the frame step changes, 0.0 otherwise
- `step` output = current step index as float (0-based)
- Reset on rising edge: captures current phase as offset, so sequence restarts from step 0
- Distinct from NotePattern (which outputs chord spreads for music). This is a generic automation sequencer.

**Pattern to follow:** `operators/control/note_pattern/note_pattern.cpp` (phase-driven, many per-step params)

---

## Build System

Add to `CMakeLists.txt` in the control operator section:

```cmake
add_vivid_operator(logic      operators/control/logic/logic.cpp)
add_vivid_operator(gate       operators/control/gate/gate.cpp)
add_vivid_operator(random     operators/control/random/random.cpp)
add_vivid_operator(smooth     operators/control/smooth/smooth.cpp)
add_vivid_operator(sequencer  operators/control/sequencer/sequencer.cpp)
```

No EXTRA_LIBS needed — all are pure control-domain operators.

## Demo Graph

Create `graphs/control_demo.json`:
- Clock → Sequencer (phase-driven step values)
- Sequencer/value → Smooth (glide between steps)
- Smooth/value → Noise/scale
- Clock/beat_tick → Random/trigger (new random value each beat)
- Random/value → Shape/radius
- Logic (gate AND beat_tick) controlling something visible

## Verification

1. Build: `cmake --build build`
2. All 5 operators appear in Tab chooser under Control domain
3. Wire Clock → Sequencer → Smooth → Noise/scale: noise scale steps through values with smooth glide
4. Random in trigger mode: generates new value only on beat
5. Logic AND: only passes 1.0 when both inputs are > 0.5
6. Gate: passes signal through only when gate input exceeds threshold
7. Existing tests pass: `ctest --test-dir build`

## Implementation Order

1. Logic (simplest, good build pipeline sanity check)
2. Gate (nearly identical pattern)
3. Random (introduces stateful RNG)
4. Smooth (introduces time-dependent math)
5. Sequencer (most params/ports, most complex)
6. Demo graph
