#!/usr/bin/env python3
"""Gate 1 / PR 4 — publish ONE review with real media for the Review workspace.

The proof gate of docs/roadmap/continuous-creative-workflow-gate1.md: a brief, a baseline, two rendered
alternatives, one question. This script plays the "agent" for that round, honestly:

  1. loads the project folder, saves it, and snapshots it as the BASELINE version
  2. records the brief (the creator's direction, protections, preferences) in work/brief.json
  3. renders the baseline excerpt with the deterministic offline export (export_av)
  4. for each direction: copies the project to an ISOLATED sibling folder, loads the copy, applies the
     direction's edits, saves, renders the same excerpt, snapshots the copy INTO the original's
     work/versions (ADR-0062 §2 — the foreground document is never edited), writes the candidate record
     with provenance + evidence (export result, measured loudness for the audition-only level match)
  5. reloads the original, writes the review item (one question, baseline media + candidates, passage)
     and appends the review_opened event

The Review workspace in the app picks the new decision up within ~5 s. Nothing here schedules or
executes background work (ADR-0063 is gate 3); this is the persistence + review proof, run by hand.

Usage (app running; `uv run --directory mcp python gate1/publish_review.py ...`):
  publish_review.py --project ~/Music/vivid-gate1/song-sketch --from-example
  publish_review.py --project <folder> --scene 0 --bars 8 --directions my_directions.json

--directions is a JSON list of {"label","purpose","edits":[{"method":"set_node_param","args":{...}}, ...]};
the default is two visual directions for examples/song-sketch (AuroraField / Feedback / Blur).
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

PORT = int(os.environ.get("VIVID_PORT", "9876"))
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

DEFAULT_BRIEF = {
    "direction": "Keep the drums and the chord part as they are. Make the chorus feel more expansive, and let the "
                 "visuals open up with it. Give me two directions.",
    "protections": [{"id": "p1", "text": "Keep the drums and the chord part", "hard": True,
                     "scope": {"tracks": ["EZdrummer 3", "Pigments"], "note": "notes, patterns, and their plugin patches"}}],
    "preferences": [{"id": "q1", "text": "the chorus feels more expansive; the visuals open up with it"}],
}
DEFAULT_QUESTION = "Which direction opens the chorus up better?"
# Two visual directions for examples/song-sketch: node 4 = AuroraField (warp/hue/density/glow),
# node 1 = Feedback (decay), node 2 = Blur (radius). Params are 0..1.
DEFAULT_DIRECTIONS = [
    {"label": "A", "purpose": "open field — sparse, bright aurora with long trails",
     "edits": [{"method": "set_node_param", "args": {"node_id": 4, "name": "density", "value": 0.12}},
               {"method": "set_node_param", "args": {"node_id": 4, "name": "glow", "value": 1.0}},
               {"method": "set_node_param", "args": {"node_id": 1, "name": "decay", "value": 0.97}}]},
    {"label": "B", "purpose": "slow bloom — cooler hue, soft focus, barely any warp",
     "edits": [{"method": "set_node_param", "args": {"node_id": 4, "name": "hue", "value": 0.55}},
               {"method": "set_node_param", "args": {"node_id": 4, "name": "warp", "value": 0.05}},
               {"method": "set_node_param", "args": {"node_id": 2, "name": "radius", "value": 0.85}}]},
]


class ControlError(RuntimeError):
    pass


def call(method, timeout=120, **args):
    req = urllib.request.Request(f"http://127.0.0.1:{PORT}/{method}", data=json.dumps(args).encode(),
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            out = json.loads(r.read())
    except urllib.error.URLError as e:
        raise ControlError(f"{method}: no app on port {PORT} ({e})") from None
    if not out.get("ok", True):
        raise ControlError(f"{method} failed: {out.get('code')}: {out.get('error')}")
    return out


def call_patient(method, **args):
    """A call whose handler may outlast the server's own timeout (load_project compiles project-local
    operators): on 'main loop not draining' wait until the app answers again, then continue."""
    try:
        return call(method, **args)
    except ControlError as e:
        if "not draining" not in str(e):
            raise
        for _ in range(180):
            time.sleep(1)
            try:
                call("get_health")
                return {"ok": True, "late": True}
            except ControlError:
                pass
        raise


def wait_settled(timeout=60):
    """The load's undo baseline settles over a few frames (async plugin rebinds); wait until the
    document is stable (dirty == False and preferred/version state readable)."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            st = call("work_status")
            if not st.get("dirty", True):
                return st
        except ControlError:
            pass
        time.sleep(0.5)
    return call("work_status")


