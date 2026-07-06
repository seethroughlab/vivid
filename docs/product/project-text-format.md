# Vivid 4 Project Text Format (sketch)

Status: draft — sketch, not a serialization spec

Date: 2026-06-21

## Purpose

"Text is the source of truth" (PRD principle 7; [ADR-0006](../decisions/ADR-0006-agent-external-mcp.md)).
The Session View prototypes currently hold everything in an in-memory JavaScript `state` object.
This document sketches the **authored project text** that object stands in for, so that the next
features (perception layer, bindings, more agent intents) round-trip through **one agreed shape**
instead of inventing ad-hoc structures per prototype.

This is a **schema sketch**: it fixes the entities, their identity, and their relationships. It does
**not** pick a serialization format — that is deferred to a future ADR (see Open Questions).

## Authored vs runtime — the key split

The prototype's `state` mixes two things that the text format must keep apart. Only **authored**
state is project text. **Runtime** state is ephemeral (or local config) and is never persisted in
the project.

| In project text (authored, diffable) | Not in project text (runtime / local) |
|---|---|
| transport: bpm, meter, key, launch quantize | playhead: bar, beat |
| tracks | active scene, queued scene |
| scenes (name, intent) | current selection |
| cells → wells → takes (incl. `kept`) | perf metrics (fps, frame ms, mem, audio load) |
| bindings | **attached agent provider** (local config / keychain — ADR-0008) |

The attached provider is deliberately *not* project text: a project must not hard-code "use
Claude." Provider choice and keys are local environment config.

## Entities

Identity: every entity has a stable `id`. References are by `id`. Collections are ordered for
diff stability.

- **Session** (root) — `name`; `transport` { `bpm`, `meter`, `key`, `quantize` }; lists of
  `tracks`, `scenes`, `bindings`.
- **Track** — `id`, `name`, `kind` (`instrument | audio | visual | mapping | hybrid`).
- **Scene** — `id`, `name`, `intent`. The set of cells belonging to a scene *is* its clip
  assignment; there is no separate assignment table.
- **Cell** — identified by `(scene, track)`. Holds one **Well**. A plain clip is just a
  single-take well.
- **Variation Well** — `live` (id of the live take) + ordered `takes`. (Glossary: *Variation
  Well*.)
- **Take** — `id`, `name`, `kind` (`drum | midi | theory | plugin | visual | binding`), `tags`,
  `kept` (bool), and kind-specific `content` (e.g. a step `pattern`, MIDI notes, plugin-state
  reference, visual-state params). The *live take* is the cell's active clip. (Glossary: *Take*,
  *Live Take*.)
- **Binding** — `id`, `scope` (which scenes), `source` (signal ref, e.g. `bass.envelope`),
  `dest` (behavior ref, e.g. `particles.size`), `curve`, `timing`, `reason`. (Glossary:
  *Audio-Visual Binding*.)

## Illustrative serialization (format TBD)

Shown in a YAML-ish form purely to make the shape concrete — the actual format is not yet decided.

```yaml
session:
  name: One-Song Loop
  transport: { bpm: 124, meter: 4/4, key: D minor, quantize: bar }

tracks:
  - { id: bass, name: Bass, kind: instrument }
  - { id: particles, name: Particles, kind: visual }

scenes:
  - id: verse
    name: Verse
    intent: Tight groove, low visual density.
    cells:
      bass:                       # cell = (scene: verse, track: bass)
        well:
          live: t2                # the live take drives this cell
          takes:
            - { id: t1, name: D Minor Pulse, kind: theory, pattern: "1000101010001010", tags: [root/fifth], kept: false }
            - { id: t2, name: Pulse,         kind: theory, pattern: "1010001010100010", tags: [agent],      kept: true  }

bindings:
  - id: b1
    scope: [verse, chorus]
    source: bass.envelope
    dest: particles.size
    curve: soft-exponential
    timing: smoothed-80ms
    reason: Keeps verse movement tied to the groove without literal flashing.
```

## Mapping from the prototype `state`

| Prototype `state` | Project text | Notes |
|---|---|---|
| `transport.bpm`, `meter` (+ Key/Quantize pills) | `session.transport` | Key & quantize were UI pills; they belong in transport. |
| `tracks[]` | `tracks[]` | Direct. |
| `scenes[]` (`id`,`name`,`intent`) | `scenes[]` | `energy/density/motion/color` are visual-intent params — authored *or* derived (open question). |
| `clips[scene][track]` | `scenes[].cells[track].well` (single take) | A plain clip is a one-take well. |
| `wells[clipId]` | `scenes[].cells[track].well` (multi take) | `live`, `takes`, `kept` carry over. |
| `bindings[]` | `bindings[]` | Direct; `scene` → `scope`. |
| `activeScene`, `queuedScene`, `selected`, `transport.bar/beat`, `providerIdx` | — | Runtime / local; not persisted. |

## Open questions (defer until they bite)

- **Serialization format** (YAML / TOML / JSON-stable / a directory tree) — needs its own ADR.
  Requirement: readable, diffable, recoverable, agent-addressable.
- **Cell grouping** for diff locality: nest `scene → track` (launch-natural, shown above) vs
  `track → scene` (editing one part touches one place).
- **Scene visual params** (`energy/density/...`): authored scene state, or derived from the active
  clips + bindings?
- **Take `content` per kind**: the concrete schema for midi / plugin-state / visual-state content
  is out of scope here; this sketch models identity + metadata + a `pattern` placeholder only.
- **Binding `source`/`dest` reference grammar**: `track.signal` / `layer.param` namespacing.
