#!/usr/bin/env python3
"""
Generate A/V sync test videos for testing video player synchronization.

Creates videos with:
- Visual: Frame counter and timecode, with flash on each beat
- Audio: Beep/click on each beat, aligned with visual flash

If audio drifts relative to video on loop, the beep will no longer
align with the visual flash - making sync issues immediately obvious.

Requirements:
- ffmpeg (for video encoding)
- Python 3.6+

Usage:
    python generate_sync_test_video.py                    # Generate all formats
    python generate_sync_test_video.py --format h264      # H.264 only
    python generate_sync_test_video.py --duration 10      # 10 second video
"""

import subprocess
import argparse
import struct
import wave
import math
import os
import tempfile
import shutil

# Video parameters
WIDTH = 1280
HEIGHT = 720
FPS = 30
DURATION = 5  # seconds - short for quick loop testing

# Audio parameters
SAMPLE_RATE = 48000
CHANNELS = 2

# Sync parameters
BEATS_PER_SECOND = 1  # beep every second
BEEP_FREQ = 880  # Hz (A5 note - clearly audible)
BEEP_DURATION = 0.05  # seconds


def generate_beep_audio(duration_sec, output_path):
    """Generate audio with beeps at regular intervals."""
    total_samples = int(duration_sec * SAMPLE_RATE)
    beep_samples = int(BEEP_DURATION * SAMPLE_RATE)
    beat_interval_samples = int(SAMPLE_RATE / BEATS_PER_SECOND)

    samples = []
    for i in range(total_samples):
        # Check if we're at a beat point
        sample_in_beat = i % beat_interval_samples

        if sample_in_beat < beep_samples:
            # Generate sine wave beep with envelope
            t = sample_in_beat / SAMPLE_RATE
            # Quick attack, quick decay envelope
            envelope = min(1.0, sample_in_beat / (beep_samples * 0.1))  # attack
            envelope *= max(0.0, 1.0 - (sample_in_beat / beep_samples) ** 2)  # decay
            value = int(32767 * 0.8 * envelope * math.sin(2 * math.pi * BEEP_FREQ * t))
        else:
            value = 0

        # Stereo: same value for both channels
        samples.append(value)
        samples.append(value)

    # Write WAV file
    with wave.open(output_path, 'w') as wav:
        wav.setnchannels(CHANNELS)
        wav.setsampwidth(2)  # 16-bit
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(struct.pack(f'<{len(samples)}h', *samples))

    print(f"  Generated audio: {output_path}")


def generate_video_frames_filter(duration_sec):
    """
    Generate ffmpeg filter for video frames with timecode and beat flash.

    Uses lavfi (libavfilter) to generate video entirely within ffmpeg.
    """
    # Color cycling: flash white on beat, otherwise dark gray
    # The flash lasts 2 frames (about 66ms at 30fps)
    frames_per_beat = FPS // BEATS_PER_SECOND

    # Create filter that:
    # 1. Generates solid color background (flashes on beat)
    # 2. Draws timecode text
    # 3. Draws frame counter
    # 4. Draws beat indicator

    filter_complex = f"""
color=c=0x222222:s={WIDTH}x{HEIGHT}:r={FPS}:d={duration_sec}[bg];
[bg]drawtext=fontfile=/System/Library/Fonts/Menlo.ttc:
    text='%{{pts\\:hms}}':
    fontsize=72:
    fontcolor=white:
    x=(w-text_w)/2:
    y=h/2-100:
    box=1:boxcolor=black@0.5:boxborderw=10[t1];
[t1]drawtext=fontfile=/System/Library/Fonts/Menlo.ttc:
    text='Frame\\: %{{n}}':
    fontsize=48:
    fontcolor=white:
    x=(w-text_w)/2:
    y=h/2+20[t2];
[t2]drawtext=fontfile=/System/Library/Fonts/Menlo.ttc:
    text='BEEP should flash HERE':
    fontsize=36:
    fontcolor=yellow:
    x=(w-text_w)/2:
    y=h/2+120[t3];
[t3]drawbox=x=w/2-100:y=h-200:w=200:h=100:
    color=white@'if(lt(mod(n,{frames_per_beat}),2),1,0)':
    t=fill[out]
"""
    # Clean up the filter (remove newlines for ffmpeg)
    return filter_complex.replace('\n', '').replace('  ', '')


