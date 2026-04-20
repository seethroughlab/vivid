# Phase 3 — Final Infrastructure (pre-test-run)

**Date:** 2026-04-20
**Scope:** Three items requested before the reference-translation test run:
1. Per-band (bass/mid/treble) reactivity correlations
2. YouTube multi-frame thumbnail extraction
3. Reference-corpus tool (reframed post-pivot)

## 1. Per-band reactivity correlations

### What shipped

`AVReactivityMetrics` now carries three `BandCorrelations` structs — `band_brightness_correlations`, `band_motion_correlations`, `band_contrast_correlations` — each with `bass` (<250 Hz), `mid` (250–2000 Hz), `treble` (>2000 Hz). Surfaces cases where overall correlation is weak but a specific frequency band drives a specific visual axis.

Implementation: during the existing per-chunk RMS computation in `analyze_av_reactivity`, a 1024-point Hann-windowed FFT is now also run per chunk. Magnitude energy sums into three bins by frequency; three time series (one per band) × three visual time series = 9 additional Pearson correlations. FFT per chunk is cheap — ~60 FFTs for a 3 s window, <30 ms total.

### Files touched

- `src/runtime/debug/output_analyzer.{h,cpp}` — new `BandCorrelations` struct; analyze_av_reactivity extended with per-chunk FFT + 9 band correlations
- `src/runtime/debug/capture_coordinator.cpp` — JSON serialization for the new nested fields
- `tests/integration/test_output_analyzer.cpp` — 2 new test cases:
  - **Opposing envelopes**: bass ramps UP while treble ramps DOWN simultaneously; brightness tracks the bass. Expected: bass→brightness ≈ +1.0, treble→brightness ≈ −1.0. Both pass.
  - **Selective coupling**: bass present throughout, treble burst in the 2nd half; motion only in the 2nd half. Expected: treble→motion strong positive, bass→motion materially weaker. Both pass.
- `mcp/vivid_mcp.py` — `diagnose_composition_issue` extended with new rule:
  - When overall correlation on an axis is < 0.3 but a specific band's correlation exceeds 0.5, surface as an info-level finding ("band-selective coupling — may be intentional"). Value-neutral observation.
  - `analyze_output` docstring rewritten to describe all three metric lenses (overall correlation, onset response, per-band).

### Test design note

First pass of the tests used a single 80 Hz sine with a linear amplitude envelope. All three band correlations came out at 1.0 — correct behavior, because FFT spectral leakage plus numerical noise puts some energy in every band, and every band's magnitude scales with the shared envelope. Test redesigned to use signals where the bands differ *in time* (opposing envelopes, selective bursts) so the per-band correlations actually diverge. Worth noting for future tests.

## 2. YouTube multi-frame thumbnail extraction

### What shipped

`fetch_reference(url)` now downloads up to 5 thumbnails for YouTube URLs:
- `maxresdefault.jpg` (cover frame, highest resolution, fallback to `hqdefault.jpg`)
- `0.jpg` (mid-point of the video)
- `1.jpg`, `2.jpg`, `3.jpg` (spaced through the video)

Returns all paths in a new `frame_paths: list[str]` field. Partial failures are tolerated — whatever succeeds gets returned.

### Why this matters for reference translation

Most YouTube references have temporal style variation the cover frame can't show — palette shifts across scenes, motion-density changes, sections that are visually dense vs sparse. Having 5 frames spaced through the video gives the LLM enough sample points to characterize temporal variation, which is crucial for "inspired-by" translations where you're trying to capture the *character* of the piece, not just one frame.

The LLM decides how to use the frames — pick a few for `analyze_image`, describe variation across them, etc.

### Files touched

- `mcp/vivid_mcp.py` — `fetch_reference` YouTube branch extended; docstring updated
- `mcp/test_vivid_mcp_perception.py` — 2 new tests (full 5-frame success path; partial download where only cover + /1.jpg succeed)

## 3. `list_reference_graphs` — post-pivot catalog tool

### What shipped

A new MCP tool that walks `graphs/` in the repo, parses each `.json`, runs the in-process pattern detector, and returns a catalog. Value-neutral per the pivot — no quality tier, just auto-detected mechanical patterns + graph metadata.

