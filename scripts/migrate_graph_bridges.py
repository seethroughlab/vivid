#!/usr/bin/env python3
"""Add explicit bridge fields to cross-cadence connections in Vivid graph JSON files.

Usage: python3 scripts/migrate_graph_bridges.py graphs/**/*.json tests/graphs/*.json
"""

import json
import sys
import os

# Audio-native operators (always audio cadence)
AUDIO_OPERATORS = {
    "Oscillator", "Gain", "Reverb", "Delay", "Filter", "audio_out",
    "Mixer", "Compressor", "Limiter", "Distortion", "Bitcrush",
    "Chorus", "Flanger", "Phaser", "PingPongDelay", "RingMod",
    "FMSynth", "GranularSynth", "Vocoder", "SpectralFreeze",
    "Slicer", "StereoPanWidth", "ParametricEq",
    "DrumKick", "DrumSnare", "DrumHihat", "DrumClap", "DrumCymbal", "DrumTom",
    "AudioAnalysis", "MicInput", "MovieFileAudio", "AudioNoise",
    "Sampler", "SP404", "SubOsc", "PolyVoiceAllocator",
    "Feedback", "TimeMachine", "MidiFilePlayer",
}

# GPU operators that output textures — NOT audio despite having audio-like names
GPU_OPERATORS = {
    "MovieFileIn", "SyphonIn", "SyphonOut", "WebcamIn",
}

# Sinks that adapt to their input (treated as same-cadence as source)
SINKS = {"audio_out", "video_out"}

def classify_cadence(type_name):
    """Return 'audio', 'frame', or 'unknown'."""
    if type_name in SINKS:
        return type_name  # special handling
    if type_name in GPU_OPERATORS:
        return "frame"
    if type_name in AUDIO_OPERATORS:
        return "audio"
    if type_name.endswith("_au"):
        return "audio"
    if type_name.endswith("_fr"):
        return "frame"
    # GPU operators are frame cadence
    # Most remaining operators (control) are frame cadence
    return "frame"

def determine_bridge_kind(from_cadence, to_cadence, from_port, to_port):
    """Determine the bridge kind for a cross-cadence connection."""
    if from_cadence == "frame" and to_cadence == "audio":
        return "hold"
    if from_cadence == "audio" and to_cadence == "frame":
        # Check if the port name suggests analysis
        port_name = from_port.split("/")[-1] if "/" in from_port else from_port
        if port_name == "rms":
            return "rms"
        if port_name == "peak":
            return "peak"
        if port_name == "waveform":
            return "waveform"
        return "last_sample"
    return None

def migrate_graph(filepath):
    """Add bridge fields to cross-cadence connections. Returns (modified, stats)."""
    with open(filepath) as f:
        data = json.load(f)

    nodes = data.get("nodes", {})
    connections = data.get("connections", [])

    # Classify each node
    cadences = {}
    for node_id, node_def in nodes.items():
        cadences[node_id] = classify_cadence(node_def.get("type", ""))

    modified = False
    stats = {"added": 0, "skipped": 0, "already": 0}

    for conn in connections:
        if "bridge" in conn:
            stats["already"] += 1
            continue

        from_addr = conn.get("from", "")
        to_addr = conn.get("to", "")
        from_node = from_addr.split("/")[0]
        to_node = to_addr.split("/")[0]

        from_cad = cadences.get(from_node, "frame")
        to_cad = cadences.get(to_node, "frame")

        # Resolve sinks
        if from_cad == "audio_out":
            from_cad = "audio"
        if to_cad == "audio_out":
            to_cad = "audio"
        if from_cad == "video_out":
            from_cad = "frame"
        if to_cad == "video_out":
            to_cad = "frame"

        if from_cad == to_cad:
            stats["skipped"] += 1
            continue

        bridge = determine_bridge_kind(from_cad, to_cad, from_addr, to_addr)
        if bridge:
            conn["bridge"] = bridge
            stats["added"] += 1
            modified = True
        else:
            stats["skipped"] += 1

    if modified:
        with open(filepath, "w") as f:
            json.dump(data, f, indent=2)
            f.write("\n")

    return modified, stats

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <graph_files...>")
        sys.exit(1)

    total_modified = 0
    total_added = 0
    for filepath in sys.argv[1:]:
        if not os.path.exists(filepath):
            continue
        modified, stats = migrate_graph(filepath)
        if modified:
            total_modified += 1
            print(f"  {filepath}: +{stats['added']} bridges")
        total_added += stats["added"]

    print(f"\nMigrated {total_modified} files, added {total_added} bridge fields.")

if __name__ == "__main__":
    main()
