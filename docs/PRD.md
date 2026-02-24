# Vivid — Product Requirements Document

## 1. Vision

In the entire creative coding ecosystem, audio is structurally second-class. TouchDesigner's CHOPs exist, but nobody calls it an audio authoring environment. openFrameworks' ofSoundPlayer is a thin wrapper. Unity and Unreal treat audio as playback, not a creative medium. The result: in practice, the visual team builds the installation and then someone drops in a soundtrack. The audio doesn't *respond* to the same data the visuals respond to.

Meanwhile, in music production, Bitwig Studio has shown what happens when audio is treated as a medium you sculpt in real time rather than a timeline you arrange. Its modulation system — where any signal can modulate any parameter with visible feedback directly on the control — demonstrates that cross-domain routing can be immediate, visual, and explorable. Vivid takes this philosophy and extends it across the audio-visual boundary.

Vivid is the first general-purpose creative coding platform where audio and visuals are equal peers — authored in the same graph, driven by the same data, with the same level of expressive control. It combines the inspect-anywhere philosophy of TouchDesigner, the extensibility of openFrameworks, the modulation routing of Bitwig, the inline output of Jupyter notebooks, and the immediacy of Strudel — but with plain C++ that LLMs can read and write.

> **Vivid's core innovation:** an LLM that populates exploration spaces, not just writes code. The user never stares at a blank canvas. They describe a direction, the LLM generates a field of possibilities, and the user navigates that field through direct manipulation.

### 1.1 What Vivid Is For

- Live audiovisual performances
- Museum and gallery installations
- Branded interactive experiences
- Interactive art and generative art
- Creative prototyping and experimentation

### 1.2 What Vivid Is Not For

- Game engines (use Unity, Unreal, Godot)
- Traditional desktop applications
- Web applications
- Film/video post-production (linear composition tools)

**The scope boundary:** Vivid is for real-time, interactive audiovisual experiences. If the output is a window (or multiple windows, or a projector, or an LED wall) showing real-time graphics with real-time audio, and the system is reactive to some input (performer, sensor, data, time), Vivid is the right tool.

---

## 2. Core Principles

Ordered by importance. Principles 1–4 define *what Vivid is*. Principles 5–8 define *how it works*. Principles 9–11 define *how it's built*.

### 2.1 Audio-Visual Parity

The thesis. Why Vivid exists. Audio-visual parity means three things simultaneously:

- **Equal ease of creation:** a drum machine should be as easy to create as a particle system. The interface, documentation, and operator library treat both domains as first-class.
- **Equal breadth of options:** for any visual technique the creator wants to explore (feedback, particle systems, shaders, compositing), there should be an equally rich set of audio tools (synthesis, sequencing, effects, analysis) available in the same environment.
- **Easy cross-domain interaction:** this is the one existing tools miss entirely. When you create an interactive installation that responds to a camera tracking people, the audio and visual responses should be authored in the same graph, driven by the same data, with connections between domains as easy to make as connections within a single domain.

The cross-domain interaction point is critical. TouchDesigner has CHOPs and TOPs, but connecting audio analysis to visual parameters requires manual bridging between separate operator families in separate editor panes. Max/MSP separates audio (MSP) from video (Jitter) into different namespaces. In Vivid, audio, visual, and control operators coexist in a single unified graph. A wire from an FFT analysis node to a particle system's size parameter is the same gesture as a wire between two visual effects.

### 2.2 Temporal Plurality

**Vivid has no master timeline. It has clocks.** Clocks are temporal contexts that can be driven by anything — wall time, musical tempo, sensor data, spectral energy, walking speed, each other. Patterns, state machines, and animations all bind to clocks, not to "the" time.

A single master timeline implies an experience has a beginning, a duration, and an end. An installation running 12 hours a day in a museum lobby has no beginning. It has states, cycles, reactions, and rhythms, but no timeline. Forcing it onto one is like putting a river on a ruler.

