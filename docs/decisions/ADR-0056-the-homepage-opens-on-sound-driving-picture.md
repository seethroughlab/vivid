# ADR-0056: The Homepage Opens on Sound Driving Picture

Status: accepted

Date: 2026-08-12

## Context

ADR-0055 reframes the site around a manifesto — *a new instrument for live visuals* — and the four
verbs Play, Rewire, Author, Fork. This ADR decides the homepage that carries it: what the visitor
sees, in what order, in the first thirty seconds.

The current homepage (`templates/home.html`, `content/home.md`) opens on a text hero — a headline and
a paragraph — and the first moving imagery is the showcase grid several sections down. For an
*audiovisual instrument*, that is the wrong first impression: the product's whole point is that you
hear and see it react, and none of that is above the fold.

The competitive scan is unusually clear here. The strongest product sites show the product *in
motion* in the hero — Framer shows the agent working, Spline and Hydra are interactive, Ableton lets
you trigger audio, Cables animates the node graph. And critically, *no competitor in the audiovisual
category nails the obvious move*: a hero where **sound is visibly driving the picture**. That is the
single highest-leverage thing this homepage can do.

Vivid already has the machinery to do it without new web technology. The showcase pipeline
(`site/scripts/runner.py` → `process-showcase-video.sh` → `upload-showcase-video.sh`) captures
AV-synced clips from real projects — the audio *is* Vivid making the sound that drives the picture —
encodes them to HLS, and serves them from CloudFront (`d2at399jej1hdi.cloudfront.net`). The page
already ships an HLS player (`SHOWCASE_PLAYER_JS`) with muted-autoplay, click-to-unmute, and
graceful degradation to a still.

## Decision

**The homepage opens on a full-bleed, looping, audio-reactive reel** — a real Vivid performance where
sound visibly drives the visuals. It autoplays muted (respecting `prefers-reduced-motion`) and invites
one click to unmute; unmuted, the visitor hears the sound and sees it move the picture in the same
moment. This is the category's missing hero, and Vivid ships it first.

The reel is produced by the existing showcase pipeline and is therefore a **release artifact of demo
QA, not hand-made marketing** — the ADR-0037 rule holds: every second on screen comes from a named,
regenerable project. The hero reuses the shipping HLS player and its fallbacks: if CloudFront is cold
or HLS fails, the hero degrades to a high-quality still rather than an empty frame. No new web runtime
is introduced.

The homepage then tells the manifesto as a scroll, mapping to the four verbs from ADR-0055:

1. **Hero reel** — sound driving picture, full-bleed, one-click unmute. Overlaid: the one-sentence
   promise and a primary CTA (Download) plus a secondary (See it in motion / Gallery).
2. **One-line "what it is"** — a single plain-language sentence a musician or VJ understands
   instantly, immediately under the hero.
3. **The node graph, shown moving (Rewire).** The differentiator, made kinetic: audio → nodes →
   visuals, with signal visibly flowing through the graph — not a static screenshot. This is the "how
   it works" beat, and it is where Vivid looks like nothing else.
4. **Capability grid.** Named building blocks, each with a representative render/thumbnail — e.g.
   audio-analysis nodes, particles, feedback, 3D scene (ADR-0041), shaders. Shows range at a glance.
5. **Numbered benefit sections (Play · Rewire · Author · Fork).** Three to five verb-led sections,
   each pairing a benefit with a real UI shot or clip. The **Author** section is where the MCP /
   LLM-authored-operators reveal lands — the deep differentiator, arriving after the visitor already
   wants it, not before.
6. **Audience segmentation.** VJs · live performers · generative artists · musicians — one tailored
   line and visual each, so every visitor sees themselves.
7. **Gallery teaser** — a few pieces of attributed "Made with Vivid" work, linking to the full
   Gallery (ADR-0057).
8. **Proof teaser** — artist quotes / notable creators (ADR-0057), shown only when real.
9. **Community + Learn** — Discord/GitHub and a "start with a tutorial" entry into Learn.
10. **Final CTA** — repeat the primary action.
11. **Rich footer** — Product / Learn / Gallery / Community / About / Comparisons, per the ADR-0055 IA.

The hero reel and the "node graph moving" beat are the two non-negotiable moving elements; everything
else may be still imagery if a clip is not available, degrading gracefully.

## Alternatives Considered

- **A live, interactive in-browser WebGL toy** (a bespoke web demo where the visitor drags nodes or
  feeds audio and watches visuals react). Deferred, not rejected — it is the highest-wow option and a
  natural post-launch upgrade, but it is a separate build with its own maintenance surface, and it is
  not required to make sound-drives-picture land. The looping reel captures most of the effect now;
  the toy can replace or augment the hero later.
- **A WebAssembly port of the real engine in the hero.** Rejected for this effort. Maximum
  authenticity, but it is a large independent epic (porting rendering/audio to the browser), far out
  of scope for a homepage.
- **Keep the text-only hero and lead with the showcase grid lower down.** Rejected — this is the
  current state, and it puts the product's entire reason for existing below the fold.
- **A static hero image of the app UI.** Rejected — a still of a node graph reads as "developer
  tool"; the product only makes sense in motion with sound.

## Consequences

- The homepage depends on at least one hero-grade AV-synced reel from the showcase pipeline, and on
  the "node graph moving" clip; producing these is real demo/capture work gated by ADR-0037.
- `templates/home.html`, `content/home.md`, and `content.json` change substantially (new hero block,
  new section shapes for the capability grid, benefit sections, and audience segmentation); the
  existing `SHOWCASE_PLAYER_JS` and its fallbacks are reused rather than rewritten.
- Because the hero is a release artifact, refreshing it is a re-capture, not a redesign — keeping site
  media honest and regenerable per ADR-0037.
- Deferring the interactive toy keeps this effort shippable while leaving a clear, recorded upgrade
  path; if the toy is later built, this ADR is the place its decision attaches.
- The reveal ordering (MCP/authoring as the Author act, not the headline) is realized structurally
  here, making ADR-0055's positioning concrete rather than aspirational.

## References

Related: ADR-0055 (the website is a manifesto for a new instrument), ADR-0037 (showcase demos gate the
real website — the reel is a demo-QA artifact), ADR-0040 (MCP-native creative coding is the public
promise — surfaced as the "Author" reveal), ADR-0041 (procedural 3D scene graph for audio-reactive
visuals — feeds the reel and the capability grid), ADR-0057 (the gallery and social-proof system).
