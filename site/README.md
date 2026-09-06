# Vivid 4 website

A slim static site for [Vivid](https://github.com/seethroughlab/vivid) — "an MCP-native creative
coding app." Built with a hand-rolled Python generator (`string.Template` + `markdown`), in the style
of the classic `site/` stack but without its operator-scraping.

## Structure

```
site/
  build.py            # the generator (string.Template + markdown)
  content.json        # site config: nav, tutorials, showcase metadata
  content/*.md        # prose: home, start-here, free-plugins
  templates/*.html    # thin string.Template shells (base, home, cards, ...)
  assets/site.css     # single stylesheet (dark, technical)
```

Showcase hero images are **not** stored here — they are copied at build time from the demo QA harness
output (`examples/demos/showcase/heroes/`), the single source of truth (ADR-0037). Regenerate the
heroes by running that harness against the signed app; see `examples/demos/showcase/README.md`.

## Operator Reference (generated, ADR-0038)

The `/reference/` pages are generated from Vivid 4's **live operator metadata**, never hand-written.
`site/generate_reference.py` launches a clean app-ON instance (user/project operator + shader tiers
redirected to empty temp dirs, so only the bundled core operators register), dumps the unified
operator catalog over the control server, and writes the checked-in snapshot `site/reference.json`.
`build.py` renders the filterable index + one detail page per operator from that snapshot (CI needs
no app). Regenerate when the operator set changes:

```sh
uv run --project site site/generate_reference.py            # dev build
uv run --project site site/generate_reference.py --app /Applications/Vivid.app/Contents/MacOS/Vivid
```

## Build & serve locally

```sh
uv run --project site site/build.py --output site/dist
python3 -m http.server --directory site/dist 8000
# open http://localhost:8000
```

The build runs a self-check: it fails if any section page or hero image is missing, or if an
unresolved template placeholder leaks into the output.

## Deploy

`.github/workflows/pages.yml` builds the site with `uv` on every push/PR touching `site/**`. On pushes
to `main` it deploys `_site/` to the existing **Cloudflare Pages** project `vivid-site`, using the
repo secrets `CF_API_TOKEN` / `CF_ACCOUNT_ID` (already configured from the classic site). PRs run only
the build + self-check. If the Cloudflare project was renamed, update `projectName` in `pages.yml`.

## Sections

Real content: Home, Start Here, Tutorials, Free Plugins, Showcase. "Coming soon": Operator Reference
(will be generated from Vivid 4 metadata, ADR-0038) and Packages (ADR-0039).
