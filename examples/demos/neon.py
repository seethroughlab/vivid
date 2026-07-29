"""Neon — a synthwave demo (100 BPM, A minor).

Music : a driving 16th arpeggio (Pigments), an octave root bass on 8ths (Serum), and a
        gated-feel backbeat (kick/snare/hats) over an i-VI-III-VII loop (Am-F-C-G).
Visual: RETRO-VECTOR GLOW — per-NOTE 2D geometry, one distinct vector element per instrument,
        composited ADD over black (the additive geometry IS the glow — no Feedback/Blur haze).
        The bass pulses a big concentric ring, the arp sweeps small rings (pitch→hue), the
        drums throw a spark field on every hit; the whole ring field throbs on the beat.
        Notes drive the picture, not master-bus band energy.

Run with the app running:  uv run examples/demos/neon.py
"""
import os
from vivid_demo import Vivid, find, save_geo, surge_preset, SURGE_FX, SURGE_FX_TYPE
import theory

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.join(HERE, "projects", "neon")

CASSETTE = "Cassette Drums"       # free VST3 drum machine (BPB)


def build(v: Vivid, save: bool = True):
    v.reset()
    v.bpm(100)
    prog = ["Am", "F", "C", "G"]      # i-VI-III-VII, 1 bar each

    # --- roster : self-contained. Author our OWN tracks (don't assume the default session's
    # tracks) so the project regenerates from any session. All free plugins: Surge XT — a bright
    # detuned arp (with a shimmer FX) + a dark square bass — over a free VST3 drum machine. ---
    DRUMS = v.add_track(kind="instrument", instrument=CASSETTE)     # free VST3 drums
    v.set_track_gain(DRUMS, 0.85)                  # leave master headroom (kick is the hottest source)
    ARP = v.add_graph_track("arp")
    an = surge_preset(v, ARP, "pluck", prefer="Sync", gain=0.75)   # bright synthwave pluck arp
    v.clap_effect(ARP, SURGE_FX)                  # synthwave shimmer/space on the arp
    v.node_param(ARP, v.audio_node_id(ARP, "effect"), SURGE_FX_TYPE, 0.05)
    BASS = v.add_graph_track("bass"); surge_preset(v, BASS, "bass", prefer="Square", gain=0.85)

    # --- arp : classic driving 16th synthwave arp, one chord per bar ---
    notes = []
    for i, c in enumerate(prog):
        ps = theory.chord(c, octave=4)
        seq = theory.arpeggiate(ps, "up", rate=0.25, octaves=2, length=4.0, vel=0.75)
        for n in seq:
            notes.append({"p": n["p"], "s": i * 4.0 + n["s"], "d": n["d"] * 0.9, "v": n["v"]})
    v.set_clip(ARP, 0, notes, 16.0)

    # --- bass : root on straight 8ths, an octave down (drives the pulse) ---
    seq = []
    for i, c in enumerate(prog):
        root = theory.chord(c, octave=2)[0]
        for k in range(8):            # 8 eighth-notes per bar
            seq.append((root, i * 4.0 + k * 0.5, 0.45))
    v.bassline(BASS, 0, seq, length=16.0, vel=0.9)

    # --- drums : four-on-floor kick, snare backbeat, gated 16th hats (4 bars) ---
    v.drums(DRUMS, 0, {
        "kick":  "x...x...x...x...",
        "snare": "....x.......x...",
        "hat":   "x.x.x.x.x.x.x.x.",
    }, bars=4, vel=0.85)

    # --- visuals : RETRO-VECTOR GLOW — per-note 2D geometry, one distinct vector element per
    # instrument, composited ADD over black. No Feedback/Blur: the additive geometry IS the neon
    # glow. Each track's Notes source drives its own draw op — the picture reads WHICH notes play,
    # not just how loud. (Distinct from pulse's 3D Solids: neon is flat vector rings + orbs + sparks.)
    out = find(v.graph()["nodes"], "Output")
    arp_sig  = v.notes(v.track_id(ARP))
    bass_sig = v.notes(v.track_id(BASS))
    drum_sig = v.notes(v.track_id(DRUMS))

    rings  = v.instancer(arp_sig,  shape=1, size=0.22, spread=0.92, trail=0.55, pulse=0.7)  # arp → small sweeping rings
    orbs   = v.instancer(bass_sig, shape=1, size=0.62, spread=0.5,  trail=0.5,  pulse=0.9)  # bass → big concentric ring
    sparks = v.emitter(drum_sig, count=0.5, speed=0.75, gravity=0.35, life=0.4,             # drums → spark bursts
                       size=0.35, spread=0.85)
    v.beat_sync("beat_pulse", rings, "size", amount=0.18, lo=0.22, hi=0.4)  # on-beat throb of the ring field

    # Composite ADD, layered: (orbs + rings) then + sparks -> Output.
    stack = v.add_node("Composite"); v.set_node_param(stack, "mode", 1.0)
    v.connect(stack, orbs, port=0); v.connect(stack, rings, port=1)
    top = v.add_node("Composite"); v.set_node_param(top, "mode", 1.0)
    v.connect(top, stack, port=0); v.connect(top, sparks, port=1)
    v.connect(out, top)

    v.master_gain(0.6)   # headroom: the arp+bass+drums sum was clipping at 0 dBFS (AV clip needs clean audio)
    v.launch_scene(0)
    v.play()
    if save:
        save_geo(v, PROJECT)


if __name__ == "__main__":
    build(Vivid())
