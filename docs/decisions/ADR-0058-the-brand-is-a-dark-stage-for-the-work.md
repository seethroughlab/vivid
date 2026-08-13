# ADR-0058: The Brand Is a Dark Stage for the Work

Status: proposed

Date: 2026-08-12

## Context

ADR-0055 raised the site's craft bar to "out-craft the category" and named the concrete gaps but
deferred the detailed visual system to its own record. This is that record.

The current design is an honest developer-docs skin (`site/assets/site.css`, 412 hand-written lines):
a dark canvas (`--bg #16181d`), a cyan/blue/magenta accent set (`--accent #5ea0ff`, `--accent-2
#ff5aa8`, `--accent-3 #4ecdc4`) chosen to "echo the showcase heroes," `--radius 4px`, one content
width (`--maxw 1080px`), and a single `@media (max-width: 640px)` breakpoint. Its problems, for a
partner/press/artist audience:

- **No identity.** The brand is the lowercase text string `vivid` — no wordmark or mark, no favicon,
  and no social-share (OG/Twitter) image, so links preview as blank cards to the exact press audience
  most likely to share them.
- **Monospace everything.** The type is a single mono stack, and the intended face (`JetBrains Mono`)
  is never even loaded, so most visitors see system mono. Mono-only reads as "technical docs," not as
  a new medium an artist would want to play.
- **Static and flat.** The product is motion and color; the site's chrome is neither. There is no
  motion language, no depth, and colorful output has to fight a mid-gray canvas rather than glow
  against a dark stage.
- **One breakpoint.** No real responsive system; the layout is desktop-with-a-mobile-fallback.

The competitive scan is consistent: creative and developer tools that persuade this audience are
**dark, motion-rich, and typographically confident** (Linear, Raycast, Warp, Notch, Bitwig,
TouchDesigner). The dark canvas is not a default — it is a stage that makes colorful, luminous output
pop. Vivid's output *is* colorful and luminous; the brand should get out of its way.

## Decision

**The visual system is a dark stage: restrained, near-black chrome that frames the work, so the
audio-reactive output is always the brightest thing on the screen.** The brand exists to present the
medium, not to compete with it.

### Identity

Ship a real identity: a **wordmark and a compact mark** (SVG, scalable, dark/light-safe), a
**favicon**, and a **social-share image** (OG/Twitter, one per key page or a strong default) so shared
links preview well. The mark should read as an instrument/signal, not a generic app glyph; the
cyan↔magenta signal motif already in the palette is the natural seed.

### Color — the stage and the signal

- **Stage:** deepen the canvas toward near-black layered surfaces so luminous output and accents glow.
  Keep the layered `--bg`/`--bg2`/`--card` model; darken the base.
- **Signal:** keep the cyan↔magenta (with a teal third) as the brand accent — it already echoes the
  showcase output, which ties chrome to product. Formalize it as a small, documented token set with
  defined roles (primary action, links, focus, hover) rather than three ad-hoc accents.
- **Dark-only remains the commitment** (a light theme is explicitly not a goal); the stage metaphor
  depends on it.

### Typography — a confident pairing, mono earns its place

Move off mono-only to a deliberate pairing:

- A **distinctive display face for headlines** to carry the manifesto voice (Play · Rewire · Author ·
  Fork) with presence.
- A **highly readable body face** for prose.
- **Monospace reserved for what is actually code/technical** — operator names, params, snippets,
  reference tables — where it signals "this is real code," which is on-brand for the Author story.

All faces are **self-hosted** (`@font-face` with local files under `site/assets/`), not left as an
unloaded `font-family` declaration, and chosen to be fast and license-clean for a static site. A real
**type scale** (display → h1..h3 → body → small) and **spacing scale** replace the current ad-hoc
sizes.

### Motion — kinetic but disciplined

The chrome may move — subtle entrance/parallax/hover motion that makes the site feel alive — but
motion is **purposeful and calm**, never decorative noise, and **`prefers-reduced-motion` is honored
everywhere** (as the showcase player already does). The loudest motion on any page is the product
itself (the ADR-0056 hero reel, gallery clips), not the UI.

### Imagery treatment

The **work is the imagery.** Audio-reactive output (stills and clips) is the primary visual; UI
screenshots are secondary and always shown in context. Media sits on the dark stage with generous
surrounding space so its color reads. This is consistent with ADR-0037 (site media is regenerable
demo/gallery artifact) and ADR-0057 (attributed work).

### Layout and responsiveness

Introduce a real responsive system — mobile, tablet, and desktop breakpoints and fluid type — and
allow **full-bleed** sections (the hero reel, gallery) to break the `--maxw` container while prose
stays comfortably measured. 

### Implementation

All of the above lands as **design tokens and rules in the existing static stack** — extend
`:root`/`site.css`, add self-hosted font files, add the mark/favicon/OG assets, and add breakpoints.
No CSS framework, no build-step change, no replatform (consistent with ADR-0055's decision to keep
`site/build.py`). The `_self_check` grows to assert the favicon and OG image exist so a build can't
silently ship without them.

## Alternatives Considered

- **Keep the mono-only, docs-adjacent aesthetic.** Rejected. It is coherent and cheap, but it reads
  as developer documentation to an audience of artists and press; the point of this redesign is to
  look like a new medium, not a tool's manual.
- **A light or neutral theme.** Rejected. Dark is the category norm for a reason — it is a stage that
  makes luminous, colorful output pop; a light canvas would mute the one thing the site must sell.
- **Adopt a CSS framework or design-system library (Tailwind, a component kit).** Rejected as
  unnecessary weight for a small static site; hand-authored tokens in `site.css` are sufficient and
  keep the stack dependency-light per ADR-0055.
- **Commission a full external rebrand before the site work.** Deferred, not rejected. A deeper brand
  exploration can happen later; this ADR defines a coherent, buildable system now so the redesign is
  not blocked on an agency engagement.
- **Keep the three ad-hoc accents as-is.** Rejected in favor of a small documented token set with
  defined roles, so color stays consistent as the site grows.

## Consequences

- Real design assets must be produced that do not exist today: a wordmark/mark, favicon, OG image, and
  chosen/self-hosted fonts. This is genuine design work, and font licensing must be checked.
- `site/assets/site.css` is substantially reworked (darkened stage, token set with roles, type +
  spacing scales, multiple breakpoints, full-bleed support) and `templates/base.html` gains the
  favicon, OG/Twitter meta tags, and font links; `_self_check` gates the required assets.
- Page weight grows with self-hosted fonts and richer media; performance must be watched (subset
  fonts, lazy media) so the site stays fast — an implicit part of the craft bar.
- The visual system underpins ADR-0056 (the homepage) and ADR-0057 (gallery/proof); those should be
  built against these tokens rather than inventing styling inline.
- Dark-only and reduced-motion support are locked in as commitments, keeping the site accessible and
  on-brand.

## References

Related: ADR-0055 (the website is a manifesto for a new instrument — this fulfills its craft bar),
ADR-0056 (the homepage hero and narrative — built on this system), ADR-0057 (the site proves itself
through attributed work — gallery/proof styled by this system), ADR-0037 (showcase demos gate the real
website — site media as regenerable artifact).
