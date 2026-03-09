#!/usr/bin/env python3
"""
Convert HAP .mov files to .mp4 container format (no re-encoding).

This just remuxes the container from QuickTime MOV to ISO MP4,
which is needed because minimp4 only supports ISO base media format,
not QuickTime format.

Usage:
    python convert_hap_mov_to_mp4.py                    # Convert all HAP .mov in assets/videos
    python convert_hap_mov_to_mp4.py path/to/file.mov   # Convert specific file
"""

import subprocess
import sys
import os
import glob


def convert_mov_to_mp4(input_path):
    """Convert MOV to MP4 container without re-encoding."""
    if not input_path.endswith('.mov'):
        print(f"Skipping non-MOV file: {input_path}")
        return False

    output_path = input_path.replace('.mov', '.mp4')

    cmd = [
        'ffmpeg', '-y',
        '-i', input_path,
        '-c', 'copy',  # No re-encoding, just remux
        '-movflags', 'faststart',
        '-brand', 'isom',
        output_path
    ]

    print(f"Converting: {input_path} -> {output_path}")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"  ERROR: {result.stderr}")
        return False

    print(f"  Success!")
    return True


def main():
    if len(sys.argv) > 1:
        # Convert specific file(s)
        for path in sys.argv[1:]:
            convert_mov_to_mp4(path)
    else:
        # Find all HAP .mov files in assets/videos
        script_dir = os.path.dirname(os.path.abspath(__file__))
        project_root = os.path.dirname(script_dir)
        videos_dir = os.path.join(project_root, 'assets', 'videos')

        # Look for HAP files (they typically have 'hap' in the name)
        patterns = [
            os.path.join(videos_dir, '*hap*.mov'),
            os.path.join(videos_dir, '*HAP*.mov'),
        ]

        files = []
        for pattern in patterns:
            files.extend(glob.glob(pattern))

        if not files:
            print(f"No HAP .mov files found in {videos_dir}")
            print("Usage: python convert_hap_mov_to_mp4.py [file.mov ...]")
            return

        print(f"Found {len(files)} HAP .mov file(s) to convert:")
        for f in files:
            print(f"  - {os.path.basename(f)}")
        print()

        for f in files:
            convert_mov_to_mp4(f)


if __name__ == '__main__':
    main()
