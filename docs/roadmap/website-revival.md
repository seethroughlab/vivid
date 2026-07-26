# Website Revival Plan

Date: 2026-07-26

## Context

`TODO.md` calls for retrieving the general website structure from `vivid-classic`, while starting
over with a single placeholder page. The useful classic precedent is the site stack and information
architecture, not the old product story.

Classic's published site lived under `site/` and was deployed by `.github/workflows/pages.yml`.
It used a small Python static generator, Markdown tutorial content, string templates, plain CSS,
generated operator/package reference pages, and Cloudflare Pages output.

## Classic Website Structure

The major sections in `vivid-classic:site` were:

- Home page: hero, download, tutorials, package catalog, operator reference, GitHub.
- README-derived overview: screenshots, features, built-in operators, package operators, getting
  started, build requirements, architecture, docs, status, license.
- Tutorials: index plus detail pages, split into `composer` and `developer` tracks.
- Operator Reference: generated from JSON operator metadata and grouped by GPU, Audio, Control.
- Package Catalog: generated from `repos.json` and package manifests.
- Package detail pages: overview, guides, generated package-operator pages.
- Published artifacts: generated HTML/CSS, `packages.json`, and `appcast.xml`.

## Product Story Shift

Classic sold Vivid as:

> A real-time creative coding platform where audio and visuals are equal peers in the same graph.

Vivid 4 should not reuse that value proposition verbatim. The site promise is:

> An MCP-native creative coding app.

That is stricter than "agent-first audiovisual environment." If the product cannot support that
claim in the first-run tutorial, then the gap is a product bug to expose and fix, not a copy problem.
The current trunk supports the claim through:

- a DAW-style Session View for tracks, clips, scenes, instruments, effects, and mixer state;
- a rewireable visuals node graph for visual structure;
- a bidirectional mapping bridge between musical/control signals and visual/audio parameters;
- an MCP-native control surface so agents can author, inspect, and verify work.

That means the revived site should put the beginner tutorial and product loop before exhaustive
reference material. The product/site should also rebalance toward creative coding and visuals:
recent work has leaned audio-heavy because audio/plugin hosting is harder and users are less likely
to write their own audio nodes. The website and tutorial plan should deliberately pull visual
authoring, shader forking, project-local code, and MCP-driven iteration back to the center.

## Recommended First Revival

Start with a `site/` subtree that reuses the classic tech stack, but only publishes a placeholder
home page at first.

The first page should make three things clear:

- Vivid is a signed macOS app published through GitHub Releases.
- The beginner path is the front door.
- The full community package catalog is intentionally marked "Coming soon" until Vivid 4's public
  package story is decided.

Suggested first navigation:

- Start Here
- Tutorials
- Examples
- Operator Reference
- Free Plugins
- GitHub

Only `Start Here` needs real content in the first cut; the others can be visible but marked as
coming soon if the generated routes are not ready.

## Decisions Needed

Settled decisions:

- Primary promise: **MCP-native creative coding app**.
- Public install story: signed builds published to GitHub Releases.
- Tutorial artifact contract: every tutorial should create a sample graph/project and double as a
  regression/gap-finding case.
- Docs boundary: site is for onboarding/orientation; repo docs and ADRs remain engineering truth.
- Operator reference source: generate from Vivid 4 package manifests and shader metadata now.

Open decisions:

- First call to action: tutorial-first or download-first. A beginner cannot do the tutorial without
  downloading Vivid, so the likely first CTA is "Download Vivid", immediately followed by "Start the
  tutorial". The tutorial page should treat download/install as step zero, not a separate funnel.
- Free plugin policy: maintain a short, current list of free VST3/CLAP plugins that users can
  download immediately; decide which are required by each tutorial and keep the first tutorial's
  requirements explicit.
- Screenshot/video source: which current Vivid 4 projects are allowed to represent the product.
- Package/community story: publish as "Coming soon" for now, but audit how far the implementation is
  from a real remote catalog.
- Appcast/update role: whether the site still owns `appcast.xml`.

## Community Package Distance

Vivid 4 is not starting from zero on packages. Already-present pieces include:

- `vivid-package.json` manifests and parser/build validation.
- Local install/build/reload flows for operator packages.
- Project-local packages and shaders that travel with a saved folder project.
- MCP tools for package install, validation, build, reload, scaffold, and clone/fork workflows.
- Crash attribution and quarantine for bad operators.
- Shader files as first-class operators with JSON metadata headers.
- Unified operator discovery through the MCP catalog.

The missing public/community layer is likely:

