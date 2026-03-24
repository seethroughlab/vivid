# Internationalization (i18n) Roadmap

This document outlines the three tiers of non-English language support for Vivid's GPU text renderer (`Renderer2D`). Each tier builds on the previous one.

## Current state

The renderer bakes a glyph atlas at startup using `stb_truetype`. ASCII 32-126 plus 12 extra Unicode codepoints (arrows, triangles, em dash, etc.) are rasterized into a 1024x1024 R8 atlas. The `init()` function accepts optional extra codepoints, so callers can extend the baked set without editing the renderer.

Font: JetBrains Mono (Latin, Cyrillic, Greek coverage).

## Tier 1: European languages (Cyrillic, Greek, accented Latin) -- DONE

**Implemented:** The renderer scans the entire BMP (U+0080 through U+FFFF) at startup and bakes every glyph the font supports. This avoids maintaining explicit codepoint range lists -- adding a new font with broader coverage automatically bakes more glyphs.

The existing `bake_codepoint` lambda handles deduplication (UI symbols baked earlier are skipped), empty glyphs (codepoints the font doesn't cover), and atlas overflow (warning + skip). JetBrains Mono covers Latin Extended, Cyrillic, and Greek, resulting in ~1000-1500 baked glyphs total.

**Risk:** At high DPI (3x), glyph bitmaps are larger and may overflow the 1024x1024 atlas. The overflow handler degrades gracefully (skips remaining glyphs with a warning). If this becomes an issue, bump to 2048x2048.

## Tier 2: CJK (Chinese, Japanese, Korean)

**Effort: significant architectural change.**

CJK has 20,000+ commonly-used glyphs. They won't fit in a single atlas at readable sizes.

**Options:**

1. **Dynamic glyph cache (recommended):** Bake glyphs on demand into the atlas. Maintain an LRU cache -- when a codepoint is first seen, rasterize it and pack it in. If the atlas fills up, evict least-recently-used glyphs and re-upload. This is how most production GPU text renderers work (ImGui's atlas builder, Skia, etc.).

2. **Multiple atlas pages:** Keep multiple textures, switch bind groups per-glyph. Simpler to implement but more draw calls.

3. **Larger atlas:** 2048x2048 or 4096x4096 could hold ~2000-4000 CJK glyphs at 16pt. Pragmatic for apps with bounded vocabulary but not general-purpose.

**Font requirements:** JetBrains Mono has no CJK glyphs. This tier requires **font fallback** -- try the primary font, fall back to a CJK font (e.g. Noto Sans CJK, ~16MB) for missing glyphs. `stbtt_FindGlyphIndex()` returns 0 for missing glyphs, which is already the signal to try the fallback.

**What changes:**
- Atlas baking becomes dynamic (on-demand rasterization + LRU eviction)
- `lookup_glyph` gains a cache-miss path that rasterizes and packs
- Atlas texture may need partial re-upload support
- Font fallback chain (primary font -> CJK font)

**What stays the same:**
- `draw_text`, `text_width`, `wrap_text` -- these already go through `lookup_glyph`, so the dynamic cache is transparent to them

## Tier 3: Complex scripts (Arabic, Hebrew, Devanagari, Thai)

**Effort: major.**

These scripts require fundamentally different text processing:

- **Text shaping (HarfBuzz):** Letters change form based on context (Arabic initial/medial/final forms), ligatures are mandatory, vowel marks attach to consonants. The current character-by-character rendering loop cannot handle this.

- **Bidirectional text (ICU or FriBidi):** Arabic/Hebrew flows right-to-left, but embedded numbers and Latin text flow left-to-right. Correct rendering requires the Unicode Bidirectional Algorithm.

- **Cluster-aware line breaking:** Can't break in the middle of a grapheme cluster. The current byte/codepoint-level wrapping logic in `wrap_text` would need to become cluster-aware.

**What changes:**
- The character-by-character rendering loop is replaced with a shaped-glyph-run model
- HarfBuzz integration for shaping
- FriBidi or ICU for bidi resolution
- Line breaking becomes cluster-aware
- Font fallback becomes more complex (shaping must happen per-run, not per-glyph)

**What stays the same:**
- The GPU pipeline (vertex format, atlas texture, shaders) is unchanged
- `push_quad` and the batching/clipping system are unaffected

## Recommended sequence

| Priority | Tier | Trigger | Depends on |
|----------|------|---------|------------|
| Done | Configurable `init()` | -- | -- |
| Done | Tier 1: European | Full BMP scan bakes all font-supported glyphs | -- |
| Done | String management | `T()` macro + JSON locale files | -- |
| Later | Tier 2: CJK | CJK localization effort | Dynamic glyph cache + font fallback |
| Much later | Tier 3: Complex scripts | RTL/Indic localization | HarfBuzz + bidi + Tier 2 infrastructure |

## String management layer

Complements the glyph tiers above by handling the *content* side of localization: replacing English UI strings with translations.

### Design

A lightweight `T("key", "English fallback")` macro returns `const char*`, matching every existing `draw_text` callsite with zero signature changes. At startup, an optional JSON locale file is loaded; if absent (or if a key is missing), the English fallback is returned directly.

- **`src/ui/i18n.h`** — `I18n` singleton class + `T()` / `T_PLURAL()` macros
- **`src/ui/i18n.cpp`** — JSON loader (nlohmann/json), map lookup, plural support
- **`locales/en.json`** — canonical English string table (template for translators)

### What is translated

| Category | Example | Approach |
|----------|---------|----------|
| Fixed UI labels | "Save", "Cancel", "Resolution" | `T("save", "Save")` |
| Format strings | "Delete %d Nodes" | `T("delete_n_nodes", "Delete %d Nodes")` + snprintf |
| Status messages | "Fetching catalog..." | `T("fetching_catalog", "Fetching catalog...")` |

### What is NOT translated

- Operator/parameter names from `NodeSnapshot`/`ParamInfo` (data identifiers from plugins)
- Unicode symbols (`"▾"`, `"!"`, `"R"/"G"/"B"` channel labels)
- Placeholder text in input fields (`"operator_name"`, `"preset_name"`)
- Legal/copyright text, library names
- Debug/log strings not shown to users

### Plurals

`T_PLURAL("key", n, "singular", "plural")` picks by `n == 1` for English. Locale files can override with `key_one` / `key_other` variants.

### Adding a new locale

1. Copy `locales/en.json` to `locales/<lang>.json`
2. Translate values (keys stay the same)
3. Set `"locale": "<lang>"` in the file
4. Call `I18n::instance().load("locales/<lang>.json")` at startup
