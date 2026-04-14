# Curated Graphs

This folder is the beta-facing example library used by **File -> Open Example...**.
It is intentionally smaller than the full graph corpus.

- `intro/` - first-run starter patches
- `audio/` - polished audio and music examples
- `gpu/` - polished visual examples
- `filters/` - approachable shader/post-process examples
- `media/` - movie-file and file-backed media examples
- `io/` - live external I/O examples: MIDI, OSC, and Syphon

Developer references, one-operator demos, older variants, environment-heavy examples, and shader
sketches live in `../reference_graphs/`. Regression and listening fixtures live under
`../tests/graphs/`.

Every curated graph JSON should include:

```json
{
  "meta": {
    "id": "av_demo",
    "title": "Audio-Visual Sync",
    "description": "One shared LFO drives both visual scale and audio pitch.",
    "tags": ["starter", "av", "audio", "gpu"],
    "difficulty": "beginner",
    "domains": ["audio", "gpu", "control"],
    "requires_packages": [],
    "featured_rank": 4,
    "estimated_minutes": 1,
    "content_kind": "example",
    "category": "starter",
    "family": "Cross-domain starters",
    "role": "starter",
    "playability": "self_playing"
  }
}
```

Use `reference_graphs/` for broad operator coverage. Use this folder for graphs a beta user should
feel comfortable opening without knowing Vivid internals.
