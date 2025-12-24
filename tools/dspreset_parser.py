#!/usr/bin/env python3
"""
Decent Sampler (.dspreset) to Vivid JSON converter

Parses Decent Sampler XML presets and outputs a JSON format
that can be used with Vivid's Sampler/MultiSampler.

Usage:
    python dspreset_parser.py <input.dspreset> [output.json]
    python dspreset_parser.py --folder <sample_pack_folder>
"""

import xml.etree.ElementTree as ET
import json
import os
import sys
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import List, Optional

@dataclass
class SampleMapping:
    """A single sample with its key/velocity mapping"""
    path: str
    root_note: int
    lo_note: int
    hi_note: int
    lo_vel: int = 0
    hi_vel: int = 127
    volume_db: float = 0.0
    pan: float = 0.0  # -1 to 1
    tune_cents: int = 0
    loop_enabled: bool = False
    loop_start: int = 0
    loop_end: int = 0
    loop_crossfade: int = 0

@dataclass
class GroupSettings:
    """Settings shared by a group of samples"""
    attack: float = 0.0
    decay: float = 0.0
    sustain: float = 1.0
    release: float = 0.3
    volume_db: float = 0.0

@dataclass
class EffectSettings:
    """Effect chain settings"""
    filter_type: Optional[str] = None
    filter_freq: float = 22000.0
    filter_res: float = 0.707
    reverb_wet: float = 0.0
    reverb_size: float = 0.5
    chorus_mix: float = 0.0
    chorus_rate: float = 0.2
    chorus_depth: float = 0.2

@dataclass
class InstrumentPreset:
    """Complete instrument preset"""
    name: str
    source_format: str = "DecentSampler"
    samples: List[SampleMapping] = None
    groups: List[GroupSettings] = None
    effects: EffectSettings = None

    def __post_init__(self):
        if self.samples is None:
            self.samples = []
        if self.groups is None:
            self.groups = []
        if self.effects is None:
            self.effects = EffectSettings()


def parse_note(note_str: str) -> int:
    """Convert note string or number to MIDI note number"""
    if note_str is None:
        return 60

    # If it's already a number
    try:
        return int(note_str)
    except ValueError:
        pass

    # Parse note name like "C4", "F#3", "Bb5"
    note_str = note_str.strip()
    note_names = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}

    i = 0
    if note_str[i].upper() in note_names:
        note = note_names[note_str[i].upper()]
        i += 1
    else:
        return 60

    # Check for sharp/flat
    if i < len(note_str) and note_str[i] == '#':
        note += 1
        i += 1
    elif i < len(note_str) and note_str[i].lower() == 'b':
        note -= 1
        i += 1

    # Get octave
    try:
        octave = int(note_str[i:])
    except ValueError:
        octave = 4

    return note + (octave + 1) * 12


def parse_db(db_str: str) -> float:
    """Parse dB string like '-3dB' or '0.0dB'"""
    if db_str is None:
        return 0.0
    db_str = db_str.strip().lower().replace('db', '')
    try:
        return float(db_str)
    except ValueError:
        return 0.0


def parse_time(time_str: str) -> float:
    """Parse time string like '100 ms', '1.5s', '0.5' (seconds)"""
    if time_str is None:
        return 0.0

    time_str = str(time_str).strip().lower()

    # Handle "100 ms" or "100ms" format
    if 'ms' in time_str:
        try:
            return float(time_str.replace('ms', '').strip()) / 1000.0
        except ValueError:
            return 0.0

    # Handle "1.5s" or "1.5 s" format
    if 's' in time_str:
        try:
            return float(time_str.replace('s', '').strip())
        except ValueError:
            return 0.0

    # Plain number (assumed seconds)
    try:
        return float(time_str)
    except ValueError:
        return 0.0


