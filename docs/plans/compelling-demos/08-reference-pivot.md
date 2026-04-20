# Phase 3 — Reference-Driven Composition Pivot

**Date:** 2026-04-20
**Scope:** Reorient Phase 3 away from baked-in "compelling patterns" toward a reference-translation workflow. The compositional target comes from the user's external precedent, not from a curated catalog.

## Why the pivot

The earlier `get_composition_patterns` direction assumed a universal notion of "compelling AV." That assumption doesn't hold — good depends on project, user, and specific precedent. A meditation app wants ambient; a club visualizer wants punchy; an Ikeda study wants something else entirely. Positioning built-in patterns as aesthetic targets was a subtle category error.

**Reframe:** Claude is a *translator* between external precedents and Vivid's operator vocabulary. The user supplies the reference — URL, YouTube, image, audio track, artist name, text description. Claude extracts structural/stylistic properties via `analyze_image`/`analyze_track` (already existed in `vivid_analysis_mcp.py`), translates to operator choices using its own judgment, builds, and iterates by comparing output to reference.

The composition guide and pattern library survive but get reframed as **mechanical primitives** — vocabulary about *how* to wire things cleanly, not *what* to build. Like music theory scales.

## Division of responsibility

**Instructions (CLAUDE.md + MCP server instructions)** — conventions and workflow:
- Open AV tasks by asking for a precedent
- Ask HOW the reference should be used (imitate / inspired-by / style-only / opposite-of)
- Workflow: `fetch_reference` → `analyze_image`/`analyze_track` → translate → build → `compare_output_to_reference` → iterate

**MCP tools** — data access and actions:
- Keep (mechanical, value-neutral): `diagnose_composition_issue`, `analyze_output`, graph mutation tools, `capture_image`
- Reframe (same data, new framing): `get_composition_patterns` — now reads as "mechanical templates," not "curated good patterns"
- Already existed: `analyze_image`, `analyze_track`, `analyze_reference_folder` on the `vivid-analysis` MCP server
- **New:** `fetch_reference(url)` — YouTube / webpage / direct-image URL ingestion with local cache
- **New:** `compare_output_to_reference(reference_path)` — pairs current capture with reference image (no automated scoring; LLM is the judge)
- **New (pre-pivot closeout):** `explain_graph_composition(graph_path)` — detects which mechanical patterns a graph exhibits (reverse of `get_composition_patterns`)

**Not built (avoided):** a "style vocabulary" tool mapping aesthetic tags to operators. That's prescriptive hardcoding in a different costume. The LLM does the translation using its own knowledge; the catalog is the vocabulary.

## What shipped

### Code

| File | Change |
|---|---|
| `mcp/vivid_mcp.py` | New tools: `fetch_reference`, `compare_output_to_reference`, `explain_graph_composition`. New helpers: `_youtube_video_id`, `_download_to_path`, `_fetch_url_text`, `_og_and_title_from_html`, `_index_graph`, pattern detectors. |
| `mcp/vivid_mcp.py` server instructions | Front-loads "References first" workflow block + reference-to-graph sequence |
| `mcp/test_vivid_mcp_perception.py` | 16 new tests covering the three new tools (56 total, all pass) |

### Docs

| File | Change |
|---|---|
| `docs/COMPOSITION-GUIDE.md` | New opening section "Translating references into Vivid" (7-step workflow). Intro reframed: guide is mechanical primitives, not aesthetic targets. "Common patterns" renamed to "Mechanical templates." Metric-threshold framing tightened: "mechanically-working," not "compelling." |
| `CLAUDE.md` | Documentation Map entry for `COMPOSITION-GUIDE.md` repositioned to reflect the pivot |
| `~/.claude/projects/.../memory/project_compelling_demos.md` | Current state updated with the pivot framing and tool list |
| `docs/plans/compelling-demos/08-reference-pivot.md` | This doc |

## Tool summary

### `fetch_reference(url, refresh=False)`
- **Input:** http(s) URL (YouTube, project page, direct image)
- **Behavior:** Classifies URL kind, downloads a representative image to `~/.cache/vivid/references/<hash>.{jpg,png,...}`, scrapes `<title>` and `<meta name="description">`/`og:description` and `og:image`. For YouTube, extracts video ID and tries `maxresdefault.jpg` → `hqdefault.jpg`. Cached; `refresh=True` forces re-fetch.
- **Output:** `{ok, source_url, source_kind, title, description, thumbnail_local_path, text_summary, cached}`
- **Next step:** `analyze_image(thumbnail_local_path)` for palette + style tags.

### `compare_output_to_reference(reference_path, save_dir="")`
- **Input:** absolute path to reference image (usually from `fetch_reference` output)
- **Behavior:** Calls `capture_frame` on the runtime, decodes the PNG, writes alongside the reference. Intentionally performs no automated comparison — returns paths only.
- **Output:** `{ok, capture_path, reference_path, hint}`
- **Use:** Claude reads both paths as images (via the built-in Read tool), describes differences, suggests param adjustments.

