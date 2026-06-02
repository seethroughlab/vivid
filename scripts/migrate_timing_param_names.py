#!/usr/bin/env python3
"""One-time at-rest migration of in-tree graph/preset JSONs to the consolidated
timing vocabulary (clock_mode / frequency / sync_division). Clean break: there is
no runtime compatibility shim, so saved files are rewritten to the new names.

Per operator type:
  rate_mode   -> clock_mode      (Lfo, Sequencer, AudioClip, Flanger, Phaser, Chorus, Clock)  value unchanged
  clock_source-> clock_mode      (Envelope, Mseg, DrumSequencer, Arpeggiator, PatternSeq, Euclidean, Tracker)  value unchanged
  sync        -> clock_mode      (Delay)  value unchanged (0=internal, 1=metronome)
  rate        -> frequency       (Flanger, Phaser, Chorus)  value unchanged (Hz)
  rate        -> sync_division   (Arpeggiator, PatternSeq, Euclidean, Tracker)  value remapped 9->12 enum
  rate_cv port-> freq_cv         (Flanger, Phaser, Chorus connections)

Key renames are done in place (preserving order), and each file is re-emitted
with its detected indentation, so diffs show only the intended changes.
"""
import json, re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Legacy operator-id aliases (mirror operator_aliases.cpp) so graphs that still
# store the pre-merge type id get their params migrated under the resolved type.
TYPE_ALIAS = {
    "LfoFr": "Lfo", "ClockFr": "Clock", "EnvelopeFr": "Envelope",
    "SmoothFr": "Smooth", "StepCounterFr": "StepCounter",
    "EnvelopeFollower": "Smooth",
}

MODE_RENAME = {
    "Lfo": "rate_mode", "Sequencer": "rate_mode", "AudioClip": "rate_mode",
    "Flanger": "rate_mode", "Phaser": "rate_mode", "Chorus": "rate_mode",
    "Clock": "rate_mode",
    "Envelope": "clock_source", "Mseg": "clock_source", "DrumSequencer": "clock_source",
    "Arpeggiator": "clock_source", "PatternSeq": "clock_source",
    "Euclidean": "clock_source", "Tracker": "clock_source",
    "Delay": "sync",
    "PingPongDelay": "sync",
    "ChordProgression": "clock_source",
    "NotePattern": "clock_source",
    "Alternate": "clock_source",
    "StateMachine": "clock_source",
    "PhaseToMidi": "clock_source",
}
RATE_TO_FREQ = {"Flanger", "Phaser", "Chorus"}
RATE_TO_DIV = {"Arpeggiator", "PatternSeq", "Euclidean", "Tracker"}
DIV_REMAP = {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 9, 7: 10, 8: 11}
CV_RENAME = {"Flanger", "Phaser", "Chorus"}


def rename_key(d, old, new, transform=None):
    """Rename a dict key in place, preserving insertion order. Returns True if done."""
    if not isinstance(d, dict) or old not in d:
        return False
    rebuilt = {}
    for k, v in d.items():
        if k == old:
            rebuilt[new] = transform(v) if transform else v
        else:
            rebuilt[k] = v
    d.clear()
    d.update(rebuilt)
    return True


def remap_division(v):
    try:
        return DIV_REMAP.get(int(round(float(v))), v)
    except (TypeError, ValueError):
        return v


def resolve_type(t):
    return TYPE_ALIAS.get(t, t)


def migrate_param_map(op_type, params):
    """Mutate a params/locks dict in place. Returns True if anything changed."""
    if not isinstance(params, dict):
        return False
    t = resolve_type(op_type)
    changed = False
    legacy = MODE_RENAME.get(t)
    if legacy:
        changed |= rename_key(params, legacy, "clock_mode")
    if t in RATE_TO_FREQ:
        changed |= rename_key(params, "rate", "frequency")
    if t in RATE_TO_DIV:
        changed |= rename_key(params, "rate", "sync_division", transform=remap_division)
    if t == "NoteModulator":
        for pre in ("timbre", "pressure", "pitch_bend"):
            changed |= rename_key(params, pre + "_rate_mode", pre + "_clock_mode")
    return changed


