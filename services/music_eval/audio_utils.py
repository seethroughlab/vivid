"""Audio decode and format helpers shared across backends."""

import base64
import io
import numpy as np
import soundfile as sf


def decode_wav_b64(audio_b64: str) -> tuple[np.ndarray, int]:
    """Decode a base64 WAV string to (samples, sample_rate).

    Returns float32 numpy array shaped (frames, channels) or (frames,) for mono.
    Raises ValueError on decode failure.
    """
    try:
        raw = base64.b64decode(audio_b64)
    except Exception as e:
        raise ValueError(f"base64 decode failed: {e}") from e
    try:
        samples, sr = sf.read(io.BytesIO(raw), dtype="float32", always_2d=True)
    except Exception as e:
        raise ValueError(f"WAV decode failed: {e}") from e
    return samples, sr


def load_audio_file(path: str, start_seconds: float = 0.0, duration_seconds: float | None = None) -> tuple[np.ndarray, int]:
    """Load an audio file from disk, optionally slicing a window.

    Returns float32 numpy array shaped (frames, channels).
    Raises ValueError if the file cannot be read.
    """
    try:
        info = sf.info(path)
        sr = info.samplerate
        start_frame = int(start_seconds * sr)
        frames = int(duration_seconds * sr) if duration_seconds is not None else -1
        samples, _ = sf.read(path, start=start_frame, frames=frames, dtype="float32", always_2d=True)
    except Exception as e:
        raise ValueError(f"audio file load failed ({path}): {e}") from e
    return samples, sr


def to_mono(samples: np.ndarray) -> np.ndarray:
    """Mix a (frames, channels) array down to (frames,) mono."""
    if samples.ndim == 1:
        return samples
    return samples.mean(axis=1)


def encode_wav_b64(samples: np.ndarray, sample_rate: int) -> str:
    """Encode a numpy float32 array to a base64 WAV string."""
    buf = io.BytesIO()
    sf.write(buf, samples, sample_rate, format="WAV", subtype="FLOAT")
    return base64.b64encode(buf.getvalue()).decode("ascii")
