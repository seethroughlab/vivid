
- ~~The horizontal resize line should be the same style as the vertical~~ → **DONE (PR #30)** — splitter style unified.
- ~~There are a few visual operators that could just be shaders...~~ → **ADR-0016 ✅ shipped (PR #28).** The two open questions it raised are now both answered: *specifying uniforms of arbitrary shaders* → ADR-0016 (the JSON header); *browsing the shaders shipped with vivid* → **ADR-0021 ✅ shipped (PR #29)** (content is browsable — the shader library view). Authoring them live → **ADR-0020 ✅ shipped (PRs #34/#35)** (the inner loop is visible — always-on watch, rollback-first, errors on the badge, fork-to-edit).
- ~~The background grid of the graph doesn't extend to the edges of the container it's in.~~ → **DONE (PR #30)** — `NodeGraph::set_frame` draws the grid to the column edges.
- ~~The paramter sliders look terrrible still.~~ → **DONE (PR #30)** — `slider()` restyle + the curated param inspector.
- Retrieve the general website structure from classic but start over with a single placeholder page
- How are we going to deal with community-contributed packages with vivid-4? (Partly answered: **ADR-0018 ✅ shipped** — a package operator that repeatedly crashes is attributed + quarantined; **ADR-0021** covers local content scopes. A *remote* catalog/registry is still open and needs its own ADR.)
- Note operators in the audio graph need better thumbnails
- ~~Resizing the vertical split shouldn't move the visuals graph.~~ → **DONE (PR #30)** — removed the per-frame node shift in `NodeGraph::set_bounds`.