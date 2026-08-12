# Vivid Tutorials

Two tracks. **Start with the learning path** if you're new — it's GUI-first and teaches the interface
by doing, one concept at a time, then graduates you to driving Vivid over MCP. The **advanced
follow-ups** assume you already have the mental model.

## Learning path (new users — do these in order)

GUI-first: you'll click through the real interface. Each tutorial ends with a **"Try it with MCP"**
aside so that by the end you can drive the same moves from an agent / the control server.

| # | Tutorial | You'll learn | ~time |
|---|----------|--------------|-------|
| 01 | [Meet Vivid](01-meet-vivid/) | The two surfaces (DAW ↔ visual graph) + the bridge; tour a finished piece | 10 min |
| 02 | [Your first sound](02-first-sound/) | Add a track, load an instrument, make a clip, hit play | 10 min |
| 03 | [Your first visual](03-first-visual/) | The node graph: add a generator, wire it to Output, tweak params | 10 min |
| 04 | [Make it react](04-make-it-react/) | The bridge: map an audio characteristic to a visual param | 10 min |
| 05 | [Perform it](05-perform-it/) | Scenes as sections; launch them live to build an arrangement | 10 min |
| 06 | [Make it yours](06-author-an-operator/) | Author your own operator when a built-in doesn't fit (Vivid's core move) | 15 min |
| 07 | [Save & share](07-save-and-share/) | Save a portable project; export a video | 10 min |

By the end you understand the instrument end-to-end **and** can do all of it from MCP.

## Advanced follow-ups

- [`mcp-native-first-project/`](mcp-native-first-project/) — build a whole project MCP-native,
  shader-first (assumes Surge XT). The agent-driven version of the path above.
- [`live-shader-edit/`](live-shader-edit/) — hot-edit a project-local `.glsl` while the project runs.
- [`project-cpp-operator/`](project-cpp-operator/) — author a C++/GPU operator (the deeper end of #06).

## Reference

- [`free-plugin-starter-list.md`](free-plugin-starter-list.md) — curated free plugins to install.
- Full operator reference: <https://vivid.seethroughlab.com/reference/>.

---

### Tutorial template (for authors)

Every learning-path tutorial follows the same shape so the set reads consistently:

1. **Goal + time + prerequisites** (one line each).
2. **What you'll build** (2–3 sentences; a screenshot of the end state).
3. **Steps** — numbered GUI actions (`Menu > Item`, click, drag). Each ends with a
   **✓ You should see/hear:** checkpoint so the reader can self-verify.
4. **Try it with MCP** — the same move via the control server / an MCP tool, so the reader graduates
   toward automation.
5. **Recap** — the concepts introduced, in one list.
6. **Next** — a link to the following tutorial.

Screenshots live in each tutorial's `img/` folder; keep them current with the shipping UI.
