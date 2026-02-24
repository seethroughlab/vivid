# Interface Architecture

Section 3 of the PRD describes *what* the experimentation interfaces are. This document describes *how* they are built — the technology, rendering model, toolkit, layout, and thumbnail strategy.

## 6.1 GUI Technology: Native Rendering

**Decision: The interface runs natively in the same GPU context as the Vivid runtime.** This is constrained by a single non-negotiable requirement: the "See Every Step" principle demands live GPU texture thumbnails for every node in the chain, potentially 20+ simultaneously at frame rate.

A web-based interface (React/Svelte + WebSocket) was eliminated because GPU→CPU readback, encoding, and transport don't scale to 20+ thumbnails at 30fps. A hybrid approach using Chromium Embedded Framework was eliminated after direct implementation experience — texture sharing between wgpu-native's GPU context and Chromium's compositor proved unworkable, and the module added ~200MB of dependency for a fragile integration.

Native rendering gives zero-copy texture thumbnails (every intermediate texture is a handle that can be blitted directly), no process boundary, no IPC overhead, and sub-frame latency for parameter changes. The tradeoff is slower UI iteration compared to web technologies.

## 6.2 Rendering Mode: Retained

**Decision: Retained-mode UI, not immediate mode.** In immediate mode (Dear ImGui), the application redraws the entire UI every frame with no persistent widget objects. In retained mode, widgets are objects that persist between frames and manage their own state: a slider knows it's being dragged, a panel knows which child has focus, a list knows its scroll position.

Vivid's experimentation interfaces are inherently stateful — a patchbay intersection remembers its mapping curve, a session grid cell knows its variation and playback state, a parameter knob tracks its MIDI mapping and drag state. Retained mode handles this naturally. Immediate mode would require maintaining all interaction state in parallel data structures, manually synchronized with draw calls every frame.

## 6.3 Toolkit: Custom Purpose-Built Widgets

**Decision: Build a purpose-built retained-mode widget set directly on the existing wgpu rendering context.** Not a general-purpose UI framework — just the 10–15 widget types Vivid's experimentation interfaces actually need.

Alternatives evaluated and rejected: **Dear ImGui** (already in the repo, good for prototyping, but immediate-mode and limited aesthetic ceiling), **Qt Quick/QML** (mature but ~100MB+ dependency, GPL licensing complexity, two GPU contexts to coordinate), **Slint** (modern but young ecosystem with unproven custom texture integration).

The custom approach gives zero-copy texture thumbnails trivially (same GPU context), total control over look and interaction, no external dependencies, and purpose-built widgets the LLM can generate. The scope is bounded: rows, columns, fixed/flex sizing, scroll containers, and absolute positioning for the node graph.

**Required widget set:**
- Core: Panel, Button, Slider, Knob, Dropdown, TextInput, Toggle
- Specialized: NodeGraph, PatchbayMatrix, SessionGrid, TexturePreview, Waveform/Meter

## 6.4 Application Layout

**Decision: Output preview pinned right, tabbed workspace center-left, context-sensitive inspector below, transport strip at bottom, collapsible chat/REPL.** This is the default fixed layout; the output preview can undock to a separate window for multi-monitor setups.

The visibility hierarchy driving this layout:

- **Always visible:** output preview (the perception-action loop), active parameters (context-sensitive to selection), transport/clock.
- **Primary workspace (one at a time, instant switching):** node graph, patchbay matrix, session grid. These are different lenses on the same patch. Switching feels like changing a view, not navigating to a different screen.
- **On-demand (collapsible):** LLM chat, live REPL, pattern editor, state machine editor. Brought up when needed, don't consume space during direct manipulation.
- **External:** operator code editing happens in the user's IDE, not inside Vivid.

The main workspace tabs are the key interaction pattern: the node graph builds structure (add nodes, connect, see topology), the patchbay maps cross-domain relationships, and the session grid manages variations. Three primary lenses on the same underlying data.

## 6.5 Node Thumbnails