This is the architectural foundation that makes Vivid equally suited for performances (where one clock happens to be musical tempo) and installations (where there's no privileged clock and everything is reactive or cyclical).

### 2.3 General-Purpose Positioning

Vivid is a general-purpose creative coding platform — not just a performance tool. The A/V parity thesis isn't about live AV artists alone; it's about the entire creative coding ecosystem where audio is structurally second-class across installations, branded experiences, museum exhibits, and any context where visuals and sound should respond to the same data.

### 2.4 Experimentation First

Most creative coding time is spent *trying things* — not performing and not installing. Vivid's interface treats experimentation as the primary mode of use, not a stepping stone to a finished piece. The distance between an idea and seeing/hearing its result should be as short as possible. The framework's interface is designed around the experimental loop of tweak → perceive → tweak, with both audio and visual feedback simultaneous and immediate.

### 2.5 Text Is the Source of Truth

The canonical representation of a Vivid project is text — diffable, versionable, LLM-readable. The visual graph is a view of that text, not a replacement for it.

### 2.6 See Every Step

Every point in the processing chain is inspectable. A creator debugging an installation in the studio sees the same chain-health information as anyone working with the system in real time. The LLM and developer get structured data (JSON) for the same state.

### 2.7 Hot Reload Everything

Changes to parameters and routing propagate within the same frame. Changes to operator implementations recompile and hot-swap within 1–3 seconds. The system never requires a restart. Non-negotiable.

### 2.8 LLM-Native Workflow

Every layer of Vivid's architecture is designed for LLM interaction: the routing graph is serializable JSON the LLM can read and write, the operator API is constrained enough for confident generation, and the experimentation interfaces are designed to be populated by the LLM, not just built by the user.

### 2.9 Keep the Core Minimal

The Vivid runtime is small. Complexity lives in addons and operator libraries. The core handles graph execution, domain bridging, and the operator plugin interface.

### 2.10 Creator Tools vs Developer Tools

Built-in UI handles what external tools can't: real-time GPU/audio feedback, parameter manipulation, connection routing, live preview. External tools handle what Vivid shouldn't rebuild: code editing, file management, version control. Decision framework: "Would a creator need it during a live session, while prototyping in the studio, or while running an installation?"

Operator code editing happens in the user's native IDE (VS Code, CLion, etc.), not inside Vivid. Vivid provides scaffolding — right-click in the graph to create a new operator, and Vivid generates the boilerplate C++ file with the correct base class and opens it in the external editor. On save, Vivid detects the change and hot-reloads the operator into the palette.

### 2.11 Don't Reinvent the Wheel

The creative coding ecosystem has 20 years of solved problems. Vivid leverages existing tools aggressively. The value is in the integration and the experimentation layer, not in rebuilding synthesizers, shader libraries, or UI frameworks from scratch.

---

## 3. Experimentation & Interface Design

### 3.1 What Experimentation Actually Requires

#### The Perception-Action Loop Must Be Under 50ms
The threshold between "I changed something and I see/hear the result" and "I'm waiting for something to happen." Parameter tweaks, connection changes, and preset switches must be instant. Kernel/shader recompilation is the only acceptable source of latency (50–200ms for kernel swaps, 1–3s for full operator hot-reload).

#### Combinatorial Discovery Requires a Palette
Experimentation isn't just tweaking parameters — it's combining different building blocks. The palette of available operators, connections, and configurations must be easy to browse, easy to populate (both by user and LLM), and easy to extend.

#### Happy Accidents Require Branching
When something unexpected and good happens, there must be a low-cost way to save the current state, continue exploring, and return to it later. Undo/redo is not enough — it's linear. What's needed is branching: "save this as a variation, keep going."

#### Visible Options Produce More Exploration
When alternatives are visible (a grid of visual variations, a set of connection configurations), users explore more combinations than when they must imagine alternatives. The interface should make options spatial, not just sequential.

#### Randomization and Mutation Are Legitimate
"Try something different" is a valid creative operation. Randomizing parameters within intelligent ranges, mutating existing configurations, and LLM-generated "what if" variations are all part of the exploration toolkit.

### 3.2 The Six Experimentation Interfaces

Each interface below is a lens on the same underlying patch — different views of the same data, optimized for different exploration modes.

#### The Node Graph

The graph is the canonical structural view: operators as nodes, connections as wires, parameters as values on nodes. This is how the user understands what exists and how it's connected. It's the foundation, but not the primary experimentation tool — it's too low-level for rapid exploration.

Discovery potential: low (structural, not parametric). Latency: instant for connections, 50–200ms for adding operators.

#### The Patchbay / Connection Matrix

A matrix where rows are outputs and columns are inputs (or vice versa). Each intersection is a potential connection. Clicking an intersection creates the connection; the intersection cell can hold a mapping curve, scaling factor, or modulation amount. This is how the user discovers unexpected audio→visual mappings.

The patchbay operates on operators that already exist in the graph. It is for rapidly exploring how they interact, not for creating new operators. The LLM populates the matrix with configurations — "here are 8 different mapping setups ranging from subtle to aggressive."

Interaction model takes inspiration from Bitwig's modulation routing, where any modulator can be dragged onto any parameter and the modulation range is displayed as a visual overlay directly on the target control. In Vivid, this extends across the audio-visual boundary: when a patchbay intersection is active, the target parameter's control shows the modulation range and current value in real time.

Discovery potential: very high (N×M possible connections). Latency: instant.

#### The Session / Variation Grid

A grid where columns are different configurations of the same structural patch, and rows might represent audio variations, visual variations, or complete A/V states. The user can click through columns to audition, drag to reorder, and ask the LLM to "fill this row with 8 variations of the particle behavior." Think Ableton Session View but for audiovisual configurations rather than clip launching.

Discovery potential: very high. Latency: instant for parameter snapshots, 50–200ms for kernel swaps.

#### The Live REPL

A command input that executes graph mutations against the running state. Each command modifies the live output: adding nodes, changing connections, setting parameter values. Previous commands are visible as toggleable history. The TidalCycles and Hydra communities demonstrate deeply exploratory text-based live coding. The LLM can suggest next commands based on REPL history. The REPL operates on the same JSON graph representation that everything else uses.

Discovery potential: high. Latency: instant for parameter changes, 50–200ms for structural changes.

#### The Parameter Space Explorer

Every exposed parameter as a manipulable control (slider, knob, XY pad, color picker). Parameters can be grouped, MIDI-mapped, randomized, and interpolated between saved presets. Once a structure exists, this finds the sweet spot. The LLM annotates parameters with semantic meaning and suggests values based on intent ("make it feel tense and dark"). The innovation: controls span both audio and visual domains simultaneously.

Discovery potential: moderate (high for refinement). Latency: instant.

#### The Pattern Algebra

Time-varying behavior through composable pattern expressions (TidalCycles model). Patterns are first-class: nested, combined, transformed, and applied to any parameter. Combining two simple patterns often produces complex, musically interesting polyrhythms. Pattern expressions are extremely LLM-friendly: short, text-based, compositional. No existing tool applies pattern algebra to audiovisual parameter mapping.

Discovery potential: very high. Latency: instant (interpreted scheduling instructions).

#### The State Machine

Macro-level structure as named states (intro, build, drop, ambient, climax) with transition conditions. Each state has its own parameter configuration, active modules, and pattern set. Transitions can be crossfades, hard cuts, or morphs. State machines are among the most LLM-friendly structures — small, discrete, easy to reason about.

Discovery potential: moderate (high for composition). Latency: 50–200ms for kernel swaps.

### 3.3 The Audio/Visual Exploration Asymmetry

A critical design constraint: the preceding interfaces implicitly treat audio and visual exploration as if they have the same feedback characteristics. They do not.

Visual exploration is spatial and instantaneous — you can glance at a grid of 8 visual variations and evaluate all of them in under a second. Audio exploration is temporal and sequential — you cannot "glance" at a sound. You have to listen to it unfold over at least a bar or two. The evaluation bandwidth asymmetry is roughly 65×: visual permits ~8 options per second; audio permits ~0.12 options per second (~8 seconds per variation).

When exploring audio and visual together, the audio's temporal constraint dominates. Visual exploration gets slowed to audio speed, wasting the spatial advantage that makes visual experimentation so productive.

### 3.4 The Tiered Exploration Model

The resolution is not to treat audio and visual exploration identically, but to design for their different speeds. Crucially, the interface does not impose an order. A creator might start with audio and explore visuals against it, start with visuals and add audio later, or iterate on both from rough drafts. The strategies below are what the interface supports, not steps it enforces.

#### Strategy: Anchor One Domain, Explore the Other

The most productive cross-domain exploration comes from holding one domain stable while freely exploring the other. Which domain to anchor is the creator's choice:

- **Audio-first:** select or compose an audio element, then rapidly explore visual responses against it. The audio loops; visual changes are instant. The patchbay and session grid are at their most powerful here.
- **Visual-first:** build a visual scene, then explore how different audio elements interact with it. This is common for installations where the visual identity is primary and audio is a reactive layer.

The interface assists whichever domain is being explored: when exploring audio, it offers rich metadata, short-loop auditioning, and A/B comparison to compensate for audio's slower evaluation. When exploring visuals, it offers spatial grids, instant switching, and LLM-generated mapping variations.

#### Strategy: Coupled Refinement

Fine-tuning the relationship between audio and visual once both are roughly in place. This operates at audio speed (evaluation per bar/phrase) and is the most LLM-assisted phase: the LLM observes the current state and suggests adjustments, the user saves successful coupled configurations to the session grid as complete A/V states.

#### Audio Exploration Aids

Because audio evaluates slower than visual, the interface provides tools to narrow the gap:

- **LLM annotations:** each audio variation tagged with semantic descriptions ("more syncopated," "darker harmonic quality"). The user scans descriptions first, auditions only promising ones.
- **Visual audio previews:** spectrograms, rhythm grids, harmonic maps — giving audio some of visual's spatial evaluation quality.
- **Short-loop auditioning:** 2-beat excerpts rather than full 4-bar phrases. Cuts audition time by 75%.
- **Comparative playback:** A/B toggle switching between variations on the beat boundary.

### 3.5 LLM Role by Exploration Strategy

| Strategy | LLM Role | Example |
|----------|----------|---------|
| Audio Exploration | Annotator + pre-filter | "Variation B has a deeper swing feel with a Neapolitan chord in bar 3." |
| Visual Exploration | Variation generator | "Here are 6 mapping configurations ranging from minimal to heavy audio-reactivity." |
| Coupled Refinement | Critic + advisor | "Particle density peaks are lagging the beat by ~100ms. Try routing onset detection with 0 smoothing." |
