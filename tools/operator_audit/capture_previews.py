#!/usr/bin/env python3
"""Per-operator reference PREVIEW images (ADR-0050 follow-up).

Drives the RUNNING app over the control server and captures a small preview PNG for every VISUAL
operator, written to `site/assets/reference/<slug>.png`. The reference site (site/build.py) shows these
on the operator cards + detail pages; `site/generate_reference.py` adds a `preview` field per op when the
PNG exists.

This is the ADR-0042 audit harness's per-op flow with one substitution: instead of `analyze_frame`
(perception only), it wires the op's OWN render into Output and calls `capture_frame` (same GPU readback,
but writes a real PNG), then downscales to a square thumbnail with Pillow.

Run it against a NORMAL app instance (full operator + bundled-shader set) — NOT the clean instance
generate_reference.py spins up (which lacks shaders). Like reference.json, the PNGs are committed and
regenerated on a dev machine (the capture needs a GPU + a non-occluded window; there is no headless mode).

    # launch the app by DIRECT binary path first (open -a can run a stale copy):
    #   VIVID_PORT=9877 build/vivid.app/Contents/MacOS/vivid &
    uv run --with pillow tools/operator_audit/capture_previews.py            # all visual ops
    uv run --with pillow tools/operator_audit/capture_previews.py Shape3D    # one op
"""
import os
import re
import sys
import time
import tempfile
import subprocess

# macOS pauses a fully-occluded window's render loop (App Nap), so keep Vivid foreground — otherwise
# captures come back blank. Cheap (~50ms) and idempotent. (Same workaround as audit.py.)
_FG = ['osascript', '-e',
       'tell application "System Events" to set frontmost of (first process whose name is "vivid") to true']


def foreground():
    try:
        subprocess.run(_FG, capture_output=True, timeout=5)
    except Exception:
        pass


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "examples", "demos"))
sys.path.insert(0, HERE)

from vivid_demo import Vivid            # noqa: E402
import scaffolds                        # noqa: E402
from PIL import Image                   # noqa: E402  (uv run --with pillow)

# The canonical effect-preview INPUT: Vivid's prism→spectrum test image (full hue gamut + a hard prism
# edge + a smooth spectral gradient + fine detail), so a Blur/CRT/Displace/Kaleidoscope preview reads as
# "here's what it did to THIS", the way TouchDesigner uses its banana. Effect (texture-consuming) ops get
# it via an Image node; source/3D ops are unaffected. Absolute path (Image needs one).
TEST_IMAGE = os.path.join(HERE, "assets", "effect-testcard.png")

# Bundled example assets used to SEED the file/signal-driven ops that otherwise render blank. Absolute
# paths (the ops resolve files absolutely). A missing asset degrades gracefully — the op just stays blank.
_MEDIA = os.path.abspath(os.path.join(HERE, "..", "..", "examples", "demos", "media"))
GLTF  = os.path.join(_MEDIA, "frank", "scene.gltf")   # a mesh for MeshLoad / Model / MeshRender / MeshDisplace
VIDEO = os.path.join(_MEDIA, "loop.mp4")              # a clip for the Video op

# The op-under-test's own `file` param, seeded so a file-based SOURCE renders its content instead of a
# placeholder. (Texture/mesh INPUTS to other ops are seeded by PreviewSources below.)
FILE_SEED = {"Image": TEST_IMAGE, "MeshLoad": GLTF, "Model": GLTF, "Video": VIDEO}

def needs_signal(op) -> bool:
    """True for ops that only render once a live signal is flowing: any signal-consumer, the Notes source
    itself, and AudioSpectrum (which reads the master spectrum bus)."""
    if op["name"] in ("AudioSpectrum", "Notes"):
        return True
    return any(scaffolds.input_kind(p) == "signal" for p in scaffolds.in_ports(op))


