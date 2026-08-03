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


class PreviewSources(scaffolds.Sources):
    """Audit scaffold sources, but texture inputs come from the prism test image (an Image node) rather
    than the scaffold's default `Gradient` — a bundled SHADER that isn't registered in a fresh instance.
    Falls back to `NoiseField` (a procedural source that's always present) when the test image is absent,
    so the tool still runs before the asset is added. Patched only here, never in shared scaffolds.py."""

    def get(self, kind):
        if kind == "texture":
            if "texture" not in self.cache:
                self.cache["texture"] = (self.v.image(TEST_IMAGE) if os.path.exists(TEST_IMAGE)
                                         else self.v.add_node("NoiseField"))
            return self.cache["texture"]
        return super().get(kind)

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


def square_thumb(src_png: str, dst_png: str):
    """Center-crop to square, downscale to THUMB, write dst (RGBA preserved)."""
    im = Image.open(src_png).convert("RGBA")
    w, h = im.size
    s = min(w, h)
    im = im.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s))
    im = im.resize((THUMB, THUMB), Image.LANCZOS)
    im.save(dst_png)


def capture_op(v, op, tmpdir) -> str:
    """Wire the op's own render into Output and capture a preview. Returns a status string."""
    name = op["name"]
    slug = slugify(name)
    v.call("new_project")
    out = find_output(v)
    try:
        op_node, _terminal = scaffolds.build_scaffold(v, op, PreviewSources(v))
    except Exception as e:
        return f"scaffold-error: {str(e)[:80]}"
    # Feed the op's OWN output into Output (the audit's thumbnail step) so the preview is JUST this op.
    try:
        v.connect(out, op_node, 0, 0)
    except Exception as e:
        return f"wire-error: {str(e)[:80]}"
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
    square_thumb(raw, os.path.join(OUT_DIR, f"{slug}.png"))
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
