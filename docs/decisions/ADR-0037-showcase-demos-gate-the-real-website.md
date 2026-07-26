# ADR-0037: Showcase Demos Gate the Real Website

Status: accepted

Date: 2026-07-26

## Context

The revived website needs current screenshots and possibly short videos. Classic screenshots cannot
represent Vivid 4 because they sell the old single-graph product. The current showcase demos are
useful, but they were built while audio and plugin hosting were the hardest product risks; they do
not yet fully prove "MCP-native creative coding app."

If the website launches with weak or misleading visuals, it will either undersell Vivid or teach the
wrong product model.

## Decision

The real website is gated by refreshed showcase demos. A showcase is website-ready only if it is a
saved, loadable, regenerable project that demonstrates the current Vivid 4 product model.

At minimum, the showcase set must include:

- A first-run beginner project that matches ADR-0035.
- A scene/clip project that visibly uses Session View and the visual graph together.
- An audio-reactive visual project that makes the mapping bridge inspectable.
- A creative coding project that forks or authors a shader/project-local operator.
- A plugin-based music project that uses the curated free-plugin path from ADR-0036.

Each website screenshot or video must come from a named project with a regeneration path. Site media
is treated as a release artifact of the demo QA path, not as hand-crafted marketing material.

## Alternatives Considered

- **Use classic screenshots temporarily.** Rejected. They communicate the wrong architecture.
- **Use atmospheric or abstract images.** Rejected. Users need to inspect the actual product and
  output.
- **Wait for the website until every demo is perfect.** Rejected. A placeholder can ship early, but
  the real site should not claim more than the demos prove.

## Consequences

- Demo polish is on the critical path before a public website launch.
- Showcase work will uncover UI, MCP, save/load, plugin, media, and rendering gaps.
- The website implementation should support adding media later without redesigning the whole site.

