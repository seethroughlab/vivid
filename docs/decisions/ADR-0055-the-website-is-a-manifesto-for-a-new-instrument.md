# ADR-0055: The Website Is a Manifesto for a New Instrument

Status: proposed

Date: 2026-08-12

## Context

The public site at vivid.seethroughlab.com shipped as an honest, functional developer-docs skeleton
(ADR-0037 through ADR-0041): a hero, a "what it is" list, a showcase grid, a generated operator
reference, and a download funnel. It works, but it undersells the product, and a specific audience is
about to look at it closely — partners and collaborators, press and creative-technology influencers,
and notable artists (VJs, live performers, generative artists, musicians). These are peers. They
decide fast, and they trust craft and other artists over feature lists and corporate logos.

Three problems make the current site the wrong front door for that audience:

1. **The lede is buried.** The hero headline is "An MCP-native creative coding app" (`content.json`
   `tagline`, `content/home.md`). That is a *mechanism*, stated in jargon ("MCP," "operator ABI 17"),
   before any plain-language reason to care. The thing that actually lands with a musician or a VJ —
   *a live audiovisual instrument where sound becomes generative visuals you can rewire and extend* —
   is nowhere above the fold, and the "author your own operators" north-star has no page, section, or
   demo of its own.
2. **There is no proof and no story.** The site has zero social proof: no artists, no quotes, no
   "made with Vivid" gallery, no community links, no about/press. For an audience of peers, this is
   the single largest gap.
3. **It reads as documentation, not as a product for artists.** Dark, monospace, one breakpoint, no
   logo/wordmark image, no social share image, and the intended brand font is declared but never
   loaded (`assets/site.css`). It looks like a tool's docs, not like a new medium.

A scan of the field (TouchDesigner, Notch, Cables, Resolume, Ableton, Bitwig, Rive, Spline, Linear,
Framer, Raycast, Warp) shows the category *under-markets itself*: the closest competitors lean on UI
screenshots and community images and rarely tell a tight, motion-first "sound drives visuals" story.
That is an opening. Vivid can out-craft the space rather than imitate it.

This ADR sets the site's purpose, audience, positioning, and information architecture. Two companion
ADRs decide the homepage experience (ADR-0056) and the proof system (ADR-0057). Implementation is
downstream; these ADRs set the direction the redesign is built against.

## Decision

**The website's job is to make a first-time visitor grasp the vision in thirty seconds** — not to
convert a download. The audience is peers (partners, press, artists), so credibility comes from
showing the instrument in motion and from other artists' work, not from a feature funnel.

The public positioning leads with the medium, not the mechanism. Vivid's public promise becomes:

> Vivid is a new instrument for live visuals — play sound and picture as one, rewire the signal path
> live, author your own operators with code and AI, and fork the whole thing.

Four verbs carry the story and structure the site's narrative:

- **Play** — sound and picture are one signal path you perform in real time.
- **Rewire** — the path between them is a live, rewireable node graph, not a fixed pipeline.
- **Author** — you write your own operators (shaders, compiled nodes) in code, with AI as a
  first-class collaborator over MCP.
- **Fork** — a project is a folder you own end to end; take it, change it, run it anywhere.

This **reframes ADR-0040's public lead, not its substance.** ADR-0040 made "MCP-native creative
coding app" the public promise *and* a product gate. The product gate stands unchanged — MCP and
LLM-authored operators remain *the* differentiator and the deepest part of the story. What changes is
sequencing on the public site: "MCP-native" and "author with code and AI" are the **reveal** (the
"and it goes deeper" third act), not the first sentence a musician reads. The instrument is the hook;
the authoring loop is the payoff. ADR-0040's `Status:` line is annotated to record this
public-facing-lead amendment.

### Information architecture

The site expands from a docs index into a narrative product site. Top-level structure:

- **Home** — the manifesto homepage (see ADR-0056): open on sound driving picture, then Play →
  Rewire → Author → Fork, capability grid, audience segmentation, gallery/proof teasers, CTA.
- **The Instrument** — the product/how-it-works pages: Session View, the rewireable visual node
  graph, and the mapping bridge that binds them. This is where the architecture is explained *after*
  the vision has landed.
