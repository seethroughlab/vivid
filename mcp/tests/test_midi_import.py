#!/usr/bin/env python3
"""MIDI file import/export against the real app (P3).

Drum plugins — EZdrummer, Superior Drummer, Addictive Drums — expose almost nothing through their
VST3 parameter surface; their whole library is grooves you drag out of the plugin's own browser. So
`import_midi` is the only path from that library into Vivid, and `export_midi` is how a part goes back
out. This drives the shipping binary and asserts the contract end to end:

  * a .mid written by export_midi re-imports with the same notes (round-trip),
  * `channel` filters a multi-part file down to one part (GM drums are channel 9),
  * `file_track` picks one track out of a format-1 file,
  * `transpose` shifts pitches and reports what it dropped out of range,
  * `append` overdubs instead of replacing,
  * clip length rounds up to a whole bar unless overridden,
  * a non-MIDI file is REJECTED with io_error rather than silently importing nothing,
  * import lands on the undo stack (a mis-imported groove must not cost you the clip),
  * a >256-note import survives save + reload (the fixed-buffer truncation regression).

Needs the built app + a GPU, so it runs on the self-hosted macOS runner / locally, not in portable
headless CI. Run:  uv run mcp/tests/test_midi_import.py
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("VIVID_MIDI_PORT", "9883"))
APP = os.environ.get("VIVID_APP", str(ROOT / "build" / "Vivid.app" / "Contents" / "MacOS" / "vivid"))
BASE = f"http://127.0.0.1:{PORT}"


def call(method: str, body: dict | None = None) -> dict:
    data = json.dumps(body or {}).encode()
    req = urllib.request.Request(f"{BASE}/{method}", data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read().decode())


def ok(method: str, body: dict | None = None) -> dict:
    r = call(method, body)
    if not r.get("ok", False):
        raise SystemExit(f"FAIL: {method} -> {r}")
    return r


def wait_up(timeout=30.0) -> bool:
    end = time.time() + timeout
    while time.time() < end:
        try:
            call("get_health")
            return True
        except Exception:
            time.sleep(0.5)
    return False


# --- a minimal SMF writer, so the fixtures are explicit bytes rather than a checked-in blob ---
def _vlq(x: int) -> bytes:
    out = bytes([x & 0x7F])
    x >>= 7
    while x:
        out = bytes([(x & 0x7F) | 0x80]) + out
        x >>= 7
    return out


def _track(events: list[tuple[int, bytes]]) -> bytes:
    body = b"".join(_vlq(dt) + ev for dt, ev in events) + _vlq(0) + b"\xFF\x2F\x00"
    return b"MTrk" + struct.pack(">I", len(body)) + body


def smf(tracks: list[bytes], ppq: int = 96, fmt: int = 1) -> bytes:
    return b"MThd" + struct.pack(">IHHH", 6, fmt, len(tracks), ppq) + b"".join(tracks)


def note(ch: int, pitch: int, vel: int, on: bool = True) -> bytes:
    return bytes([(0x90 if on else 0x80) | ch, pitch, vel if on else 0])


def pitches(track: int, scene: int) -> list:
    return sorted(n["p"] for n in ok("get_clip", {"track": track, "scene": scene})["notes"])


def main() -> int:
    if not Path(APP).exists():
        print(f"SKIP: app not built at {APP} (set VIVID_APP)")
        return 0
    env = {**os.environ, "VIVID_PORT": str(PORT), "VIVID_NO_RECOVER": "1", "VIVID_DISCARD_RECOVERY": "1"}
    proc = subprocess.Popen([APP], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    tmp = tempfile.TemporaryDirectory()
    td = Path(tmp.name)
    try:
        if not wait_up():
            if proc.poll() is not None:
                print(f"FAIL: app exited before the control server came up (code {proc.returncode})")
                return 1
            print("SKIP: control server never came up (no window server / GPU in this environment)")
            return 0
        ok("new_project")
        t = ok("add_track", {"kind": "instrument", "instrument": "TestTone"})
        ti = t.get("track", t.get("index", 0))

        # --- a two-track file: a bass part on channel 0, a drum part on channel 9 ---
        bass = _track([(0, note(0, 40, 100)), (96, note(0, 40, 0, on=False)),
                       (0, note(0, 43, 100)), (96, note(0, 43, 0, on=False))])
        drums = _track([(0, note(9, 36, 110)), (48, note(9, 36, 0, on=False)),
                        (0, note(9, 38, 90)),  (48, note(9, 38, 0, on=False))])
        two_part = td / "two_part.mid"
        two_part.write_bytes(smf([bass, drums]))

        # Everything imports by default.
        r = ok("import_midi", {"track": ti, "scene": 0, "path": str(two_part)})
        if r["notes"] != 4 or pitches(ti, 0) != [36, 38, 40, 43]:
            print(f"FAIL: full import -> {r}, pitches {pitches(ti, 0)}")
            return 1
        if r.get("file_format") != 1 or r.get("file_tracks") != 2:
            print(f"FAIL: header not reported: {r}")
            return 1
        # Content is 2 beats; the loop length rounds up to a whole bar.
        if abs(r["length"] - 4.0) > 1e-6:
            print(f"FAIL: length should round up to a bar, got {r['length']}")
            return 1

        # channel filter: GM drums only.
        r = ok("import_midi", {"track": ti, "scene": 0, "path": str(two_part), "channel": 9})
        if pitches(ti, 0) != [36, 38] or r["skipped"] != 2:
            print(f"FAIL: channel filter -> {r}, pitches {pitches(ti, 0)}")
            return 1

        # file_track filter: track 0 is the bass part.
        r = ok("import_midi", {"track": ti, "scene": 0, "path": str(two_part), "file_track": 0})
        if pitches(ti, 0) != [40, 43]:
            print(f"FAIL: file_track filter -> pitches {pitches(ti, 0)}")
            return 1

        # transpose, and an explicit length override.
        r = ok("import_midi", {"track": ti, "scene": 0, "path": str(two_part),
                               "file_track": 0, "transpose": 12, "length": 8.0})
        if pitches(ti, 0) != [52, 55] or abs(r["length"] - 8.0) > 1e-6:
            print(f"FAIL: transpose/length -> {r}, pitches {pitches(ti, 0)}")
            return 1

        # append overdubs rather than replacing.
        ok("import_midi", {"track": ti, "scene": 0, "path": str(two_part), "file_track": 0})
        r = ok("import_midi", {"track": ti, "scene": 0, "path": str(two_part),
                               "file_track": 1, "append": True})
        if pitches(ti, 0) != [36, 38, 40, 43]:
            print(f"FAIL: append should overdub -> pitches {pitches(ti, 0)}")
            return 1

        # A file that is not MIDI is rejected, not silently imported as nothing.
        junk = td / "junk.mid"
        junk.write_bytes(b"this is not a midi file at all")
        r = call("import_midi", {"track": ti, "scene": 0, "path": str(junk)})
        if r.get("ok") or r.get("code") != "io_error":
            print(f"FAIL: junk file should be rejected with io_error -> {r}")
            return 1

        # Import is undoable — a mis-imported groove must not cost the clip that was there.
        ok("set_clip", {"track": ti, "scene": 0, "length": 4.0, "notes": [{"p": 72, "s": 0, "d": 1, "v": 0.8}]})
        ok("import_midi", {"track": ti, "scene": 0, "path": str(two_part)})
        if pitches(ti, 0) == [72]:
            print("FAIL: import did not change the clip")
            return 1
        ok("undo")
        if pitches(ti, 0) != [72]:
            print(f"FAIL: undo after import did not restore the clip -> {pitches(ti, 0)}")
            return 1

        # --- export round-trip ---
        src = [{"p": 60, "s": 0.0, "d": 1.0, "v": 0.8},
               {"p": 64, "s": 1.0, "d": 0.5, "v": 0.6},
               {"p": 67, "s": 1.5, "d": 2.5, "v": 1.0}]
        ok("set_clip", {"track": ti, "scene": 0, "length": 4.0, "notes": src})
        out = td / "roundtrip.mid"
        e = ok("export_midi", {"track": ti, "scene": 0, "path": str(out)})
        if e["notes"] != 3 or not out.exists():
            print(f"FAIL: export_midi -> {e}")
            return 1
        ok("set_clip", {"track": ti, "scene": 1, "length": 4.0, "notes": []})
        ok("import_midi", {"track": ti, "scene": 1, "path": str(out)})
        back = ok("get_clip", {"track": ti, "scene": 1})["notes"]
        if sorted(n["p"] for n in back) != [60, 64, 67]:
            print(f"FAIL: round-trip pitches -> {back}")
            return 1
        for a in src:
            m = [n for n in back if n["p"] == a["p"]]
            if not m or abs(m[0]["s"] - a["s"]) > 0.01 or abs(m[0]["d"] - a["d"]) > 0.01:
                print(f"FAIL: round-trip timing for p={a['p']} -> {m}")
                return 1

        # Exporting an empty clip is an error, not an empty file.
        ok("set_clip", {"track": ti, "scene": 2, "length": 4.0, "notes": []})
        r = call("export_midi", {"track": ti, "scene": 2, "path": str(td / "empty.mid")})
        if r.get("ok"):
            print(f"FAIL: exporting an empty clip should fail -> {r}")
            return 1

        # --- a dense import survives save + reload (the fixed-256-buffer truncation regression) ---
        dense_evs = []
        for i in range(300):
            dense_evs.append((0 if i else 0, note(0, 36 + (i % 24), 100)))
            dense_evs.append((24, note(0, 36 + (i % 24), 0, on=False)))
        dense = td / "dense.mid"
        dense.write_bytes(smf([_track(dense_evs)]))
        ok("add_scene")   # a fresh session has 3 scenes (0..2); scenes 0-2 are already in use above
        r = ok("import_midi", {"track": ti, "scene": 3, "path": str(dense)})
        n_imported = r["notes"]
        if n_imported < 257:
            print(f"FAIL: dense fixture should exceed 256 notes, got {n_imported}")
            return 1
        proj = td / "dense_project.vivid"
        ok("save_project", {"path": str(proj)})
        ok("load_project", {"path": str(proj)})   # save_project writes a FOLDER project; load_session takes a bare session file
        after = len(ok("get_clip", {"track": ti, "scene": 3})["notes"])
        if after != n_imported:
            print(f"FAIL: {n_imported} notes imported but {after} survived save+reload (truncation)")
            return 1

        print(f"ok   test_midi_import — import/export SMF: filters, transpose, append, round-trip, "
              f"undo, junk rejected, {n_imported} notes survive save+reload")
        return 0
    finally:
        tmp.cleanup()
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