def parse_dspreset(filepath: str) -> InstrumentPreset:
    """Parse a Decent Sampler .dspreset file"""

    tree = ET.parse(filepath)
    root = tree.getroot()

    preset_name = Path(filepath).stem
    preset = InstrumentPreset(name=preset_name)

    base_dir = Path(filepath).parent

    # Parse groups and samples
    for groups_elem in root.findall('.//groups'):
        # Get group-level ADSR
        group_settings = GroupSettings(
            attack=parse_time(groups_elem.get('attack', '0.0')),
            decay=parse_time(groups_elem.get('decay', '0.0')),
            sustain=float(groups_elem.get('sustain', 1.0)),
            release=parse_time(groups_elem.get('release', '0.3')),
            volume_db=parse_db(groups_elem.get('volume', '0dB'))
        )
        preset.groups.append(group_settings)

        # Parse each group
        for group in groups_elem.findall('group'):
            group_volume = parse_db(group.get('volume', '0dB'))
            group_attack = parse_time(group.get('attack')) if group.get('attack') else group_settings.attack
            group_decay = parse_time(group.get('decay')) if group.get('decay') else group_settings.decay
            group_sustain = float(group.get('sustain', group_settings.sustain))
            group_release = parse_time(group.get('release')) if group.get('release') else group_settings.release

            # Parse samples in this group
            for sample in group.findall('sample'):
                path = sample.get('path', '')
                root_note = int(sample.get('rootNote', 60))
                lo_note = int(sample.get('loNote', root_note))
                hi_note = int(sample.get('hiNote', root_note))
                lo_vel = int(sample.get('loVel', 0))
                hi_vel = int(sample.get('hiVel', 127))
                volume = parse_db(sample.get('volume', '0dB'))
                pan = float(sample.get('pan', 0))
                tune = int(sample.get('tuning', 0))

                # Loop settings
                loop_enabled = sample.get('loopEnabled', 'false').lower() == 'true'
                loop_start = int(sample.get('loopStart', 0))
                loop_end = int(sample.get('loopEnd', 0))
                loop_xfade = int(sample.get('loopCrossfade', 0))

                mapping = SampleMapping(
                    path=path,
                    root_note=root_note,
                    lo_note=lo_note,
                    hi_note=hi_note,
                    lo_vel=lo_vel,
                    hi_vel=hi_vel,
                    volume_db=volume + group_volume,
                    pan=pan,
                    tune_cents=tune,
                    loop_enabled=loop_enabled,
                    loop_start=loop_start,
                    loop_end=loop_end,
                    loop_crossfade=loop_xfade
                )
                preset.samples.append(mapping)

    # Parse effects
    effects = EffectSettings()
    for effect in root.findall('.//effect'):
        effect_type = effect.get('type', '').lower()

        if effect_type == 'lowpass':
            effects.filter_type = 'lowpass'
            effects.filter_freq = float(effect.get('frequency', 22000))
            effects.filter_res = float(effect.get('resonance', 0.707))
        elif effect_type == 'highpass':
            effects.filter_type = 'highpass'
            effects.filter_freq = float(effect.get('frequency', 20))
            effects.filter_res = float(effect.get('resonance', 0.707))
        elif effect_type == 'reverb':
            effects.reverb_wet = float(effect.get('wetLevel', 0))
            effects.reverb_size = float(effect.get('roomSize', 0.5))
        elif effect_type == 'chorus':
            effects.chorus_mix = float(effect.get('mix', 0))
            effects.chorus_rate = float(effect.get('modRate', 0.2))
            effects.chorus_depth = float(effect.get('modDepth', 0.2))

    preset.effects = effects

    return preset


def preset_to_dict(preset: InstrumentPreset) -> dict:
    """Convert preset to JSON-serializable dict"""
    return {
        'name': preset.name,
        'source_format': preset.source_format,
        'samples': [asdict(s) for s in preset.samples],
        'envelope': asdict(preset.groups[0]) if preset.groups else asdict(GroupSettings()),
        'effects': asdict(preset.effects)
    }


