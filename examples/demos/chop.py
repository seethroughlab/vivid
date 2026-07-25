"""Chop — a breakbeat SONG built by SLICING a drum break, authored over MCP (90 BPM).

Headlines a mechanism no other demo shows: take a real audio break, chop it into a **drum-rack
Sampler** with `slice_to_midi` (one ascending note per 1/16 slice), then **re-sequence** those
slices into new patterns per section — the classic breakbeat/jungle move. The drums are **fully
plugin-free** (the native Sampler + the bundled `media/break90.wav`); only the sub-bass uses Surge.

Not a loop: a five-section arrangement laid out as launchable SCENES — intro / verse / chorus /
bridge / outro — that you step through (`v.perform(...)` auditions the whole thing). The story:

  section   what plays                                       the chop
  INTRO      the RAW break (audio clip), untouched            — (source statement)
  VERSE      the drum-rack takes over                         a solid re-ordered groove
  CHORUS     the drum-rack, full energy                       busy 1/16 amen w/ a stutter fill
  BRIDGE     the drum-rack, half-time                         sparse, on the beat
  OUTRO      the drum-rack, resolving                         a tail

Each part drives its OWN identifiable visual through the bridge: every slice hit punches the frame,
the busy chorus smears the feedback trails, and the sub swells + turns the solid. Because a part only
plays in its sections, its visual only fires then — the picture writes itself from the music.

Requires Surge XT (free CLAP) for the sub-bass. The drums need no plugin — just the bundled break.
Run with the app running:  uv run examples/demos/chop.py   (append `perform` for the full song)
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "chop")
MEDIA = os.path.join(HERE, "media")
BREAK = os.path.join(MEDIA, "break90.wav")   # a real 1-bar/90-BPM break (bundled)
BPM = 90                                      # matches the break, so the 1/16 slices land clean


def build(v: Vivid, save: bool = True, perform: bool = False):
    v.reset()
    v.bpm(BPM)
    S_INTRO, S_VERSE, S_CHORUS, S_BRIDGE, S_OUTRO = v.scenes(
        ["intro", "verse", "chorus", "bridge", "outro"])
    v.launch_quantize(4)   # switch scenes on 4-bar phrase boundaries, not mid-clip

    # ================= the breakbeat: import -> warp -> slice -> re-sequence =================
    # 1) the raw break into an AUDIO (sampler) cell, warped to the project tempo. Plays in INTRO only.
    raw = v.add_track(kind="audio")
    v.import_audio(raw, S_INTRO, BREAK, src_bpm=90)
    v.warp(raw, S_INTRO, mode="beats")
    v.set_track_gain(raw, 0.8)

    # 2) CHOP it: slice the break into a new MIDI track driven by a native Sampler loaded with the
    #    slices (mode 3 = 16-step grid). Returns the new "break90 Slices" drum-rack track index.
    dt = v.call("slice_to_midi", track=raw, scene=S_INTRO, mode=3)["track"]
    v.set_track_gain(dt, 0.5)   # the break slices are hot — leave headroom

    # 3) learn the slice pitches slice_to_midi wrote (one ascending note per slice, from C1/36).
    gen = v.call("get_clip", track=dt, scene=S_INTRO)
    pitches = sorted({int(round(nt["p"])) for nt in gen.get("notes", [])})
    n = len(pitches) or 1

    def sl(frac):                       # pick a slice by fractional position (adapts to slice count)
        return pitches[min(n - 1, int(frac * n))]

    def seq2(pattern, fill=None):
        """A 2-bar clip (8 beats, 1/16 grid) from a 16-step bar `pattern` of slice-fracs (None=rest).
        `fill` (4 fracs) replaces bar 2's last beat — the classic stutter tail."""
        notes = []
        for bar in range(2):
            steps = list(pattern)
            if bar == 1 and fill:
                steps[12:16] = fill
            for i, frac in enumerate(steps):
                if frac is None:
                    continue
                notes.append({"p": sl(frac), "s": round(bar * 4.0 + i * 0.25, 4),
                              "d": 0.22, "v": 0.95 if i % 4 == 0 else 0.72})
        return notes

    # bar patterns (16 steps = 1 bar of 1/16s); fracs index into the slice bank
    VERSE  = [0.00, None, 0.50, None, 0.25, None, 0.00, None,
              0.50, None, 0.25, None, 0.72, None, 0.50, None]
    CHORUS = [0.00, None, 0.50, None, 0.25, None, 0.00, 0.72,
              0.50, None, 0.25, 0.25, None, 0.72, None, 0.90]
    BRIDGE = [0.00, None, None, None, 0.50, None, None, None,
              0.25, None, None, None, 0.72, None, None, None]
    OUTRO  = [0.00, None, None, None, None, None, None, None,
              0.50, None, None, None, None, None, None, None]

    v.clear_clip(dt, S_INTRO)                                   # intro = the RAW break only
    v.set_clip(dt, S_VERSE,  seq2(VERSE),  8.0)
    v.set_clip(dt, S_CHORUS, seq2(CHORUS, fill=[0.90, 0.90, 0.95, 0.95]), 8.0)
    v.set_clip(dt, S_BRIDGE, seq2(BRIDGE), 8.0)
    v.set_clip(dt, S_OUTRO,  seq2(OUTRO),  8.0)

    # ---------------- sub-bass (Surge; verse/chorus/bridge — a rolling A-minor low line) ----------
    sub = v.add_graph_track("sub")
    surge_preset(v, sub, "bass", prefer="Sub", gain=0.5)
    A1, C2, D2, E2, G2 = 33, 36, 38, 40, 43
    v.bassline(sub, S_VERSE,  [(A1, 0, 1.0), (A1, 1.5, 0.5), (E2, 2, 1.0), (G2, 3, 1.0),
                               (A1, 4, 1.0), (A1, 5.5, 0.5), (D2, 6, 1.0), (C2, 7, 1.0)], 8.0, vel=0.85)
    v.bassline(sub, S_CHORUS, [(A1, i * 0.5, 0.4) if i % 4 else (A1, i * 0.5, 0.45)
                               for i in range(16)], 8.0, vel=0.9)
    v.bassline(sub, S_BRIDGE, [(A1, 0, 2.0), (G2, 4, 2.0)], 8.0, vel=0.8)

    # ================= visuals : a chop-reactive lattice (real geometry, core ops) =================
    # A square ShapeGrid + a solid Mesh octahedron ADD into the frame; every slice hit jolts it, the
    # busy chorus smears the Feedback trails, and the sub swells + spins the solid. No custom op.
    out = find(v.graph()["nodes"], "Output")
    grid = v.add_node("ShapeGrid")
    for k, val in dict(sides=0.9, cols=0.55, rows=0.55, size=0.5, rotation=0.0,
                       r=0.98, g=0.42, b=0.12, bg_r=0.02, bg_g=0.02, bg_b=0.03).items():
        v.set_node_param(grid, k, val)
    mesh = v.add_node("Mesh")                                   # solid octahedron
    for k, val in dict(wireframe=0.0, shape=2.0, size=0.4, r=1.0, g=0.82, b=0.25, spin=0.0).items():
        v.set_node_param(mesh, k, val)
    comp = v.add_node("Composite"); v.set_node_param(comp, "mode", 1.0)   # ADD
    v.connect(comp, grid, port=0); v.connect(comp, mesh, port=1)
    jolt = v.add_node("Transform"); v.set_node_param(jolt, "tx", 0.0)     # <- each slice hit punches
    v.connect(jolt, comp)
    fb = v.add_node("Feedback"); v.set_node_param(fb, "decay", 0.3)       # <- busy sections smear
    v.connect(fb, jolt)
    v.connect(out, fb)

    # ---- the bridge : each part -> its own identifiable visual effect (per-track sources) ----
    v.track_viz(dt,  "transient", jolt, "tx",     amount=1.0, lo=0.0, hi=0.5)   # slice hit -> frame punch
    v.track_viz(dt,  "level",     fb,   "decay",  amount=0.6, lo=0.2, hi=0.72)  # density  -> stutter trails
    v.track_viz(sub, "low",       mesh, "size",   amount=1.0, lo=0.3, hi=0.6)   # sub      -> solid swells
    v.track_viz(sub, "low",       mesh, "spin",   amount=0.5, lo=0.0, hi=0.6)   #          -> and turns

    if save:
        save_geo(v, PROJECT)

    # form = the launch order. perform() steps it (bar-quantized); else land on the chorus so a plain
    # run immediately shows the full arrangement.
    order = [S_INTRO, S_VERSE, S_CHORUS, S_VERSE, S_BRIDGE, S_OUTRO]
    if perform:
        v.perform(order, [4, 8, 8, 8, 8, 4], bpm=BPM)
    else:
        v.play()                # transport must run BEFORE launch — launch is bar-quantized
        v.launch_scene(S_CHORUS)
        print("built. sections = scenes intro/verse/chorus/bridge/outro; "
              "v.perform(%s, [4,8,8,8,8,4], bpm=%d) plays the whole song." % (order, BPM))


if __name__ == "__main__":
    import sys
    build(Vivid(), perform="perform" in sys.argv)
