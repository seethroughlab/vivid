# Blocker and Non-Blocker Class Definitions

## Blocker Classes

Issues that block the beta from shipping. Every blocker must be fixed or the affected surface must be removed from the beta path.

| ID | Class | Description | Example |
|----|-------|-------------|---------|
| B1 | Crash | App crash, SIGABRT, SIGSEGV on any code path a beginner can reach | Loading an intro graph crashes |
| B2 | Hang | Unrecoverable freeze requiring force-quit | Audio thread deadlock on graph swap |
| B3 | Launch failure | App fails to start on supported macOS versions | Missing dylib on clean install |
| B4 | Graph load failure | Any graph in `graphs/` fails to load (missing type, schema error) | Operator not registered |
| B5 | Missing core operator | A `VIVID_REGISTER`'d operator is absent from the built binary | Build config omits an operator |
| B6 | WebGPU validation error | Dawn/wgpu validation error during normal operation | Texture size mismatch in shader |
| B7 | Audio device lockup | Audio engine enters unrecoverable state | Device sample rate change kills output |
| B8 | Persistent silence | Audio graph produces no audible output when it should | Oscillator connected but gain is 0 by default |
| B9 | Black/frozen visual | GPU graph produces black or frozen output when it should animate | Shader compile failure silently degrades |
| B10 | Scary audio | Runaway feedback, extreme clipping, stuck notes, painful volume spikes | No limiter on a feedback path |
| B11 | Broken save/load | Save, load, or variation recall corrupts or loses graph data | Variation recall drops a connection |
| B12 | Beginner-blocking docs | Setup docs that prevent a non-programmer from launching the app | "Run cmake" with no context or prerequisites |

## Non-Blocker Classes

Issues that can ship with the beta as known limitations. Document them clearly and file follow-ups.

| ID | Class | Description | Action |
|----|-------|-------------|--------|
| N1 | Cosmetic wording | Typo, awkward phrasing in non-critical UI text | File follow-up, do not block |
| N2 | Layout polish | Minor inspector spacing, alignment, or sizing issues | File follow-up |
| N3 | Advanced rough edges | Issues in features not on the beginner path (e.g. advanced modulation, export) | Document in known limitations |
| N4 | Env-dependent skip | Feature requires hardware/software the tester may not have | Label in environment-labels.md, skip test |
| N5 | Package-dependent skip | Graph requires a package not installed in the beta | Label in inventory, skip test |
| N6 | Performance | Slow but functional on supported hardware | Document minimum specs in known limitations |

## Decision Rules

1. **First 15 minutes rule.** If a beginner can encounter the issue during their first session following the getting-started path, it is a blocker.

2. **Hardware absence.** If the issue only manifests when specific hardware is missing (camera, MIDI controller, etc.), it is N4 — unless the absence causes a crash or gives no indication of what went wrong, in which case escalate to B1 or B3.

3. **Graceful degradation.** If the degraded path (missing device, missing media) is ugly but functional and informative, it is N2. If the degraded path is silent, black, or confusing with no recovery guidance, it is a blocker.

4. **Intro path priority.** Graphs in `graphs/intro/` and operators they use are held to a higher standard than advanced examples. Cosmetic issues in intro graphs are blockers; cosmetic issues in advanced graphs are N1-N2.

5. **Env-dependent graphs in non-intro folders.** These are N4 by default as long as the example browser does not present them as starter content. If a beginner could reasonably open one expecting it to work out of the box, escalate.
