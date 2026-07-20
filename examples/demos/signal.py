"""Signal — external video footage into a hard-edged reactive visual chain (100 BPM).

The showcase: pixels from OUTSIDE the synth engine — a real video clip — pulled into the graph
and treated like any other source. The footage is grid-displaced and feedback-smeared to the
beat, with a VectorText call-sign over it. Deliberately technical, not psychedelic: no plasma,
no kaleidoscope — just displacement, feedback trails, and type.

Uses the A3 feature (`set_video_source`) to pick which discovered clip the shared source texture
plays; a `Video` node blits it. A native sampler plays a bundled break so the picture has a beat
to react to — no plugins, fully self-contained. A live-webcam variant is one line away (see below).

Run with the app running:  uv run examples/demos/signal.py
(Swap examples/demos/media/loop.mp4 for your own footage; the media root is this folder.)
"""
import os
from vivid_demo import Vivid, find, save_geo

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "signal")
MEDIA = os.path.join(HERE, "media")
BREAK = os.path.join(MEDIA, "break90.wav")
TITLE = "SIGNAL"


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(100)

    # --- audio : a bundled break on a native sampler (portable, drives the reactivity) ---
    drums = v.add_track(kind="audio")
    v.import_audio(drums, 0, BREAK, src_bpm=90)
    v.warp(drums, 0, mode="beats")                    # lock the break to 100 BPM

    # --- visuals : a REAL video clip -> Displace -> Feedback, with a type call-sign over it ---
    out = find(v.graph()["nodes"], "Output")
    v.set_media_root(MEDIA)                            # discover the clips in this folder
    vid = v.video(0)                                   # A3: select clip 0; a Video node blits it
    disp = v.add_node("Displace")                     # hard grid displacement, not a warp field
    v.set_node_param(disp, "amount", 0.1)
    v.set_node_param(disp, "mode", 0.0)
    fb = v.add_node("Feedback")
    v.set_node_param(fb, "decay", 0.5)                # smear/trails
    title = v.add_node("VectorText")                  # filled vector glyphs (real geometry)
    for k, val in dict(size=0.14, x=0.5, y=0.86, r=0.1, g=1.0, b=0.75,
                       bg_r=0.0, bg_g=0.0, bg_b=0.0).items():
        v.set_node_param(title, k, val)               # teal call-sign, black bg (keys via ADD)
    v.set_node_file(title, "file", os.path.join(MEDIA, "signal.txt"))
    comp = v.add_node("Composite")
    v.connect(disp, vid)                              # Video -> Displace
    v.connect(fb, disp)                               # -> Feedback
    v.connect(comp, fb, port=0)                       # A = the treated footage
    v.connect(comp, title, port=1)                    # B = the title
    v.set_node_param(comp, "mode", 1.0)              # ADD (black bg of the title drops out)
    v.connect(out, comp)

    # --- the bridge : the break drives the treatment (smooth params only) ---
    v.map("master.transient", fb,   "decay",  amount=0.5, lo=0.35, hi=0.72)   # hits lengthen trails
    v.map("master.high",      disp, "amount", amount=1.0, lo=0.05, hi=0.28)   # hats displace it

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
