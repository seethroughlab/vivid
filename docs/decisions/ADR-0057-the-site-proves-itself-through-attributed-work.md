# ADR-0057: The Site Proves Itself Through Attributed Work

Status: proposed

Date: 2026-08-12

## Context

ADR-0055 identifies the site's largest gap as *proof*: for an audience of partners, press, and
notable artists, there is currently no social proof at all — no gallery of work, no artist quotes, no
community links, no about/press. The showcase today (`templates/showcase.html`, `content.json`
`showcase[]`) is nine internally-produced demo reels: excellent for showing the product's surface, but
they are *our* demos, not *their* work, and they carry no attribution.

The competitive scan is decisive on how this audience is persuaded. A "made with X" gallery is table
stakes in the visual-tool category (Cables, TouchDesigner, Spline, Resolume, Cavalry, Notch all have
one). And the form of proof matters: this audience trusts **individual named creators — handle, name,
sometimes a face — over corporate logo walls.** Raycast's wall of recognizable practitioners, Rive's
testimonials with headshots, and Cavalry's creator-handle attribution resonate here in a way that a
Fortune-500 logo grid does not. Awards (Bitwig) and a single honest credibility number also help.

The constraint that shapes this ADR is honesty. ADR-0039 already established the site's discipline:
present a surface as "coming soon" rather than fake it (community packages). The same rule must govern
proof — the failure mode of a proof section is not that it is empty, but that it is *fabricated*.

## Decision

**The site proves itself through attributed work and the voices of peers, not through logos or
claims.** Three surfaces, all governed by an honesty rule.

### A "Made with Vivid" gallery

Today's showcase evolves from an internal demo grid into a curated, filterable **Gallery of work made
in Vivid, attributed to the artists who made it.** Each entry carries the piece (still + AV-synced
clip, reusing the ADR-0056/ADR-0037 media pipeline and the shipping HLS player) and its creator —
name/handle, an optional headshot or avatar, and a link to their site or socials. The nine
first-party demos remain as a "from the makers" strand, clearly labeled as such, so the gallery is
never empty while real external work is gathered.

### Social-proof surfaces

- **Testimonials / quotes** from real users, each with a name, title/handle, and headshot — shown on
  the homepage proof teaser (ADR-0056) and an About/Press page.
- **A notable-creator strand** — recognizable artists using Vivid, presented as people (the Raycast
  pattern), not a logo grid.
- **Awards or press mentions**, if and when they exist.
- **At most one credibility number**, and only if it is real and flattering (e.g. GitHub stars,
  downloads) — surfaced sparingly, in the nav or a proof strip (the Warp pattern).

### Community and learning as first-class proof

Active community and real learning material are themselves proof of a living product. The Community
section (Discord/GitHub/showcase submissions) and the Learn section (Start Here, Tutorials, generated
Operator Reference per ADR-0038) are promoted from footer links to first-class IA, with a clear path
for artists to *submit* their work to the gallery.

### Comparisons (optional, roadmap)

"Vivid vs TouchDesigner / Resolume / Notch" pages are a deliberate acquisition play (the Rive/Framer
pattern) and a strong fit given the category under-markets itself. Scoped as optional and later; the
proof system does not depend on them.

### Content model and the honesty rule

Proof is data-driven, like the existing `showcase[]`/`supports[]`. `content.json` gains new shapes —
roughly `gallery[]` (piece + creator + media + link), `creators[]`, and `testimonials[]` — so
surfaces render from data and stay easy to add to as real material arrives. The build's `_self_check`
extends to validate these shapes (attribution present, media resolvable).

**The honesty rule:** every proof element must be real and attributable. No invented quotes, no
stock "artists," no logos for relationships that do not exist. A proof surface with no real content
ships as "coming soon" or is omitted — never fabricated. This is the ADR-0039 discipline applied to
credibility.

## Alternatives Considered

- **A corporate logo wall.** Rejected as the wrong signal for this audience. Logos persuade
  enterprise buyers; artists and press are moved by other artists and by the work itself. If notable
  studios or venues adopt Vivid, they appear as *creators/quotes with names*, not as a logo grid.
- **Keep the passive first-party showcase, add nothing.** Rejected — a "made with X" gallery is
  expected in this category, and internal demos alone cannot carry the "real people use this" proof
  that partners and press look for.
- **Fabricate or over-state proof to fill the sections now.** Rejected outright — it violates the
  ADR-0039 honesty discipline and is the one failure mode that actively damages credibility with a
  press/partner audience.
- **Gate the whole gallery until enough external work exists.** Rejected — the first-party demos,
  clearly labeled "from the makers," seed the gallery honestly so it is never empty while external
  submissions accumulate.

## Consequences

- The redesign gains a real content dependency: the gallery and testimonial surfaces are only as
  strong as the real artists and quotes gathered, so outreach/collection becomes part of the launch,
  not an afterthought. The honesty rule means these surfaces may launch small and grow.
- `content.json` and `_self_check` (`site/build.py`) grow new data shapes and validation; the showcase
  templates generalize to carry attribution and creator metadata.
- A submission path (form or GitHub-based) is needed so artists can add work — new, small surface
  area to design.
- Community and About/Press become maintained pages, not placeholders; this is ongoing content work
  beyond the initial build.
- Comparison pages, if pursued, are additional content with a research/accuracy burden; keeping them
  optional avoids blocking the core proof system on them.

## References

Related: ADR-0055 (the website is a manifesto for a new instrument — this realizes its Gallery and
proof IA), ADR-0056 (the homepage hero and narrative — the gallery/proof teasers live in its scroll),
ADR-0037 (showcase demos gate the real website — the media pipeline the gallery reuses), ADR-0038
(reference docs are generated from Vivid 4 metadata — the Learn section), ADR-0039 (community packages
are "coming soon" until registry trust exists — the honesty discipline applied here to proof).