**Decision: Always-on small thumbnails, with on-hover fallback for large graphs.** Every node in the graph displays a live texture thumbnail at all times, matching the existing Vivid chain visualizer and TouchDesigner's behavior. This directly serves the "See Every Step" principle — maximum inspectability. If GPU cost becomes a problem at high node counts (20+), a user toggle switches to on-hover mode where nodes are compact by default and expand on selection.

## 6.6 Visual Style

**Aesthetic: dark steel with colored accents.** Vivid's interface is a professional tool, not a consumer application. The visual language draws from hardware audio equipment and HUD displays — dark, high-contrast, precise, content-forward. Sharp geometry, monospace type, thin borders. More Elektron Digitakt than Apple Human Interface Guidelines.

### Core Principles

- **Content is the star.** The interface chrome recedes; the live previews, waveforms, and values dominate. Node containers are minimal dark steel rectangles — as invisible as possible so the preview content takes focus.
- **Identity lives in the preview, not the container.** Operators across all three domains share the same container shape (sharp-cornered rectangles, uniform size). A thin accent-color bar at the top and small domain badge are the only container-level indicators. The preview content inside is where domain identity becomes unmistakable.
- **Three-color domain system.** GPU operators use cyan (`#4ECDC4`) for accent color. Audio operators use amber (`#F0A030`). Control operators use light gray (`#C0C8D0`). These colors appear in accent bars, port indicators, wire colors, and inspector highlights. Background and chrome use dark steel grays (`#16191D` background, `#1A1D21` panels, `#22262B` containers, `#2A2E33` borders).
- **Monospace type throughout.** Reinforces the tool aesthetic and ensures values, labels, and code all align cleanly. Sans-serif body text would feel like a website.

### Domain Preview Treatments

- **GPU nodes:** the texture IS the preview. A full-bleed live thumbnail fills the node body. This is the dominant visual element — you see the output of every processing step.
- **Audio nodes:** waveform display (time domain), spectrum analyzer (frequency domain), and a thin level meter strip. You "see the sound" through its visual signatures. Waveform and spectrum update in real time.
- **Control nodes:** compact data display. Current value in large type, sparkline showing recent history, small metadata (frequency, channel, etc.). Intentionally smaller than GPU/Audio nodes — control data is compact by nature.

### Interface Chrome

- **Workspace grid.** A subtle grid underlays the node graph — very low opacity, in the GPU accent color. Provides structure and snap targets without visual noise.
- **Wires.** Thin (1px), in the domain color of the source port, low opacity (40%). Cross-domain wires (Control→GPU, Control→Audio) are dashed to indicate the bridge crossing. Wires should never visually compete with node content.
- **Inspector.** Dark background, parameters as horizontal rows. Slider tracks are dark with a domain-colored fill. Modulation range overlays (Bitwig-inspired) appear as subtle highlights showing the modulated range. Modulation source is indicated by a small tag next to the parameter.
- **Transport bar.** Minimal. Beat position as filled/unfilled dots. BPM as a number. Current state name. No unnecessary decoration.

### What This Is NOT

- Not soft or rounded — sharp corners, no border-radius, no blur effects on chrome
- Not colorful — the three domain colors are the only chromatic accents against neutral gray
- Not decorative — every visual element serves a functional purpose
- Not MaxMSP — not esoteric or diagrammatic, the live content dominates over the wiring
- Not Notch — no irregular node shapes, no visual complexity in the containers themselves

### Color Reference

| Token | Hex | Usage |
|-------|-----|-------|
| `bg.base` | `#16191D` | Application background |
| `bg.panel` | `#1A1D21` | Panel backgrounds |
| `bg.container` | `#22262B` | Node containers, input fields |
| `border.default` | `#2A2E33` | Borders, dividers |
| `border.hover` | `#3A3E43` | Hover state borders |
| `text.primary` | `#E8EAED` | Primary text |
| `text.secondary` | `#8A8F98` | Secondary/label text |
| `domain.gpu` | `#4ECDC4` | GPU accent, wires, badges |
| `domain.audio` | `#F0A030` | Audio accent, wires, badges |
| `domain.control` | `#C0C8D0` | Control accent, wires, badges |
