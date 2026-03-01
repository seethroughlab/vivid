# Open Questions & To Be Determined

Design questions and undecided topics acknowledged as important but not yet resolved. These will be addressed as the project progresses, most during or after early phase implementation.

---

## Open Questions

### Cross-Domain Connection UX

When a user drags a wire between nodes in different domains, should the system block and offer to insert a bridge node (explicit), auto-insert a visually distinct bridge node (semi-explicit), or silently handle bridging (implicit)? To be resolved during prototyping.

### Semantic Tag Depth

How many semantic tags to define initially, and whether to formalize a standard set or let it grow organically as operators are built.

### WebSocket API Scope

What mutations should the WebSocket API support? Parameter changes only, or full graph topology changes (adding/removing nodes, connections)? How to handle conflicts between WebSocket mutations and direct UI manipulation?

### Audio/Visual Session Grid Interaction

The exact UX for the exploration strategies — how audio columns, visual columns, and mapping columns behave differently within the same grid widget.

---

## To Be Determined

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

### Bundled Compiler

Optionally shipping a C++ compiler (e.g., Zig's `zig c++`) so users don't need Xcode Command Line Tools installed. This is a packaging/distribution problem, not an architecture problem — the hot-reload system already invokes the system compiler as a subprocess. The question is whether the onboarding friction of `xcode-select --install` justifies bundling a ~40MB compiler binary.

### OSC Input Operator

An OSC (Open Sound Control) input operator following the same pattern as MIDI Input — a control-domain operator that listens on a configurable UDP port and maps incoming OSC addresses to output ports. Deferred because MIDI is more immediately useful for the target creative workflow, but the architecture is identical.