class PreviewSources(scaffolds.Sources):
    """Audit scaffold sources, seeded so preview inputs carry real content:
      - `texture` comes from the prism test image (an Image node) rather than the scaffold's default
        `Gradient` shader (absent from a fresh instance); falls back to `NoiseField` if the asset is gone.
      - `mesh` (MeshLoad) is pointed at a bundled glTF, so mesh consumers (MeshRender/MeshDisplace) render.
      - `signal` (Notes) is bound to a freshly-armed track, so signal consumers can be driven with note_on.
    Patched only here, never in shared scaffolds.py."""

    def __init__(self, v):
        super().__init__(v)
        self.track_idx = None   # the armed instrument track backing the `signal` source (per project)

    def ensure_track(self):
        """A fresh, armed instrument track whose STABLE id the `Notes` source reads. Returns (idx, id)."""
        if self.track_idx is None:
            # TestTone is a bundled native synth — enough to give the Notes source live events (and to feed
            # the master spectrum for AudioSpectrum) without depending on any third-party plugin.
            self.track_idx = self.v.call("add_track", kind="instrument", instrument="TestTone")["track"]
            self.v.call("arm_track", track=self.track_idx)
        return self.track_idx, self.v.track_id(self.track_idx)

    def get(self, kind):
        if kind == "texture":
            if "texture" not in self.cache:
                self.cache["texture"] = (self.v.image(TEST_IMAGE) if os.path.exists(TEST_IMAGE)
                                         else self.v.add_node("NoiseField"))
            return self.cache["texture"]
        if kind == "signal":
            if "signal" not in self.cache:
                _, tid = self.ensure_track()
                n = self.v.add_node("Notes")
                self.v.set_node_param(n, "track", float(tid))
                self.cache["signal"] = n
            return self.cache["signal"]
        node = super().get(kind)
        if kind == "mesh" and node is not None and "mesh_seeded" not in self.cache:
            self.cache["mesh_seeded"] = True
            if os.path.exists(GLTF):
                self.v.call("set_node_file_param", node_id=node, name="file", value=GLTF)
        return node

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT_DIR = os.path.join(REPO, "site", "assets", "reference")
THUMB = 256            # final square preview size (px)
SETTLE = 0.35          # seconds to let the graph render before capturing
SKIP = {"Output"}      # the sink node, not a content op


def slugify(v: str) -> str:
    # MUST match site/generate_reference.py::slugify so the site finds the file.
    return re.sub(r"[^a-z0-9]+", "-", v.lower()).strip("-")


def find_output(v) -> int:
    for n in v.graph()["nodes"]:
        if (n.get("type") or n.get("op")) == "Output":
            return n["id"]
    raise RuntimeError("no Output node")


def rendered(a: dict) -> bool:
    """Non-blank in the audit sense: any real signal. A uniform solid fill reads as blank by contrast, so
    accept brightness / colour-spread too (mirrors audit.py::rendered)."""
    return (not a.get("is_blank", True)
            or a.get("brightness", 0.0) > 0.02
            or a.get("contrast", 0.0) > 0.01
            or a.get("color_spread", 0.0) > 0.02)


