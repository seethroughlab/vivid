
- The horizontal resize line should be the same style as the vertical
- ~~There are a few visual operators that could just be shaders...~~ → **ADR-0016** (shaders are content; in flight on this branch). The two open questions it raised are now owned: *specifying uniforms of arbitrary shaders* → ADR-0016 (the JSON header); *browsing the shaders shipped with vivid* → **ADR-0021** (content is browsable). Authoring them live → **ADR-0020**.
- The background grid of the graph doesn't extend to the edges of the container it's in.
- The paramter sliders look terrrible still.
- Retrieve the general website structure from classic but start over with a single placeholder page
- How are we going to deal with community-contributed packages with vivid-4? (Partly answered: **ADR-0018** quarantines a package operator that repeatedly crashes; **ADR-0021** covers local content scopes. A *remote* catalog/registry is still open and needs its own ADR.)
- Note operators in the audio graph need better thumbnails
- Resizing the vertical split shouldn't move the visuals graph.