def generate_simple_video_filter(duration_sec):
    """
    Simpler filter that's more likely to work across ffmpeg versions.
    Uses geq for conditional brightness.
    """
    frames_per_beat = FPS // BEATS_PER_SECOND

    # Use geq filter for frame-based conditional rendering
    # Flash the entire background bright on beat frames
    filter_parts = [
        f"color=c=0x333333:s={WIDTH}x{HEIGHT}:r={FPS}:d={duration_sec}",
        # Use geq to flash background on beat (first 3 frames of each beat)
        f"geq=lum='if(lt(mod(N,{frames_per_beat}),3),200,50)':cb=128:cr=128",
        # Timecode
        f"drawtext=fontfile=/System/Library/Fonts/Menlo.ttc:"
        f"text='%{{pts\\:hms}}':"
        f"fontsize=96:fontcolor=white:x=(w-text_w)/2:y=h/3",
        # Frame counter
        f"drawtext=fontfile=/System/Library/Fonts/Menlo.ttc:"
        f"text='Frame %{{n}}':"
        f"fontsize=48:fontcolor=0xcccccc:x=(w-text_w)/2:y=h/3+120",
        # Instructions
        f"drawtext=fontfile=/System/Library/Fonts/Menlo.ttc:"
        f"text='BEEP aligns with FLASH':"
        f"fontsize=32:fontcolor=yellow:x=(w-text_w)/2:y=h-150",
    ]

    return ','.join(filter_parts)


def encode_video(audio_path, output_path, codec_args, duration_sec):
    """Encode video with specified codec, combining generated video and audio."""

    video_filter = generate_simple_video_filter(duration_sec)

    cmd = [
        'ffmpeg', '-y',
        '-f', 'lavfi',
        '-i', video_filter,
        '-i', audio_path,
        '-t', str(duration_sec),
        '-map', '0:v',
        '-map', '1:a',
        '-shortest',
    ] + codec_args + [output_path]

    print(f"  Encoding: {output_path}")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"  ERROR: ffmpeg failed")
        print(result.stderr)
        return False

    return True


def generate_h264(audio_path, output_dir, duration_sec):
    """Generate H.264 + AAC video."""
    output = os.path.join(output_dir, 'sync-test-h264.mp4')
    codec_args = [
        '-c:v', 'libx264',
        '-preset', 'fast',
        '-crf', '18',
        '-pix_fmt', 'yuv420p',
        '-c:a', 'aac',
        '-b:a', '192k',
    ]
    return encode_video(audio_path, output, codec_args, duration_sec)


def generate_prores(audio_path, output_dir, duration_sec):
    """Generate ProRes + PCM video."""
    output = os.path.join(output_dir, 'sync-test-prores.mov')
    codec_args = [
        '-c:v', 'prores_ks',
        '-profile:v', '2',  # ProRes 422 LT
        '-c:a', 'pcm_s16le',
    ]
    return encode_video(audio_path, output, codec_args, duration_sec)


def generate_hap(audio_path, output_dir, duration_sec):
    """Generate HAP + PCM video (requires ffmpeg with HAP support)."""
    output = os.path.join(output_dir, 'sync-test-hap.mov')
    codec_args = [
        '-c:v', 'hap',
        '-format', 'hap',  # or 'hap_alpha', 'hap_q'
        '-c:a', 'pcm_s16le',
    ]
    success = encode_video(audio_path, output, codec_args, duration_sec)
    if not success:
        print("  Note: HAP encoding requires ffmpeg compiled with HAP support")
    return success


def main():
    parser = argparse.ArgumentParser(description='Generate A/V sync test videos')
    parser.add_argument('--format', choices=['h264', 'prores', 'hap', 'all'],
                        default='all', help='Output format(s)')
    parser.add_argument('--duration', type=float, default=DURATION,
                        help=f'Video duration in seconds (default: {DURATION})')
    parser.add_argument('--output', default='assets/videos',
                        help='Output directory (default: assets/videos)')
    args = parser.parse_args()

    # Ensure output directory exists
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    output_dir = os.path.join(project_root, args.output)
    os.makedirs(output_dir, exist_ok=True)

    print(f"Generating {args.duration}s sync test video(s)...")
    print(f"Output directory: {output_dir}")
    print()

    # Create temp directory for intermediate files
    with tempfile.TemporaryDirectory() as temp_dir:
        # Generate audio
        audio_path = os.path.join(temp_dir, 'beeps.wav')
        print("Step 1: Generating audio...")
        generate_beep_audio(args.duration, audio_path)
        print()

        # Generate video(s)
        print("Step 2: Encoding video(s)...")

        if args.format in ['h264', 'all']:
            generate_h264(audio_path, output_dir, args.duration)

        if args.format in ['prores', 'all']:
            generate_prores(audio_path, output_dir, args.duration)

        if args.format in ['hap', 'all']:
            generate_hap(audio_path, output_dir, args.duration)

    print()
    print("Done! Test videos created in:", output_dir)
    print()
    print("To test sync:")
    print("  1. Play video in Vivid with looping enabled")
    print("  2. Watch the white flash box at the bottom")
    print("  3. The beep should occur exactly when the box flashes")
    print("  4. If beep drifts away from flash after loops, sync is broken")


if __name__ == '__main__':
    main()