### `explain_graph_composition(graph_path)`
- **Input:** path to a Vivid graph JSON
- **Behavior:** Parses the graph, detects which mechanical patterns it exhibits via structural signatures (drum-peak → Smooth → shape-scale; gain.rms → visual param; single LFO forked to both domains; FFTAnalysis → color target). Each detection has a confidence (high/medium/low) and the specific node chains that matched.
- **Output:** `{ok, graph_path, meta, summary, patterns_detected, notes}`
- **Use:** understanding an unfamiliar graph; auditing for mechanical patterns; scaffolding annotations.

## Verification

### Tests
- `mcp/test_vivid_mcp_perception.py`: 56 tests, all pass.
  - 7 cover `explain_graph_composition` (drum-driven pulse at high confidence with both axes, low confidence + warning for missing envelope, continuous reactivity, parametric sync, empty graph, missing file, invalid JSON)
  - 6 cover `fetch_reference` (YouTube ID parsing across 5 URL shapes, rejection of non-http, cache hit path, YouTube download + metadata, direct image, generic webpage OG scrape)
  - 3 cover `compare_output_to_reference` (happy path with mocked `capture_frame`, missing reference, capture failure)

### Live spot-check
`explain_graph_composition` against real intro graphs:
- `showcase_demo.json` → `drum-driven-pulse` at `confidence=high`, 3 chains (kick/snare/hat). Correct — this is the Smooth-fixed version.
- `audio_reactive_demo.json` → `continuous-reactivity` at `confidence=high`, 1 chain (gain.rms → displace.amount). Correct.
- `av_metronome_demo.json` → no patterns matched. Correct — uses separate LFOs synced to the graph metronome, which is parametric in *spirit* but not a forked single source (the detector only catches the explicit fork form).

### End-to-end workflow (to be exercised on a real task)
Success criterion: when the user next asks Claude to build an AV graph, Claude should:
1. Open by asking for a precedent without being prompted.
2. Ask how the reference should be used (imitate / inspired-by / etc.).
3. Call `fetch_reference` → `analyze_image`/`analyze_track` → articulate the extracted style in plain language.
4. Propose operator choices with reasoning grounded in the extracted descriptors.
5. Build incrementally; at each step, `compare_output_to_reference` to check alignment.
6. Iterate until the user says it matches the intent — not until some metric hits a threshold.

If any step still requires the user to prompt Claude through it, the MCP-server-instructions block needs tightening.

## Status of the broader roadmap

- **Phase 1 (perception loop)**: ✅ settling, ✅ multi-axis correlation, ✅ onset response rate. 🟡 per-band correlation (task 8, still pending — low priority now that the reference-compare loop covers aesthetic judgment).
- **Phase 2 (operators + polish)**: ✅ complete. 🟡 `OnsetDetector` graph operator still pending (nice-to-have, not blocking).
- **Phase 3 (composition knowledge)**:
  - ✅ Composition guide (reframed as mechanical primitives + reference workflow)
  - ✅ `diagnose_composition_issue` tool
  - ✅ `get_composition_patterns` tool (reframed)
  - ✅ `explain_graph_composition` tool
  - ✅ `fetch_reference` tool (new)
  - ✅ `compare_output_to_reference` tool (new)
  - ✅ MCP server "References first" instructions
  - 🟡 Reference corpus curation — deferred; requires user-in-the-loop labeling, revisit when there's concrete need

## What we avoided

- **Style-vocabulary tool** mapping aesthetic tags to operator choices. Tempting because it would make the LLM's job easier, but it would replicate the original category error: hard-coding *what's good* (just at a finer granularity). The LLM does the translation using its own knowledge of how, say, "Ikeda aesthetic" maps to operator choices — exactly as it would for any cross-domain translation task. The operator catalog is the vocabulary; the translation is a judgment call.

- **Automated aesthetic scoring.** `compare_output_to_reference` returns both paths, not a similarity score. A metric number here would be either misleading (pretending objectivity) or thin (LPIPS-style similarity ignores the "intent" dimension of the reference). The LLM is the judge.

- **Hard-coded tempo/density translations.** E.g., "if reference is 180 BPM, set metronome to 180." The LLM can do this straightforwardly from the `analyze_track` output; baking it into a tool would leak into edge cases (what if BPM detection is wrong, or the user wanted a different tempo for contrast).

## Open

- **Reference corpus curation.** When (if) needed: use `explain_graph_composition` to auto-scaffold annotations across `graphs/intro/` + other graphs, then the user rates and labels a tier per graph. Not blocked; just waiting on the judgment-is-yours step.
- **YouTube video-frame extraction.** Currently `fetch_reference` only grabs the thumbnail. If Claude needs to see multiple frames from a video (e.g., to extract temporal patterns), that's a new slice — probably via `yt-dlp` or similar. Not done; not urgent.
- **Caching hygiene.** `~/.cache/vivid/references/` grows unbounded. A future sweep could add LRU eviction; not urgent.