- **Gallery** — "Made with Vivid," attributed to the artists who made it (see ADR-0057). This
  replaces today's passive showcase grid as the primary proof surface.
- **Learn** — a first-class getting-started experience plus the generated Operator Reference
  (ADR-0038). Getting started is a designed, on-site, motion-first, GUI-first tutorial series
  (ADR-0059), not an index of GitHub links — it is where "grasp the vision" becomes "I could make
  this," and where the "author your own operators" story finally lands as the learner graduates to
  code and AI.
- **Community** — Discord/GitHub/showcase submissions; the ecosystem this audience buys into.
- **About / Press** — the story, the maker, and a press kit (logo, screenshots, one-liner, contact).
  This matters specifically for the partner/press audience and does not exist today.
- **Download** — kept as a persistent CTA, but no longer the site's organizing principle.
- **Comparisons** (optional, roadmap) — "Vivid vs TouchDesigner / Resolume / Notch" pages, a
  deliberate acquisition play borrowed from Rive and Framer.

**Packages** stays "Coming soon" per ADR-0039 until a registry and trust model exist; the redesign
does not change that decision.

### Craft bar

The site commits to out-crafting the category rather than matching it. Concretely, the redesign must
raise the production floor to: a real wordmark/logo and favicon; a social-share (OG/Twitter) image so
shared links preview well to press; the intended brand font actually loaded; a motion-rich dark
aesthetic that makes colorful output pop; and a responsive layout beyond the single 640px breakpoint.
The detailed brand/visual system is decided in ADR-0058; this ADR sets the bar and the reason for it.

The static-generation stack (`site/build.py`, `content.json`, `string.Template`, Cloudflare Pages)
is kept — it is fast, dependency-light, and sufficient. The redesign is a content, structure, and
craft overhaul, not a replatforming.

## Alternatives Considered

- **Keep "MCP-native creative coding app" as the public lead (ADR-0040 unchanged).** Rejected for
  this audience. It is accurate and it is the north-star, but it is a mechanism stated in jargon; a
  musician or VJ cannot tell in thirty seconds what they would make or why they should care. The
  substance is preserved — it becomes the reveal, not the headline.
- **Investor-first framing (market, traction, moat).** Rejected. The stated audience is
  partners/press/artists, who are moved by the work and the craft, not by a pitch deck rendered as a
  webpage. An investor narrative can live in About/Press if needed, without setting the homepage tone.
- **Minimal content polish on the current structure.** Rejected against the stated ambition. Fixing
  copy and adding a font would not close the proof gap or change the "this is docs" read; the site
  needs a new spine, not a coat of paint.
- **Replatform to a JS framework / CMS.** Rejected as unnecessary. The static generator already
  produces the required pages and gates them with a self-check; the gap is narrative, proof, and
  craft, none of which require a framework.

## Consequences

- The homepage and top-level IA change shape; today's routes (Start Here, Tutorials, Showcase, Free
  Plugins, Reference) are kept but re-homed under Learn/Gallery, so existing generated content
  (ADR-0038) and the showcase pipeline (ADR-0037) are reused, not discarded.
- ADR-0040 is amended in scope: its product gate stands; only the *public headline ordering* changes.
  This ADR must land that annotation so the decision chain stays coherent.
- New surfaces (Gallery with attribution, About/Press, Community) create a content dependency: they
  are only credible with real artists, quotes, and links. ADR-0057 sets the honesty rule that governs
  shipping them.
- Raising the craft bar adds real design work (logo, OG image, font loading, responsive pass) that
  did not exist in the docs-skeleton scope.
- The "author your own operators" north-star finally gets a first-class place in the narrative (the
  Author act and a Learn home), aligning the public story with ADR-0046/0054's internal north-star.

## References

Related: ADR-0037 (showcase demos gate the real website), ADR-0038 (reference docs are generated from
Vivid 4 metadata), ADR-0039 (community packages are "coming soon" until registry trust exists),
ADR-0040 (MCP-native creative coding is the public promise — public-facing lead amended here),
ADR-0041 (procedural 3D scene graph for audio-reactive visuals). Companions: ADR-0056 (the homepage
hero and narrative), ADR-0057 (the gallery and social-proof system), ADR-0058 (the brand and visual
system), ADR-0059 (fun, motion-first getting-started tutorials).
