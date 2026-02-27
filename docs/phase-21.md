# Phase 21 Design: State Machines

A general-purpose `StateMachine` control operator that drives macro-level structure — song sections, installation modes, live performance cues — by cycling through named states with configurable durations and transition rules.

## Motivation

Most Vivid patches today are reactive: they respond to audio input or evolve continuously. That's powerful for textures and improvisation, but it doesn't give you *composed arc* — the ability to say "play an intro for 8 bars, then switch to the verse." Three use cases motivate this:

**Song structure.** Intro → verse → chorus → bridge → outro, each with different note patterns, drum patterns, and visual character. The state machine drives section changes; downstream operators read the state index and progress to select patterns and crossfade parameters.

**Installation.** Idle → active → cooldown → idle, driven by sensor thresholds. An art installation sits in a calm idle state, ramps up when a visitor approaches (threshold crossing on an input signal), holds the active state for a fixed duration, then cools down and returns to idle.

**Live performance.** Manual section switching with quantized transitions. A performer triggers "go to drop" mid-bar, and the transition fires on the next bar boundary rather than immediately, keeping everything musically aligned.

## Existing Patterns in the Codebase

The state machine operator builds on patterns already proven across several control operators.

### Explicit state enum (Envelope)

`operators/control/envelope/envelope.cpp` has a five-state machine (IDLE → ATTACK → DECAY → SUSTAIN → RELEASE) with time-based progression through stages, edge detection for gate on/off, and phase-wrap detection for re-triggering. Key patterns:

- `Stage stage_ = IDLE` with a switch/if chain in `process()` advancing through states
- `env_progress_ += dt` accumulates time within the current stage
- Normalized progress `t = env_progress_ / stage_time` gives 0–1 within a stage
- Rising-edge gate detection: `gate_on && !prev_gate_`

### Step-through via phase (Sequencer, DrumSequencer)

`operators/control/sequencer/sequencer.cpp` maps a continuous phase input to discrete steps:

```cpp
float adj_phase = std::fmod(phase - phase_offset_ + 1.0f, 1.0f);
int step = static_cast<int>(adj_phase * n);
```

Step change detection (`step != prev_step_`) fires a trigger output. The DrumSequencer uses the identical pattern for 6 parallel drum channels.

### Beat counting from phase wraps (NotePattern, ChordProgression, Arpeggiator)

These operators count beats by detecting when `beat_phase` wraps around:

```cpp
float delta = beat_phase - prev_phase_;
if (delta < -0.5f)
    beat_count_++;
```

This converts a continuous 0–1 phase signal into a monotonic beat counter, which then indexes into multi-beat patterns. The StateMachine uses this same technique to count bars for duration-based transitions.

### Rising-edge reset (Sequencer, DrumSequencer)

```cpp
if (reset && !prev_reset_)
    phase_offset_ = phase;
prev_reset_ = reset;
```

Captures the current phase at the moment of a rising edge, enabling mid-beat resets. The StateMachine uses the same pattern for its reset input.

### Condition evaluation (Gate, Logic)

The Gate operator passes or blocks a signal based on a threshold. The Logic operator computes AND/OR/XOR/NOT/NAND/NOR on boolean-ized inputs. These can feed the StateMachine's trigger input for conditional transitions.

## Design: The StateMachine Operator

### Identity

```
Name:   StateMachine
Domain: VIVID_DOMAIN_CONTROL
Time-dependent: false (driven by beat_phase input, not wall clock)
```

### Parameters

| # | Name | Type | Default | Range | Notes |
|---|------|------|---------|-------|-------|
| 0 | `states` | int | 4 | 1–8 | Number of active states |
| 1 | `transition` | int (enum) | 0 | 0–2 | 0 = sequential, 1 = manual, 2 = threshold |
| 2 | `quantize` | bool | true | | Defer transitions to next bar boundary |
| 3 | `loop` | bool | true | | Loop back to state 0 after last state |
| 4 | `bars_per_beat` | int | 4 | 1–32 | Beats per bar (how many phase wraps = 1 bar) |
| 5 | `threshold` | float | 0.5 | 0.0–1.0 | Crossing threshold (transition mode 2 only) |
| 6–13 | `dur_0` .. `dur_7` | float | 4.0 | 0.0–256.0 | Duration of each state in bars (0 = infinite / manual hold) |

