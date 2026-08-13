# ADR-0059: The Site Teaches Through Fun, Motion-First Tutorials

Status: proposed

Date: 2026-08-12

## Context

ADR-0055 places Learn in the site's IA and folds the existing tutorials under it, but treats them as
"kept as-is." That understates their importance. For the site's audience — partners, press, and
notable artists whose first job is to *grasp the vision* (ADR-0055) — the moment right after "I get
it" is "could *I* make this?" A clear, fun getting-started path is what turns the manifesto into
belief, and it is disproportionately persuasive to artists deciding whether to invest their time.

The current state does not serve that at all:

- The site's `/tutorials/` (`templates/tutorials.html`, `content.json` `tutorials[]`) is three cards
  that **link out to GitHub** — no on-site body, no designed experience. The site is a thin index over
  the repo.
- All three tutorials — *First MCP-native project*, *Live shader edit*, *Project C++ operator* — are
  **code/MCP-focused and advanced** (they exist as ADR-0040 product-proof/friction tests). The
  in-repo set (`examples/tutorials/`) matches: `mcp-native-first-project`, `live-shader-edit`,
  `project-cpp-operator`. There is **no gentle, GUI-first "make your first audio-reactive visual in
  five minutes"** anywhere.
- Nothing shows the *result* of a step. Learning an audiovisual instrument from static text is like
  learning an instrument from sheet music with no sound.

So the front door to the product is a set of advanced code exercises hosted on GitHub. That is the
wrong first lesson for a musician or VJ, and it wastes the vision the homepage just delivered.

This ADR makes getting-started tutorials a first-class, designed part of the site. It is a companion
to ADR-0056 (homepage), ADR-0057 (gallery/proof), and ADR-0058 (brand), under the ADR-0055 anchor.

## Decision

**Getting started is an on-site, motion-first, GUI-first guided tour — clear, fun, and hand-crafted
for the web.** The tutorials are a designed experience, not an index of links.

### On-site and authored for delight

The core getting-started tutorials live **on the site** as designed pages (`site/tutorials/<slug>/`),
hand-authored for clarity and delight rather than generated from the repo's code-friction scripts.
They are optimized for persuasion and confidence-building, with a warm, encouraging voice and quick
wins. This is a deliberate split from ADR-0038's "generate from metadata" rule: reference *facts* are
generated; a *first lesson* is a crafted experience.

### Motion-first: show, don't tell

Every step pairs **one short instruction with a 3–5s looping clip (or GIF) of that exact action** —
the click, and the audiovisual result it produces. Motion is the point: the visitor sees sound move
the picture, step by step. The step clips are **captured from the real app** through the existing
showcase capture pipeline (`site/scripts/*`, ADR-0056), so they are **regenerable artifacts, honest
to the product** (the ADR-0037 discipline) even though the surrounding tutorial is hand-authored. A
still frame is the graceful fallback when a clip is unavailable (reusing the shipping player).

### GUI-first, graduating to code and AI

The path mirrors the manifesto's four verbs and starts with **zero code**:

1. **Meet Vivid / make a sound** — the shortest possible first win.
2. **Make a visual** — get something on screen.
3. **Make it react** — map sound to picture; the "aha."
4. **Perform it** — play it live (Session View, scenes).
5. **Author your own** — *now* graduate to writing an operator with code and AI over MCP — the
   "Author" reveal from ADR-0055/ADR-0040, arriving after the learner already feels capable.

The first four require no toolchain and no plugins beyond the curated beginner instrument
(ADR-0036/ADR-0040). The GUI-first-then-MCP shape means a musician succeeds before ever seeing a code
editor, then discovers the deep authoring loop as a superpower, not a barrier.

### A designed learning path

A short, numbered series (not a sprawling doc set) with a consistent template: goal · time · prereqs
→ steps (instruction + clip + a "✓ you should see/hear…" checkpoint) → recap → "next →". Fun comes
from motion, a friendly voice, and fast wins — the feeling of an instrument you *play*, not software
you configure.

### Content model and relationship to the repo tutorials

On-site tutorials become data-driven bodies (extend `content.json` / a tutorial-content shape with
per-step text + media) rendered by the site templates; `_self_check` gates their step media like it
gates showcase heroes. The in-repo `examples/tutorials/` remain the **runnable, acceptance-tested**
code tutorials (ADR-0040 proofs) and the destination the beginner series graduates into — the web
series is the polished, motion-first *front door*; the repo series is the tested *reference*.

## Alternatives Considered

- **Keep the index that links out to GitHub.** Rejected — it bounces the visitor off the site into
  raw scripts, shows no result, and offers no beginner path. This is the current state and the exact
  gap this ADR closes.
- **Generate on-site tutorials from the repo scripts (like the Operator Reference).** Rejected as the
  primary approach: the repo tutorials are code/friction-test-shaped and advanced, not beginner-fun,
  and generation would inherit their tone. Honesty is preserved instead by capturing step *clips* from
  the real app, so authored prose can be delightful without drifting from real behavior.
- **Static annotated screenshots only.** Rejected as the primary medium — a still cannot show sound
  driving the picture, which is the whole lesson. Stills remain the fallback when a clip is missing.
- **A guided in-app first-run (coach-marks) as the main tutorial.** Deferred — immersive, but it is
  app work, not site work, and a larger build; recorded as a future upgrade the site can point into.
- **An interactive in-browser lesson (do step one before installing).** Deferred — highest wow, but a
  significant custom web build; a natural sibling to ADR-0056's deferred WebGL hero toy.

## Consequences

- A **new beginner, GUI-first tutorial series** must be authored (content + captured per-step clips) —
  real content and media production, but reusing the showcase capture pipeline and the shipping HLS
  player, so no new web runtime.
- `content.json` and the tutorial templates grow a per-step body + media shape; `_self_check` gates
  the step media, keeping the build honest.
- **Drift is the maintenance risk:** hand-authored tutorials can fall out of date as the app changes.
  Mitigations: keep the series small and canonical, and regenerate step clips from the real app so a
  recapture surfaces drift (the ADR-0037 artifact discipline doing double duty).
- The existing three code tutorials are re-homed as the "graduate to code + AI" tier under Learn,
  giving them a clear place without diluting the beginner path.
- Motion-heavy pages must stay fast: short loops, lazy-loaded media, and `prefers-reduced-motion`
  respected per ADR-0058.
- Getting started becomes a headline feature of the site, not a footnote — matching the emphasis this
  audience actually rewards.

## References

Related: ADR-0055 (the website is a manifesto for a new instrument — this fills out its Learn IA),
ADR-0056 (the homepage hero and narrative — same motion-first, capture-pipeline approach), ADR-0058
(the brand and visual system — motion discipline and page performance), ADR-0037 (showcase demos gate
the real website — step clips are regenerable artifacts), ADR-0040 (MCP-native creative coding is the
public promise — the GUI-first path graduates into it), ADR-0036 (curated free-plugin path for the
beginner instrument), ADR-0035 (the first-run beginner project). Companion to ADR-0057 (the gallery
and social-proof system).
