# Graphs

Graphs are now organized as real folders and discovered via recursive scan.

- `intro/` — first-run starter patches
- `audio/` — audio/control-focused examples
- `gpu/` — core GPU composition examples
- `filters/` — focused WGSL/post-process examples
- `io/` — OSC/Syphon/Movie File I/O examples

Each graph JSON should include a top-level `meta` object. Discovery/search uses this data.
You can update this in runtime via **File -> Open Example... -> Edit Meta** (opens the graph file for metadata edits).

Example:

```json
{
  "meta": {
    "id": "av_demo",
    "title": "Audio + Visual Starter",
    "description": "Canonical first audiovisual patch.",
    "tags": ["starter", "av", "audio", "gpu"],
    "difficulty": "beginner",
    "domains": ["audio", "gpu", "control"],
    "requires_packages": [],
    "featured_rank": 0
  },
  "nodes": {},
  "connections": []
}
```