**Total: 14 parameters.**

The `transition` enum labels: `{"sequential", "manual", "threshold"}`.

Duration of 0 means the state holds indefinitely until an external trigger advances it, regardless of transition mode. This allows mixing timed and manual states in sequential mode (e.g., an 8-bar intro that auto-advances, then a verse that waits for a manual cue).

### Ports

**Inputs** (indexed in input_values order):

| # | Name | Type | Purpose |
|---|------|------|---------|
| 0 | `beat_phase` | float | Clock's beat phase (0–1 sawtooth) |
| 1 | `trigger` | float | Manual advance (rising edge → next state) |
| 2 | `reset` | float | Rising edge → return to state 0 |
| 3 | `signal` | float | Signal for threshold mode |

**Outputs** (indexed in output_values order):

| # | Name | Type | Purpose |
|---|------|------|---------|
| 0 | `state` | float | Current state index (0–7) as float |
| 1 | `progress` | float | Progress through current state (0.0–1.0) |
| 2 | `trigger` | float | 1.0 on the frame a state transition fires, 0.0 otherwise |
| 3 | `bar` | float | Current bar within the current state (0-indexed) |
| 4 | `beat` | float | Current beat phase within the current bar (0.0–1.0) |

### State Variables

```cpp
int    current_state_  = 0;
int    bar_count_       = 0;       // bars elapsed in current state
int    beat_count_      = 0;       // beats elapsed in current state
float  prev_phase_      = 0.0f;    // for phase-wrap detection
bool   prev_trigger_    = false;   // for rising-edge detection
bool   prev_reset_      = false;   // for rising-edge detection
float  prev_signal_     = 0.0f;    // for threshold crossing detection
bool   pending_advance_ = false;   // quantized transition waiting for bar boundary
bool   finished_        = false;   // true when non-looping sequence ends
```

### process() Logic

```
1. Read inputs: beat_phase, trigger, reset, signal
2. Read params: states, transition mode, quantize, loop, bars_per_beat, threshold, dur_0..dur_7

3. RESET DETECTION
   if (reset rising edge):
       current_state_ = 0
       bar_count_ = 0
       beat_count_ = 0
       pending_advance_ = false
       finished_ = false
       prev_phase_ = beat_phase
       → skip to output

4. BEAT COUNTING
   delta = beat_phase - prev_phase_
   if (delta < -0.5):           // phase wrapped → one beat completed
       beat_count_++
   prev_phase_ = beat_phase

   BAR COUNTING
   new_bar = false
   while (beat_count_ >= bars_per_beat):
       beat_count_ -= bars_per_beat
       bar_count_++
       new_bar = true

5. ADVANCE DECISION
   should_advance = false

   if (finished_):
       → skip to output

   switch (transition mode):
     case sequential:
         dur = dur_[current_state_]
         if (dur > 0 and bar_count_ >= dur):
             should_advance = true

     case manual:
         if (trigger rising edge):
             should_advance = true

     case threshold:
         crossed = (prev_signal_ < threshold and signal >= threshold)
                or (prev_signal_ >= threshold and signal < threshold)
         if (crossed):
             should_advance = true
         prev_signal_ = signal

   // Duration-0 override: even in sequential mode, allow manual trigger to advance
   if (dur_[current_state_] == 0 and trigger rising edge):
       should_advance = true

6. QUANTIZATION
   if (should_advance and quantize and not new_bar):
       pending_advance_ = true
       should_advance = false

   if (pending_advance_ and new_bar):
       pending_advance_ = false
       should_advance = true

7. STATE TRANSITION
   transition_fired = false
   if (should_advance):
       next = current_state_ + 1
       if (next >= states):
           if (loop):
               next = 0
           else:
               finished_ = true
               next = current_state_    // stay on last state
       if (next != current_state_):
           current_state_ = next
           bar_count_ = 0
           beat_count_ = 0
           transition_fired = true

8. COMPUTE OUTPUTS
   state_out = current_state_
   bar_out = bar_count_

   dur = dur_[current_state_]
   if (dur > 0):
       // progress = (completed bars + fractional bar) / total bars
       fractional_bar = beat_count_ / bars_per_beat + beat_phase / bars_per_beat
       progress_out = clamp((bar_count_ + fractional_bar) / dur, 0.0, 1.0)
   else:
       progress_out = 0.0      // infinite state has no meaningful progress

   beat_out = beat_phase        // pass through (phase within current beat)
   trigger_out = transition_fired ? 1.0 : 0.0

9. WRITE OUTPUTS
   output_values[0] = state_out
   output_values[1] = progress_out
   output_values[2] = trigger_out
   output_values[3] = bar_out
   output_values[4] = beat_out
```