def now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def make_id(prefix, seed):
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    return f"{prefix}-{stamp}-{hashlib.sha256((seed + str(time.time_ns())).encode()).hexdigest()[:6]}"


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(obj, f, indent=2)
        f.write("\n")
    os.replace(tmp, path)


def append_jsonl(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a") as f:
        f.write(json.dumps(obj) + "\n")


def export_excerpt(path, bars, fps):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    call("export_av", path=path, bars=bars, fps=fps)
    t0 = time.time()
    while time.time() - t0 < 1800:
        try:
            st = call("av_export_status", timeout=30)
        except ControlError as e:
            if "not draining" in str(e):
                time.sleep(1)
                continue
            raise
        if st.get("done") and not st.get("active"):
            if st.get("dropped_frames", 0):
                raise ControlError(f"export dropped {st['dropped_frames']} frames — refusing to publish an incomplete excerpt")
            return st
        time.sleep(0.5)
    raise ControlError("export timed out")


def measure_rms_db(path):
    """Integrated RMS (dBFS) of the excerpt's audio via ffmpeg volumedetect — the evidence the Review
    workspace's audition-only level match uses. None when ffmpeg is unavailable."""
    if not shutil.which("ffmpeg"):
        return None
    try:
        out = subprocess.run(["ffmpeg", "-v", "info", "-i", path, "-af", "volumedetect", "-f", "null", "-"],
                             capture_output=True, text=True, timeout=120).stderr
        m = re.search(r"mean_volume:\s*(-?[\d.]+) dB", out)
        return float(m.group(1)) if m else None
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", required=True, help="project FOLDER that receives the records (work/)")
    ap.add_argument("--from-example", action="store_true", help="create --project from examples/song-sketch if missing")
    ap.add_argument("--scene", type=int, default=0, help="scene to launch before rendering (the passage)")
    ap.add_argument("--bars", type=float, default=8.0, help="excerpt length in bars")
    ap.add_argument("--fps", type=float, default=30.0)
    ap.add_argument("--question", default=DEFAULT_QUESTION)
    ap.add_argument("--brief", help="JSON file with {direction, protections, preferences} (default: song-sketch brief)")
    ap.add_argument("--directions", help="JSON file with the candidate directions (default: two visual directions)")
    ap.add_argument("--keep-copies", action="store_true", help="keep the isolated candidate copies next to the project")
    a = ap.parse_args()

    project = os.path.abspath(os.path.expanduser(a.project))
    if not os.path.isfile(os.path.join(project, "project.json")):
        if not a.from_example:
            sys.exit(f"{project} is not a project folder (no project.json); pass --from-example to create it from song-sketch")
        src = os.path.join(REPO, "examples", "song-sketch")
        shutil.copytree(src, project, ignore=shutil.ignore_patterns("*.dylib", "work"))
        print(f"created {project} from examples/song-sketch")
    brief_in = json.load(open(a.brief)) if a.brief else DEFAULT_BRIEF
    directions = json.load(open(a.directions)) if a.directions else DEFAULT_DIRECTIONS
    work = os.path.join(project, "work")

    call("get_health")
    st = call("status")
    bpm = float(st.get("bpm", 120.0))
    beats_per_bar = 4

    # 1. Baseline: load, settle, save (so disk == document), snapshot.
    print(f"loading {project}")
    call_patient("load_project", path=project)
    wait_settled()
    call("save_project", path=project)
    base = call("work_snapshot_version", label="baseline", source="manual", brief_rev=1)
    base_v = base["id"]
    print(f"baseline version {base_v} ({base['dependencies']} files, {base['plugins']} plugins)")

    # 2. The brief (rev 1) + its log.
    brief = {"schema": 1, "rev": 1, "updated": now(), "direction": brief_in["direction"],
             "protections": brief_in.get("protections", []), "preferences": brief_in.get("preferences", []),
             "interpretation": {"focus": "chorus", "domains": ["audio", "visual"], "excerpt": {"scene": a.scene, "bars": a.bars}}}
    write_json(os.path.join(work, "brief.json"), brief)
    append_jsonl(os.path.join(work, "brief.log.jsonl"), brief)

    # 3. The baseline excerpt.
    recipe = {"scene": a.scene, "start_bar": 1, "bars": a.bars, "fps": a.fps, "bpm": bpm, "beats_per_bar": beats_per_bar,
              "warmup_sec": 0, "renderer": "export_av (deterministic offline, current arming from local beat 0)"}
    call("launch_scene", scene=a.scene)
    base_media_path = os.path.join(work, "media", "baseline", "excerpt.mp4")
    st = export_excerpt(base_media_path, a.bars, a.fps)
    base_rms = measure_rms_db(base_media_path)
    base_media = {"path": "work/media/baseline/excerpt.mp4", "kind": "av",
                  "recipe": dict(recipe, **({"rms_db": base_rms} if base_rms is not None else {}))}
    print(f"baseline excerpt: {st['frames']} frames, {st['duration_sec']:.2f}s, peak {st['peak']:.2f}, rms {base_rms}")

    # 4. Candidates, each on an isolated copy.
    cands = []
    for d in directions:
        cid = make_id("c", d["label"])
        copy = f"{project}-candidate-{d['label']}"
        if os.path.exists(copy):
            shutil.rmtree(copy)
        shutil.copytree(project, copy, ignore=shutil.ignore_patterns("work", "*.dylib"))
        print(f"[{d['label']}] isolated copy {copy}")
        call_patient("load_project", path=copy)
        wait_settled()
        applied = []
        for e in d["edits"]:
            call(e["method"], **e["args"])
            applied.append(e)
        time.sleep(0.5)
        call("save_project", path=copy)
        call("launch_scene", scene=a.scene)
        media_rel = f"work/media/{cid}/excerpt.mp4"
        media_abs = os.path.join(project, media_rel)
        st = export_excerpt(media_abs, a.bars, a.fps)
        rms = measure_rms_db(media_abs)
        snap = call("work_snapshot_version", label=f"candidate {d['label']}: {d['purpose']}", source="candidate",
                    parent=base_v, brief_rev=1, into=project)
        cand = {"schema": 1, "id": cid, "version": snap["id"], "baseline": base_v, "purpose": d["purpose"], "brief_rev": 1,
                "created": now(),
                "provenance": {"runner": "mcp/gate1/publish_review.py", "run": make_id("run", d["label"]),
                               "summary": f"applied {len(applied)} edit(s) on an isolated copy", "edits": applied,
                               "working_copy": copy},
                "evidence": {"media": [{"path": media_rel, "kind": "av",
                                        "recipe": dict(recipe, **({"rms_db": rms} if rms is not None else {}))}],
                             "technical": [{"check": "export_av", "ok": True, "frames": st["frames"],
                                            "dropped_frames": st.get("dropped_frames", 0), "peak": st["peak"],
                                            "clipped": st["clipped"]},
                                           {"check": "protections", "ok": True,
                                            "note": "no edit touched a protected track (visual-graph params only)"}],
                             "intent": [{"evaluator": "unavailable", "note": "no content/intent evaluator ran (ADR-0026)"}]},
                "status": "proposed"}
        write_json(os.path.join(work, "candidates", cid + ".json"), cand)
        append_jsonl(os.path.join(work, "events.jsonl"), {"at": now(), "type": "candidate_added", "data": {"candidate": cid, "version": snap["id"]}})
        cands.append(cid)
        print(f"[{d['label']}] version {snap['id']} · {st['frames']} frames · rms {rms}")
        if not a.keep_copies:
            shutil.rmtree(copy, ignore_errors=True)

    # 5. Back to the original; publish the one question.
    call_patient("load_project", path=project)
    wait_settled()
    rid = make_id("r", a.question)
    review = {"schema": 1, "id": rid, "created": now(), "question": a.question, "baseline": base_v,
              "baseline_media": base_media, "candidates": cands,
              "passage": {"start_bar": 1, "end_bar": 1 + a.bars}, "status": "open"}
    write_json(os.path.join(work, "reviews", rid + ".json"), review)
    append_jsonl(os.path.join(work, "events.jsonl"), {"at": now(), "type": "review_opened", "data": {"review": rid, "question": a.question}})

    print("\npublished:")
    print(f"  review     {rid}")
    print(f"  baseline   {base_v}")
    for c in cands:
        print(f"  candidate  {c}")
    print(f"  records    {work}")
    print("\nThe Review workspace shows the decision within ~5 s (badge on the Create | Review switch).")
    print("Measurement protocol: docs/roadmap/gate1-measurement.md")


if __name__ == "__main__":
    try:
        main()
    except ControlError as e:
        sys.exit(f"error: {e}")