def _bg_color(im: "Image.Image"):
    """The frame's background, sampled as the median of its four corners — so contain-fit padding blends
    into a full-bleed render (invisible on the usual dark 3D background) instead of adding hard bars."""
    w, h = im.size
    pts = [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]
    px = [im.getpixel(p) for p in pts]
    return tuple(sorted(c[i] for c in px)[len(px) // 2] for i in range(4))


def fit_thumb(src_png: str, dst_png: str):
    """Contain-fit the WHOLE frame into a square (padded with the sampled background), then downscale to
    THUMB. Unlike a center-crop this never clips content — a wide effect on the test card keeps its edges,
    and a centered 3D subject still reads centred with seamless padding. RGBA preserved."""
    im = Image.open(src_png).convert("RGBA")
    w, h = im.size
    s = max(w, h)
    canvas = Image.new("RGBA", (s, s), _bg_color(im))
    canvas.paste(im, ((s - w) // 2, (s - h) // 2))
    canvas = canvas.resize((THUMB, THUMB), Image.LANCZOS)
    canvas.save(dst_png)


def drive_signal(v, src: "PreviewSources"):
    """Strike a sustained chord on the source's armed track and start the transport, so signal consumers
    (Emitter/Instancer/Solids/Type) have an active element set — and an Emitter gets its note-on FIRE — at
    capture time. Best-effort: an op with no live audio instrument (AudioSpectrum) may still read blank."""
    if src.track_idx is None:
        return
    try:
        v.call("set_playing", playing=True)
        for pitch in (48, 52, 55, 60):        # a C-minor-ish spread, so a layout/palette fans out
            v.call("note_on", pitch=pitch, vel=0.85)
    except Exception:
        pass


def capture_op(v, op, tmpdir) -> str:
    """Wire the op's own render into Output and capture a preview. Returns a status string."""
    name = op["name"]
    slug = slugify(name)
    v.call("new_project")
    out = find_output(v)
    sources = PreviewSources(v)
    try:
        op_node, terminal = scaffolds.build_scaffold(v, op, sources)
    except Exception as e:
        return f"scaffold-error: {str(e)[:80]}"
    if terminal is None:
        return "skip-blank"          # audio / unknown — nothing GPU-renderable
    # Seed a file-based source op with real content so it renders instead of a placeholder.
    if name in FILE_SEED and os.path.exists(FILE_SEED[name]):
        try:
            v.call("set_node_file_param", node_id=op_node, name="file", value=FILE_SEED[name])
        except Exception:
            pass
    # Feed the scaffold's TERMINAL (a texture output — op_node itself for an effect, or its render chain
    # for a scene/mesh/signal op) into Output. Wiring op_node directly only works for texture-output ops
    # and now trips typed-connection validation for the rest (custom_ref -> texture).
    try:
        v.connect(out, terminal, 0, 0)
    except Exception as e:
        return f"wire-error: {str(e)[:80]}"
    if needs_signal(op):
        # A signal-consumer already created the source track (via Sources.get("signal")); Notes IS the
        # source, so bind it; AudioSpectrum has no signal port, so make a track just to feed the master bus.
        if name == "Notes":
            _, tid = sources.ensure_track()
            v.set_node_param(op_node, "track", float(tid))
        elif sources.track_idx is None:
            sources.ensure_track()
        drive_signal(v, sources)
    time.sleep(SETTLE)
    a = v.call("analyze_frame").get("analysis", {})
    for _ in range(2):                       # a blank frame usually means Vivid lost foreground
        if rendered(a):
            break
        foreground(); time.sleep(SETTLE + 0.2)
        a = v.call("analyze_frame").get("analysis", {})
    if not rendered(a):
        return "skip-blank"
    raw = os.path.join(tmpdir, f"{slug}.raw.png")
    r = v.call("capture_frame", path=raw)
    if not r.get("captured") or not os.path.exists(raw):
        return f"capture-failed: {r.get('reason', '')[:60]}"
    os.makedirs(OUT_DIR, exist_ok=True)
    fit_thumb(raw, os.path.join(OUT_DIR, f"{slug}.png"))
    return "ok"


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    v = Vivid()
    ops = [o for o in v.call("list_operators").get("operators", []) if o["name"] not in SKIP]
    # VISUAL ops only — they're the ones that render. Audio ops (gpu=0) have no visual output.
    ops = [o for o in ops if o.get("gpu")]
    if only:
        ops = [o for o in ops if o["name"] == only]
        if not ops:
            print(f"no visual operator '{only}'"); return

    foreground(); time.sleep(0.5)
    written, skipped, failed = [], [], []
    with tempfile.TemporaryDirectory() as tmp:
        for i, op in enumerate(sorted(ops, key=lambda o: o["name"]), 1):
            status = capture_op(v, op, tmp)
            name = op["name"]
            if status == "ok":
                written.append(name); mark = "OK  "
            elif status == "skip-blank":
                skipped.append(name); mark = "skip"
            else:
                failed.append((name, status)); mark = "FAIL"
            print(f"  [{i:>2}/{len(ops)}] {mark}  {name:<22} {status if status != 'ok' else ''}")

    print(f"\n=== previews -> {OUT_DIR} ===")
    print(f"  wrote {len(written)}")
    if skipped:
        print(f"  skipped (blank / needs-input): {', '.join(skipped)}")
    if failed:
        print(f"  FAILED: {', '.join(n for n, _ in failed)}")


if __name__ == "__main__":
    main()