### Registration

```cpp
struct StateMachine : vivid::OperatorBase {
    static constexpr const char* kName   = "StateMachine";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   states{"states", 4, 1, 8};
    vivid::Param<int>   transition{"transition", 0, {"sequential", "manual", "threshold"}};
    vivid::Param<bool>  quantize{"quantize", true};
    vivid::Param<bool>  loop{"loop", true};
    vivid::Param<int>   bars_per_beat{"bars_per_beat", 4, 1, 32};
    vivid::Param<float> threshold{"threshold", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> dur_0{"dur_0", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_1{"dur_1", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_2{"dur_2", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_3{"dur_3", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_4{"dur_4", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_5{"dur_5", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_6{"dur_6", 4.0f, 0.0f, 256.0f};
    vivid::Param<float> dur_7{"dur_7", 4.0f, 0.0f, 256.0f};

    // ... state variables, collect_params, collect_ports, process ...
};

VIVID_REGISTER(StateMachine)
```

## The Subgraph Vision

The long-term goal is that each state owns a subgraph — a self-contained patch fragment that gets activated on entry and deactivated on exit. A song's "chorus" state would contain its own NotePattern, DrumSequencer, and GPU operators, all wired up independently from the "verse" state's subgraph.

This is the mechanism that makes state machines truly compositional rather than just switching parameter values.

### What's needed

Subgraphs don't exist yet. The infrastructure requires:

1. **Subgraph container** — a way to embed a group of nodes inside a parent node, with the parent controlling their lifecycle (process only when active).
2. **Activation/deactivation** — when a state becomes active, its subgraph's operators begin processing; when inactive, they stop (and optionally reset).
3. **Cross-graph routing** — inputs to the StateMachine node flow into the active subgraph; the subgraph's outputs flow out. Inactive subgraphs produce silence/zero/last-value.
4. **Crossfade transitions** — during a transition window, both the outgoing and incoming subgraphs process simultaneously, with their outputs blended.
5. **Session serialization** — subgraph contents must save/load with the session.

### How the operator design accommodates this

The current design is deliberately subgraph-ready without requiring subgraphs:

- **State index output** — downstream operators can use this today to select behavior (e.g., a Sequencer could be duplicated per section, each gated by a comparison on state index). When subgraphs arrive, the state index drives which subgraph is active instead.
- **Progress output** — usable today for parameter automation crossfades. With subgraphs, it drives the blend factor between outgoing/incoming subgraphs.
- **Trigger output** — fires on transitions. Today it can reset envelopes or one-shot effects. With subgraphs, it triggers activation/deactivation.
- **Duration model** — bar-based durations map directly to "run this subgraph for N bars."

No changes to the StateMachine operator itself will be needed when subgraphs are implemented — only the runtime infrastructure around it.

## Legacy Reference

The `legacy` branch had a `Song` class (`modules/vivid-audio/include/vivid/audio/song.h`, `modules/vivid-audio/src/song.cpp`) that addressed the same use case with a different architecture.

### Concepts worth carrying forward

- **Section progress (0–1)** — the `sectionProgress()` output that gives normalized progress through the current section. Directly maps to our `progress` output.
- **Edge detection flags** — `sectionJustStarted()` and `barJustStarted()` for firing one-shot events at boundaries. Maps to our `trigger` output.
- **Bar/beat tracking** — `currentBar()` and `currentBeat()` within the current section. Maps to our `bar` and `beat` outputs.
- **Bar-based section durations** — sections defined by start/end bar numbers, which is equivalent to our per-state `dur_N` in bars.

### What doesn't carry over