def generate_vivid_code(preset: InstrumentPreset, base_path: str = "") -> str:
    """Generate C++ code for loading this preset in Vivid"""

    lines = [
        f"// Auto-generated from {preset.name}.dspreset",
        f"// {len(preset.samples)} samples",
        "",
    ]

    # For simple single-sample presets
    if len(preset.samples) == 1:
        s = preset.samples[0]
        sample_path = os.path.join(base_path, s.path) if base_path else s.path
        lines.extend([
            f'auto& sampler = chain.add<Sampler>("{preset.name}");',
            f'sampler.loadSample("{sample_path}");',
            f'sampler.rootNote = {s.root_note};',
        ])
        if preset.groups:
            g = preset.groups[0]
            lines.extend([
                f'sampler.attack = {g.attack}f;',
                f'sampler.decay = {g.decay}f;',
                f'sampler.sustain = {g.sustain}f;',
                f'sampler.release = {g.release}f;',
            ])
    else:
        # Multi-sample - would need MultiSampler
        lines.extend([
            "// Multi-sample preset - requires MultiSampler (not yet implemented)",
            "// Sample mappings:",
        ])
        for s in preset.samples:
            lines.append(f"//   {s.path}: root={s.root_note}, range={s.lo_note}-{s.hi_note}")

    return '\n'.join(lines)


def scan_folder(folder: str) -> List[InstrumentPreset]:
    """Scan a folder for .dspreset files and parse them all"""
    presets = []
    folder_path = Path(folder)

    for dspreset in folder_path.rglob('*.dspreset'):
        try:
            preset = parse_dspreset(str(dspreset))
            preset.name = dspreset.stem
            presets.append(preset)
            print(f"  Parsed: {dspreset.name} ({len(preset.samples)} samples)")
        except Exception as e:
            print(f"  Error parsing {dspreset}: {e}")

    return presets


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    if sys.argv[1] == '--folder':
        if len(sys.argv) < 3:
            print("Usage: dspreset_parser.py --folder <sample_pack_folder>")
            sys.exit(1)

        folder = sys.argv[2]
        print(f"Scanning {folder} for .dspreset files...")
        presets = scan_folder(folder)

        if not presets:
            print("No .dspreset files found")
            sys.exit(1)

        # Output combined JSON
        output = {
            'instruments': [preset_to_dict(p) for p in presets]
        }

        output_path = os.path.join(folder, 'vivid_presets.json')
        with open(output_path, 'w') as f:
            json.dump(output, f, indent=2)
        print(f"\nWrote {output_path}")

        # Also generate C++ snippets
        cpp_path = os.path.join(folder, 'vivid_setup.cpp')
        with open(cpp_path, 'w') as f:
            for preset in presets:
                f.write(generate_vivid_code(preset, folder) + '\n\n')
        print(f"Wrote {cpp_path}")

    else:
        # Single file mode
        input_path = sys.argv[1]
        output_path = sys.argv[2] if len(sys.argv) > 2 else input_path.replace('.dspreset', '.json')

        print(f"Parsing {input_path}...")
        preset = parse_dspreset(input_path)

        print(f"  Name: {preset.name}")
        print(f"  Samples: {len(preset.samples)}")
        for s in preset.samples:
            print(f"    {s.path}: root={s.root_note}, range={s.lo_note}-{s.hi_note}")

        if preset.groups:
            g = preset.groups[0]
            print(f"  ADSR: A={g.attack}, D={g.decay}, S={g.sustain}, R={g.release}")

        # Write JSON
        with open(output_path, 'w') as f:
            json.dump(preset_to_dict(preset), f, indent=2)
        print(f"\nWrote {output_path}")

        # Print C++ code
        print("\n// Vivid C++ setup code:")
        print(generate_vivid_code(preset, str(Path(input_path).parent)))


if __name__ == '__main__':
    main()
