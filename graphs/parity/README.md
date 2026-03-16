# Parity Fixture Graphs

Canonical fixture graphs for evaluating Vivid's audio-visual parity claim. Each graph is designed to stress-test a specific direction of cross-domain interaction using the rubric defined in `docs/internal/PARITY-RUBRIC.md`.

## Fixtures

### `parity_audio_first.json` — Audio-First Workflow

**Workflow:** Build a subtractive synth patch (Oscillator → Filter → Gain → AudioAnalysis → audio_out), then add visual response by routing AudioAnalysis multi-metric outputs (spectral_centroid, rms) into NoiseTexture speed/scale and Shape radius, with LFO cross-driving Shape rotation.

**Parity rationale:** Tests the audio→visual direction. The audio side should be a natural, self-contained patch. AudioAnalysis provides a symmetric multi-metric bridge (5 control-float outputs) that mirrors TextureAnalysis's role in the visual→audio direction.

**Operators used:** LFO, Oscillator, Filter, Gain, AudioAnalysis, NoiseTexture, Shape, Composite, Bloom, audio_out, video_out

**Known-good result:** A modulating synth tone whose visuals (noise field + pulsing hexagon) respond to audio spectral content and amplitude. Higher spectral centroid → faster noise animation. Louder audio → denser noise + larger shape. LFO sweep → shape rotation.

---

### `parity_visual_first.json` — Visual-First Workflow

**Workflow:** Build a GPU motion loop (Shape → Feedback → Bloom → video_out), then add audio response by routing TextureAnalysis outputs (brightness, edge_density) into Oscillator frequency and Filter cutoff.

**Parity rationale:** Tests the visual→audio direction. The visual side should feel complete as a standalone feedback loop. Adding audio that responds to visual structure should be comparably easy.

**Operators used:** LFO, Shape, Feedback, Bloom, TextureAnalysis, Oscillator, Filter, Gain, audio_out, video_out

**Known-good result:** A rotating star shape with feedback trails and bloom, whose brightness drives oscillator pitch (brighter → higher pitch) and edge density drives filter cutoff (more edges → brighter filter tone).

---

### `parity_cross_domain.json` — Cross-Domain-First Workflow

**Workflow:** Start with shared control sources (Clock → LFO and Clock → Envelope), then route both into audio (LFO → Oscillator frequency, Envelope → Gain amplitude and Filter cutoff) and visuals (LFO → Shape rotation and NoiseTexture scale, Envelope → Shape radius) simultaneously.

**Parity rationale:** Tests whether a single control layer can drive both domains symmetrically. The control hub should feel domain-agnostic — wiring to audio vs visual targets should be equally natural.

**Operators used:** Clock, LFO, Envelope, Oscillator, Filter, Gain, Shape, NoiseTexture, Composite, Bloom, audio_out, video_out

**Known-good result:** A clock-synced patch where the LFO smoothly modulates both pitch and visual motion, while the envelope provides rhythmic amplitude shaping in audio and size/filter pulsing in visuals. Both domains respond to the same beat structure.

---

## How to Rerun the Evaluation

1. Load each fixture graph in Vivid (`File → Open` or via MCP `load_graph`).
2. Verify audio and visual output are both active.
3. Score each fixture against the 5 rubric categories in `docs/internal/PARITY-RUBRIC.md`.
4. Classify any weaknesses using the gap taxonomy.
5. Record results in `docs/internal/PARITY-VALIDATION-REPORT.md`.
6. Update the PRD Conformance Scorecard if the overall parity judgment changes.
