# Expanded family/member browser

## What it is

v1 ships a compact 6×8 grid of family × member cells. It's dense, fast to click, and works — but it doesn't tell the user anything about *what each wavetable sounds like* before they select it.

The expanded browser replaces (or augments) the grid with an **expandable family tree** where each member row includes a **miniature preview glyph** — a tiny polyline showing the wavetable's characteristic shape. Like a "waveform thumbnail" in every row.

```
▼ AnalogWarm
    Core    ╱╲_╱╲_    (sine-like)
    Soft    ╱‾╲‾╲_    (soft square)
    Rich    ╱\/|\_    (additive stack)
    …
▶ BrightDigital
▶ VocalFormant
…
```

Clicking a member row selects (family, member). Clicking a family header expands/collapses.

## Why deferred from v1

Two reasons:
1. **Rendering per-row waveform glyphs** means sampling each of the 48 wavetables every frame (or caching). Not hard but non-trivial; the v1 grid defers this to the full preview for the *selected* table.
2. **The grid already works for the common case**. Users who've used the operator a few times know which families sound like what; the visual glyphs are a learn-mode feature rather than a fluent-mode one.

v1 bets on fluent UX (compact grid, one click to select). The expanded browser is an onboarding upgrade.

## Engine cost

**Zero**. Same `sample_level()` path we already use; just called 48× per frame instead of 1×.

Performance: each glyph is maybe 64 samples × one frame = 3072 sample_level calls per frame if done naively. At 60fps that's ~180K/sec — fine, but we'd cache per-(family, member) and invalidate only when the builtin table set changes (which is effectively never after init).

## Editor cost

**~4 hours**:
- Browser layout — scrollable list with collapsible family sections. New idiom; doesn't exist in any current editor. Probably 150 lines of list-rendering + scroll handling (reusing `ui_scroll_region_*` from `editor_ui.h`).
- Per-row glyph rendering — 60 lines. Each glyph is a polyline in the row's right margin.
- Cache structure — `std::array<std::vector<float>, 48> member_glyph_cache_` on the core. Populated lazily on first draw; invalidated on `prepare_instance_assets` (which currently just warms the builtin table singleton).
- Click handling — hit-test each row; expand/collapse family headers; set params on member click.

Replace the v1 grid with the browser, OR keep the grid as a compact alternative (Tab toggles between views). Starting with replace is simpler.

## Interactions

- **[frame-stack-visualization](frame-stack-visualization.md)** — uses the same sample cache. If frame-stack ships first, its cache is reusable here.
- **Polish: browser favorites** — pin favourite members to the top. Needs persistent per-node state. Mentioned in the top-level README.

## Scope cuts

- **Search/filter box**: type to filter visible members. Nice but 48 rows isn't really search territory.
- **Tag-based grouping**: "warm", "bright", "metallic" as cross-cutting tags. Would need a new param-metadata channel. Defer indefinitely.
- **Member rating/favourites**: star a member to promote it. Needs persistent state; save for later.
- **Import custom banks**: drop a bank file (many `.wav`s in one archive). Depends on [file-drop-import](file-drop-import.md) landing first.

## Test plan

- Pure-logic: glyph-sampling against golden values for a known builtin table.
- End-to-end: click on row 17 → captured set_param for the expected (family, member) pair. Family-header click → internal expanded state toggles.
- Cache behaviour — don't test explicitly; it's a transparent optimization.