- a curated remote registry format and hosting location;
- package signing/checksum/provenance policy;
- install/update UX for remote packages;
- compatibility/version display against the current operator ABI and app version;
- package docs generation from manifests/source comments;
- moderation/removal/deprecation states;
- CI that builds package examples against current Vivid;
- a clear distinction between bundled, local, project-local, curated, and third-party packages.

So the website should say "Packages coming soon" publicly while the first implementation work
focuses on local/project package docs and generated references.

## Beginner Tutorial Series

The tutorial series should uncover product gaps and bugs while producing sample graphs. Each tutorial
should have a small explicit output, a loadable saved project, and a friction log.

Proposed sequence:

- Download and First Launch: download the signed GitHub Release build, open Vivid, confirm the
  control server/MCP bridge can connect, and open the examples picker.
- First 10 Minutes: open an example, identify Session View, Visual Graph, Output, and the mapping
  bridge.
- First Visual Graph: create generator -> effect -> Output, tweak params, save project.
- First Sound: install the required free plugin(s), create a simple instrument/effect path, and
  document exactly which plugin formats/versions were tested.
- Clips and Scenes: build Intro and Drop scene states.
- Audio Drives Visuals: map a transient/envelope/control source to size, color, bloom, or motion.
- Edit Live: make changes during playback, exercise undo/redo, save/load, verify state survives.
- Use Media: import image/video/text/model assets and document missing-file behavior.
- Fork a Shader: browse a shader, edit it, observe hot reload, recover from an error.
- Write/Fork a Visual Operator: use project-local code or shader metadata to prove the creative
  coding story directly.
- Complete Mini Piece: combine scenes, audio, visual graph, mappings, and saved project output.

For each tutorial, record:

- What could be completed entirely through the UI.
- What required MCP/control-server help.
- What needed an unexplained dependency.
- What crashed, silently failed, or produced unclear health/errors.
- What could not be verified visually or sonically.
- What reusable example graph was produced.

## Free Plugin List

The site should carry a curated free-plugin page with direct download links, supported format notes,
and tutorial usage. Candidates to evaluate:

- Surge XT: CLAP/VST3 instrument and effects; strong default candidate for required tutorials.
- Vital Basic: free wavetable synth; useful if VST3 path support is smoother for users.
- Dexed: free FM synth; small, recognizable, good for MIDI/instrument tests.
- TAL-NoiseMaker: free subtractive synth; simple beginner sound source.
- Valhalla Supermassive: free effect; excellent for "add space" tutorials.
- BPB Cassette Drums: free drum instrument; useful for showcase demos, but installation/data-folder
  friction must be documented clearly.

Before making any plugin required, verify current download URL, license/free tier, macOS format,
Apple Silicon/Intel status, install path, Vivid scan behavior, preset loading, and whether the
tutorial can recover when it is missing.

## Screenshots And Showcases

New screenshots are required. Classic images should not be reused because they sell the old
single-graph product. The current showcase demos also need work before they can carry the site:

- They should represent "MCP-native creative coding app", not only audio complexity.
- At least one showcase should visibly demonstrate editing/forking a shader or project-local visual
  operator.
- At least one should show Session View scenes/clips plus the visual graph and mappings.
- At least one should use required free plugins exactly as documented.
- Each screenshot candidate should come from a saved, loadable project that can be regenerated.
- Site images should be treated as release artifacts of the tutorial/showcase QA path.

## Implementation Shape

Recommended phases:

- Phase 0: accept the website-gating ADR sequence in implementation order:
  [ADR-0034](../decisions/ADR-0034-mcp-native-creative-coding-is-the-public-promise.md),
  [ADR-0035](../decisions/ADR-0035-tutorials-are-release-gated-sample-projects.md),
  [ADR-0036](../decisions/ADR-0036-free-plugin-onboarding-is-curated.md),
  [ADR-0037](../decisions/ADR-0037-showcase-demos-gate-the-real-website.md),
  [ADR-0038](../decisions/ADR-0038-reference-docs-are-generated-from-vivid-4-metadata.md),
  [ADR-0039](../decisions/ADR-0039-community-packages-are-coming-soon-until-registry-trust-exists.md).
- Phase 1: copy/adapt classic `site/` build skeleton; render one Vivid 4 placeholder home page.
- Phase 2: add signed-release download CTA, tutorial index, free-plugin page, and the first
  beginner tutorial page.
- Phase 3: wire tutorial sample projects into `examples/` and make them part of a lightweight
  smoke/regression path.
- Phase 4: regenerate operator reference from Vivid 4 metadata.
- Phase 5: add showcase screenshots/videos generated from refreshed demos.
- Phase 6: decide and implement package/community catalog beyond "Coming soon".
