Vivid is a signed macOS app, published through GitHub Releases. Install is step zero of the first
tutorial — you can't do the tutorial without the app, and the tutorial is the front door.

## 1. Download Vivid

Grab the latest signed macOS build from the [releases page](https://github.com/seethroughlab/vivid/releases/latest),
open the `.dmg`, and drag **Vivid** into `/Applications`. The build is code-signed and notarized, so
it opens without a Gatekeeper detour.

## 2. Install the beginner instrument (Surge XT)

The first tutorial needs exactly one free plugin: **Surge XT**, an open-source synth. Install it and
Vivid will find it automatically. See [Free Plugins](/free-plugins/) for the download link and the
install path Vivid expects. Nothing else is required to start.

## 3. Build your first project

Launch Vivid, then run the [First MCP-native project](https://github.com/seethroughlab/vivid/tree/main/examples/tutorials/mcp-native-first-project)
builder — a short `uv run build.py` script that drives the running app over MCP to author the project
for you. In about ten minutes you'll have:

- a **Surge XT track** with a short MIDI clip,
- a **project-local shader** wired into the visual graph, and
- **audio-to-visual mappings** so the picture moves with the sound —

all saved as one portable project you can reload, inspect over MCP, and fork.

## 4. Keep going

- [All tutorials](/tutorials/) — the beginner path, plus creative-coding on shaders and C++ operators.
- [Showcase](/showcase/) — five finished projects, each a saved project you can regenerate.

> Every tutorial project doubles as a regression case: if the product can't support a step, that's a
> bug to fix, not a paragraph to soften.

## Photosensitivity note

Vivid renders **audio-reactive visuals** that can flash, strobe, or move rapidly — especially on
loud or percussive material. If you or anyone viewing your work is sensitive to flashing lights,
keep the output window small, lower reactive intensity, or avoid fast-flashing looks. Vivid does not
yet limit flash rate.
