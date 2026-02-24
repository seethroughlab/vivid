# Roadmap

Each phase is independently useful and shippable.

## Phase 1: Routing Graph + Parameter Explorer + LLM Scaffolding

Define the JSON graph format. Build the runtime that interprets it. Expose all parameters as manipulable controls. Let the LLM generate graph descriptions from natural language. Minimum viable experimentation loop: LLM creates a structure, user tweaks parameters, sees results instantly. Computation kernels are C++ compiled via Zig.

**Phase 1 concrete deliverable:** a native window containing a node graph with at least 6 operators spanning all three domains (e.g., Noise, Blur, Oscillator, Delay, LFO, Clock). Live GPU texture thumbnails on every GPU node. Audio output. An inspector panel with working sliders for all parameters. The graph is loaded from a JSON file and can be modified via the REPL or by editing the JSON directly. Operators can be scaffolded and hot-reloaded from an external IDE. Spreads work for at least one case (e.g., FFT bins driving visual element sizes). A built-in chat panel connects to the Anthropic API for creative exploration. An MCP server exposes the Runtime API so Claude Code can inspect, modify, and scaffold from the terminal.

## Phase 2: Patchbay + Session Grid

Add the connection matrix for audio→visual mapping (highest-discovery interface, most unique to Vivid's identity). Add the session grid for variation management. LLM generates connection matrix variations and session grid populations. This is where experimentation starts to feel qualitatively different from existing tools.

## Phase 3: REPL + Pattern Algebra + WebSocket API

Add the live REPL for direct graph manipulation. Introduce pattern algebra as composable pattern operators (not a DSL — pattern scheduling is implemented as Control operators). Build the WebSocket API for external process integration, enabling scripting in any language the user prefers without introducing a language dependency into the core.

## Phase 4: State Machines + External Integration

Add the state machine for macro structure. Refine the WebSocket API for production use cases (remote control, external show control systems, OSC/MIDI bridge applications). This is the complete platform.

**The guiding principle:** the LLM populates the exploration space; the user navigates it. Every architectural decision should be evaluated against this principle. If a decision makes it harder for the LLM to generate options, or harder for the user to evaluate and combine them in real-time, it's the wrong decision.

---

## Open Questions

### Cross-Domain Connection UX
When a user drags a wire between nodes in different domains, should the system block and offer to insert a bridge node (explicit), auto-insert a visually distinct bridge node (semi-explicit), or silently handle bridging (implicit)? To be resolved during prototyping.

### Semantic Tag Depth
How many semantic tags to define initially, and whether to formalize a standard set or let it grow organically as operators are built.

### GPU Operator Model
Whether GPU operators are primarily C++ host code dispatching compute shaders / WGSL, or C++ all the way down. Affects the operator API and what Zig compiles.

### Graph Serialization Format
The declarative graph representation enabling LLM-driven patching. Needs to capture node types, connections, parameter values, and semantic tags. This is the single source of truth for the entire system.

### Control Operator Sufficiency
The decision to handle all automation and logic as visible Control operators (no scripting layer) requires a rich enough set of built-in control operators. What is the minimum viable set? LFO, Clock, Sequencer, Pattern, Envelope, Math, Logic, Gate, Random, Smooth/Lerp. Are there common automation patterns that are awkward to express as node graphs?

### WebSocket API Scope
What mutations should the WebSocket API support? Parameter changes only, or full graph topology changes (adding/removing nodes, connections)? How to handle conflicts between WebSocket mutations and direct UI manipulation?

### Audio/Visual Session Grid Interaction
The exact UX for the exploration strategies — how audio columns, visual columns, and mapping columns behave differently within the same grid widget.

---

## To Be Determined

The following topics are acknowledged as important but are not yet designed. They will be addressed as the project progresses, most during or after Phase 1 implementation.

### Subpatches
Equivalent to TouchDesigner's Bases or Max/MSP's subpatchers. A subpatch collapses a group of operators into a single node with defined inputs and outputs. Essential for managing graph complexity, enabling operator reuse, and supporting LLM scaffolding (the LLM generates a subpatch, not a flat graph). The design depends on how Spreads and Simulation Zones interact with encapsulation boundaries. A subpatch may also serve as the unit of session grid variation — swapping a cell swaps a subpatch.

### Project File Format
Is a Vivid project a single .json file, or a directory containing the graph JSON, operator source files, assets (textures, audio samples), and assertion definitions? How does save/load work? How are assets referenced — absolute paths, relative paths, or embedded?

### Performance Targets
60fps at what node count? Audio at what buffer size / sample rate? Maximum acceptable hot-reload time? GPU memory budget? These become acceptance criteria for Phase 1.

### Error Handling and Recovery
What happens when an operator's hot-reload fails to compile? When an operator segfaults? When an audio operator misses its deadline? When a GPU shader fails validation? The graph must keep running. Strategies: per-operator error isolation, fallback to last-known-good, visual error indicators on failed nodes, audio silence on missed deadlines.

### Spread Visual Representation
How do Spreads appear in the graph? Wire thickness proportional to cardinality? A small badge showing count? Color intensity? How does the user know they're looking at a Spread of 512 vs. a Spread of 1? This is a UX design problem to be resolved through prototyping.

### Simulation Zone Visual Representation
Whether Simulation Zones are a visible bounding box around grouped nodes (like Blender) or a single Feedback operator with an internal graph (like a subpatch). The former is more explicit and inspectable; the latter is simpler for the JSON schema.

### Accessibility
Keyboard navigation, screen reader support, high-contrast mode. Important but not blocking Phase 1.

### Multi-Window / Multi-Monitor
Output preview undocking to a separate window for projector/LED wall output. Multiple graph views. Phase 2 concern.

### Library Version Pinning
Should the project record which library versions were used? A lockfile (vivid-lock.json) that pins awesome-particles@0.2.0 would ensure reproducibility, especially for installations that need to rebuild months later. Alternatively, the project could simply vendor library source into its own directory. The tradeoff is reproducibility vs. simplicity.
