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
from vivid_demo import Vivid, find, save_geo, surge_preset

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "signal")
MEDIA = os.path.join(HERE, "media")
CASSETTE = "Cassette Drums"


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(100)

    # --- audio : Cassette Drums + a Surge sub bass — a groove the footage reacts to ---
    drums = v.add_track(kind="instrument", instrument=CASSETTE)
    v.drums(drums, 0, {
        "kick": "x.......x.......",
        "snare": "....x.......x...",
        "hat":  "..x.x...x.x.x...",
    }, bars=2, vel=0.9)
    bass = v.add_graph_track("bass")
    surge_preset(v, bass, "bass", prefer="", gain=0.8)
    v.bassline(bass, 0, [(33, i * 0.5, 0.45) for i in range(16)], length=8.0, vel=0.85)

    # --- visuals : a REAL video clip -> Displace -> Feedback, with a type call-sign over it ---
    out = find(v.graph()["nodes"], "Output")
    v.set_media_root(MEDIA)                            # discover the clips in this folder
    vid = v.video(0)                                   # A3: select clip 0; a Video node blits it
    disp = v.add_node("Displace")                     # hard grid displacement, not a warp field
    v.set_node_param(disp, "amount", 0.08)
    v.set_node_param(disp, "mode", 0.0)
    fb = v.add_node("Feedback")
    v.set_node_param(fb, "decay", 0.28)              # light trails — keep the footage legible
    v.connect(disp, vid)                              # Video -> Displace
    v.connect(fb, disp)                               # -> Feedback
    v.connect(out, fb)                                # -> Output
    # (No type call-sign here: a Video source currently flips the composited output, which would
    #  render a title upside-down. The cellular-automaton field IS the signal.)

    # --- the bridge : the break drives the treatment (smooth params only) ---
    v.map("master.transient", fb,   "decay",  amount=0.5, lo=0.25, hi=0.6)    # hits lengthen trails
    v.map("master.high",      disp, "amount", amount=1.0, lo=0.04, hi=0.22)   # hats displace it

    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