- **Direct operator pointers** — `Song` held a `Clock*` obtained by name lookup. The v2 architecture uses port connections instead; the StateMachine receives `beat_phase` through a normal input port.
- **String-based section names** — sections were identified by name (`"intro"`, `"chorus"`). The StateMachine uses integer indices (0–7). Names can be a UI/metadata concern later, but the runtime operates on indices.
- **Programmatic C++ API** — `song.addSection(...)`, `song.jumpToSection(...)`. The StateMachine is configured entirely through parameters and driven through input ports, like every other v2 operator.
- **Repeat counts** — `Section::repeatCount` allowed a section to repeat N times or loop forever. The StateMachine handles this differently: set a longer duration, or use `loop` mode. If per-state repeat counts prove necessary, they can be added later as additional parameters.
- **Callback system** — `onSectionChange(callback)` fired a C++ callback. The v2 equivalent is the `trigger` output port, which downstream operators react to through normal graph connections.

## Integration Patterns

How the StateMachine works with existing operators today, without subgraphs.

### State index driving parameter automation

Connect the `state` output to a Sequencer's `phase` input (after scaling). With 4 states, state index 0–3 maps to phase 0.0–0.75, selecting different sequencer steps. Each step holds a parameter value for that state.

```
Clock.beat_phase → StateMachine.beat_phase
StateMachine.state → [Math: / states] → Sequencer.phase
Sequencer.value → [target parameter]
```

This gives per-state parameter snapshots using only existing operators.

### Section progress as a crossfade ramp

The `progress` output ramps 0–1 through each state. Connect it to a Gain operator's level to create a fade-in at the start of each section, or invert it for a fade-out at the end.

```
StateMachine.progress → Gain.level
AudioSource → Gain.input
```

For crossfading between two sources, use the progress output and its complement (1 - progress via a Math operator) to drive two Gain operators.

### Triggers firing one-shot events

The `trigger` output fires 1.0 on the frame a transition occurs. Connect it to an Envelope's gate input to fire a percussive hit at every section boundary, or to a sample player's trigger to play a transition sound effect.

```
StateMachine.trigger → Envelope.gate
```

### Conditional transitions with Logic

For installation scenarios, use a threshold-mode StateMachine with sensor input:

```
SensorInput → StateMachine.signal
```

Or combine multiple conditions using Logic operators feeding the manual trigger:

```
SensorA → Logic.a
SensorB → Logic.b
Logic.result → StateMachine.trigger
```

## Incremental Build Path

### Step 1: StateMachine operator (buildable now)

The operator as described in this document. It's a metadata emitter — it counts bars, tracks state, and outputs control signals. No new infrastructure required. Uses the same patterns as Envelope (state enum, edge detection), Sequencer (phase-to-step mapping), and NotePattern (beat counting from phase wraps).

**Files:**
- Create: `operators/control/state_machine/state_machine.cpp`
- Modify: `CMakeLists.txt` (add build target in control section)

### Step 2: Per-state parameter snapshots (Phase 19 dependency)

Once session/snapshot infrastructure exists (Phase 19), enable saving and recalling parameter snapshots per state. When the StateMachine transitions to state N, it recalls snapshot N, setting parameters across the graph to their stored values. This gives full per-state configuration without subgraphs.

**Depends on:** Phase 19 (Sessions & Snapshots)

### Step 3: Per-state subgraphs (deferred)

The full vision: each state owns a subgraph that activates/deactivates on transitions. This requires the subgraph container system described in "The Subgraph Vision" above. It's a significant architectural addition that affects the node graph runtime, serialization, and UI.

**Depends on:** Subgraph infrastructure (not yet on the roadmap as a numbered phase)

## Verification

1. Add StateMachine to graph, connect Clock.beat_phase → StateMachine.beat_phase
2. Set 4 states, sequential mode, 4 bars each, loop on — state output cycles 0 → 1 → 2 → 3 → 0
3. Progress output ramps 0.0–1.0 within each state, resets on transition
4. Trigger output fires 1.0 on the frame each transition occurs
5. Bar output counts 0, 1, 2, 3 within each 4-bar state
6. Set quantize off, switch to manual mode — trigger input immediately advances state
7. Set quantize on, manual mode — trigger input defers advance to next bar boundary
8. Set a state's duration to 0 — that state holds until manual trigger, even in sequential mode
9. Set loop off — sequence stops on last state, progress stays at 1.0, no further transitions
10. Reset input returns to state 0 from any state, clears bar/beat counters
11. Threshold mode with signal input — state advances when signal crosses threshold value