def migrate_endpoint(ep, type_by_id):
    if not isinstance(ep, str) or "/" not in ep:
        return ep, False
    nid, _, port = ep.rpartition("/")
    if port == "rate_cv" and resolve_type(type_by_id.get(nid)) in CV_RENAME:
        return f"{nid}/freq_cv", True
    return ep, False


def iter_nodes(nodes):
    if isinstance(nodes, list):
        for n in nodes:
            if isinstance(n, dict):
                yield n.get("id"), n
    elif isinstance(nodes, dict):
        for nid, n in nodes.items():
            if isinstance(n, dict):
                yield nid, n


def migrate_graph(doc):
    changed = False
    nodes = doc.get("nodes")
    type_by_id = {nid: n.get("type") for nid, n in iter_nodes(nodes) if nid is not None}
    for _, n in iter_nodes(nodes):
        t = n.get("type")
        changed |= migrate_param_map(t, n.get("params"))
        changed |= migrate_param_map(t, n.get("locks"))
        presets = n.get("presets")
        if isinstance(presets, list):
            for p in presets:
                if isinstance(p, dict):
                    changed |= migrate_param_map(t, p.get("params"))
    # top-level per-node presets: {"presets": {node_id: [{"params": {...}}]}}
    top_presets = doc.get("presets")
    if isinstance(top_presets, dict):
        for nid, plist in top_presets.items():
            t = type_by_id.get(nid)
            if isinstance(plist, list):
                for p in plist:
                    if isinstance(p, dict):
                        changed |= migrate_param_map(t, p.get("params"))
    conns = doc.get("connections")
    if isinstance(conns, list):
        for c in conns:
            if isinstance(c, dict):
                for key in ("from", "to"):
                    if key in c:
                        ep, ch = migrate_endpoint(c[key], type_by_id)
                        if ch:
                            c[key] = ep
                            changed = True
    return changed


FACTORY_DIR_TYPE = {
    "sequencer": "Sequencer", "lfo": "Lfo", "envelope": "Envelope",
    "arpeggiator": "Arpeggiator", "pattern_seq": "PatternSeq",
    "euclidean": "Euclidean", "tracker": "Tracker", "drum_sequencer": "DrumSequencer",
    "mseg": "Mseg", "clock": "Clock", "flanger": "Flanger", "phaser": "Phaser",
    "chorus": "Chorus", "delay": "Delay", "audio_clip": "AudioClip",
}


def migrate_factory_presets(doc, op_type):
    changed = False
    presets = doc.get("presets")
    if isinstance(presets, list):
        for p in presets:
            if isinstance(p, dict):
                changed |= migrate_param_map(op_type, p.get("params"))
    return changed


def detect_indent(text):
    """Return the indentation unit (in spaces) of the first nested line."""
    for line in text.splitlines():
        m = re.match(r"^( +)\S", line)
        if m:
            return len(m.group(1))
    return 2


def main():
    search_dirs = ["graphs", "reference_graphs", "resources", "tests/fixtures", "operators"]
    files = []
    for d in search_dirs:
        p = ROOT / d
        if p.exists():
            files += list(p.rglob("*.json"))
    changed_files = []
    for f in sorted(set(files)):
        try:
            text = f.read_text()
            doc = json.loads(text)
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        if not isinstance(doc, dict):
            continue
        if f.name == "factory_presets.json":
            op_type = FACTORY_DIR_TYPE.get(f.parent.name)
            changed = migrate_factory_presets(doc, op_type) if op_type else False
        else:
            changed = migrate_graph(doc)
        if changed:
            indent = detect_indent(text)
            out = json.dumps(doc, indent=indent, ensure_ascii=False)
            if text.endswith("\n"):
                out += "\n"
            f.write_text(out)
            changed_files.append(str(f.relative_to(ROOT)))
    print(f"Rewrote {len(changed_files)} file(s):")
    for cf in changed_files:
        print(f"  {cf}")


if __name__ == "__main__":
    main()