Filter arguments:
- `pattern_filter` — only graphs exhibiting a pattern_id (drum-driven-pulse / continuous-reactivity / parametric-sync / spectral-color)
- `subdir_filter` — only graphs under `graphs/<subdir>/` (intro / audio / gpu / filters / io / media)
- `tag_filter` — only graphs whose `meta.tags` contains a tag
- `include_packages` — default False; skip graphs that require non-core packages

Sorted output: featured graphs first (by `meta.featured_rank`), then alphabetical by title.

### Why this replaces the original "reference corpus" task

The original plan called for ~20 graphs labeled by quality tier (`exemplar`, `solid`, `weak`, `broken`) with hand-written annotations. The pivot invalidated that — "good depends on project/user/precedent" means ranking graphs in isolation is the same category error we just walked back from.

Post-pivot design: *cataloging*, not *curating*. The tool surfaces what's in the graph directory, auto-detected, so Claude can browse by pattern when translating a reference. No aesthetic judgment; the user's precedent provides that.

### Live check

55 core graphs catalogued across 5 subdirs (intro/audio/gpu/filters/media), with pattern detection returning sensible results:
- `showcase_demo` → drum-driven-pulse
- `audio_reactive_demo` → continuous-reactivity
- `av_demo` → parametric-sync
- `star_spin_demo` (gpu/) → continuous-reactivity
- Many graphs return `patterns_detected=[]` — they're pure-visual LFO loops or audio-only patches that don't match any of the four mechanical templates. That's correct; the detectors are strict.

### Files touched

- `mcp/vivid_mcp.py` — `list_reference_graphs` tool + `_graphs_root`, `_load_reference_graph` helpers
- `mcp/vivid_mcp.py` instructions string — "Translate" step now mentions `list_reference_graphs` as a starting-point lookup in the reference workflow
- `mcp/test_vivid_mcp_perception.py` — 4 new tests (returns intro set, pattern filter, subdir filter, package exclusion)

## Test status

- `test_output_analyzer` (C++): 39 assertions, all PASS (was 35; added 4 for per-band)
- `test_vivid_mcp_perception` (Python): 61 tests, all PASS (was 57; added 4 for list_reference_graphs)
- Runtime rebuild: clean
- Full python test suite ran in 0.288s

## Status of the broader roadmap

**Phase 1 — perception loop:** complete.
- ✅ Settling delay handling
- ✅ Multi-axis correlation (brightness / motion / contrast)
- ✅ Onset response rate + reactivity latency
- ✅ Per-band correlation (this slice)

**Phase 2 — operators + polish:** complete for AV-coupling basics.
- ✅ Envelope follower (Smooth alias for EnvelopeFollower + factory presets + fixed showcase_demo + intro audit)
- 🟡 `OnsetDetector` graph operator — still pending, but lower priority given Smooth + drum peak ports handle the common cases

**Phase 3 — composition knowledge:**
- ✅ Composition guide (reframed as mechanical primitives + reference workflow)
- ✅ `diagnose_composition_issue`
- ✅ `get_composition_patterns` (reframed as mechanical templates)
- ✅ `explain_graph_composition`
- ✅ `fetch_reference` (with YouTube multi-frame)
- ✅ `compare_output_to_reference`
- ✅ `list_reference_graphs` (this slice — post-pivot catalog)
- ✅ MCP "References first" instructions

## Ready for the test run

The full reference-translation loop is now in place:

```
User: "build me something like <YouTube URL>, imitate closely"
↓
Claude: fetch_reference(url)                       # returns 5 frame paths
        analyze_image(frame_paths[N]) for each     # style tags, palette
        list_reference_graphs(pattern_filter=...)  # find starting templates
        load_graph(exemplar)  or  build from scratch
        analyze_output(mode="av") to read metrics
        compare_output_to_reference(reference_path)  # visual side-by-side
        diagnose_composition_issue(intent=...)       # flag mechanical issues
        iterate until user says it matches
```

Ready to exercise on a real task. Known instruction gaps can be fixed on the fly in the MCP server instructions string — the harder infrastructure work is done.
