"""Audio analysis + plot rendering for Vivid's MCP debug tools.

These helpers consume raw audio (WAV bytes from `capture_audio` /
`capture_node_audio` / `capture_note_window`) or numpy arrays, and produce
either JSON-friendly dicts or PNG bytes. They sit on top of librosa,
soundfile, and matplotlib so the C++ runtime doesn't have to carry any
DSP or rendering code.

Design rules:
- Functions are pure (input → output). No file I/O here; callers handle it.
- All plot rendering uses matplotlib's Agg backend so there's no display
  dependency — the MCP server runs headless.
- librosa imports happen at module load (numba JIT cold start is ~1 s; we
  pay it once).
- Latency: librosa.pyin on 1 s of audio is ~50–200 ms. Acceptable for
  debug tools but not for tight loops.
"""

import base64
import io
import math
from dataclasses import dataclass

import numpy as np

# matplotlib's Agg backend produces PNGs without needing a display server.
# Must be set before importing matplotlib.pyplot.
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import librosa
import librosa.display
import soundfile as sf


# ---------------------------------------------------------------------------
# WAV / sample-array helpers
# ---------------------------------------------------------------------------

def load_wav_bytes(b64: str) -> tuple[np.ndarray, int]:
    """Decode base64 WAV into (samples, sample_rate).

    Samples are float32 in the layout `(n_frames, n_channels)` to match
    soundfile/librosa convention. Mono returns shape `(n_frames,)`.
    """
    if not b64:
        raise ValueError("empty WAV payload")
    raw = base64.b64decode(b64)
    samples, sr = sf.read(io.BytesIO(raw), dtype="float32", always_2d=False)
    return samples, sr


def to_mono(samples: np.ndarray) -> np.ndarray:
    """Mix interleaved/2D samples down to mono."""
    if samples.ndim == 1:
        return samples
    # soundfile gives (n_frames, n_channels)
    return samples.mean(axis=1)


def write_wav_bytes(samples: np.ndarray, sr: int) -> bytes:
    """Encode samples (1D mono or 2D stereo) as 32-bit float WAV bytes."""
    buf = io.BytesIO()
    sf.write(buf, samples, sr, subtype="FLOAT", format="WAV")
    return buf.getvalue()


def dump_audio_samples(samples: np.ndarray, sr: int) -> dict:
    """Encode samples as base64 float32 LE, per channel.

    Returns one base64 string per channel in `samples_b64`. Decode in the
    caller via `np.frombuffer(base64.b64decode(s), dtype="<f4")`. Matches
    the WAV-base64 transport convention used elsewhere in this module.
    """
    if samples.ndim == 1:
        chans = [samples]
    else:
        chans = [samples[:, c] for c in range(samples.shape[1])]
    samples_b64 = [
        base64.b64encode(
            np.ascontiguousarray(c, dtype="<f4").tobytes()
        ).decode("ascii")
        for c in chans
    ]
    return {
        "format": "f32le_base64",
        "sample_rate": int(sr),
        "channels": len(chans),
        "frame_count": int(samples.shape[0] if samples.ndim > 0 else 0),
        "samples_b64": samples_b64,
    }


# ---------------------------------------------------------------------------
# Plot renderers — each returns PNG bytes
# ---------------------------------------------------------------------------

_DEFAULT_DPI = 100


def _figure_to_png(fig) -> bytes:
    buf = io.BytesIO()
    fig.savefig(buf, format="png", dpi=_DEFAULT_DPI, bbox_inches="tight",
                pad_inches=0.05, facecolor="#0E0E12")
    plt.close(fig)
    return buf.getvalue()


def _figsize(width_px: int, height_px: int) -> tuple[float, float]:
    return (width_px / _DEFAULT_DPI, height_px / _DEFAULT_DPI)


def render_waveform_png(samples: np.ndarray, sr: int,
                         width: int = 720, height: int = 200) -> bytes:
    """Time-domain waveform PNG. Channels stacked vertically when 2D."""
    fig, ax = plt.subplots(figsize=_figsize(width, height))
    fig.patch.set_facecolor("#0E0E12")
    ax.set_facecolor("#0E0E12")
    if samples.size == 0:
        ax.text(0.5, 0.5, "no audio", ha="center", va="center", color="#888")
    else:
        librosa.display.waveshow(samples.T if samples.ndim > 1 else samples,
                                  sr=sr, ax=ax, color="#6BC0FF")
    ax.tick_params(colors="#888", labelsize=7)
    for spine in ax.spines.values():
        spine.set_color("#444")
    ax.set_xlabel("time (s)", color="#888", fontsize=8)
    return _figure_to_png(fig)


def render_spectrogram_png(samples: np.ndarray, sr: int,
                            fmin: float = 0.0, fmax: float | None = None,
                            width: int = 720, height: int = 240) -> bytes:
    """Magnitude spectrogram PNG. Linear frequency axis, dB color scale."""
    mono = to_mono(samples) if samples.ndim > 1 else samples
    fig, ax = plt.subplots(figsize=_figsize(width, height))
    fig.patch.set_facecolor("#0E0E12")
    ax.set_facecolor("#0E0E12")
    if mono.size < 1024:
        ax.text(0.5, 0.5, "too short", ha="center", va="center", color="#888")
    else:
        n_fft = 1024
        hop = n_fft // 4
        S = librosa.stft(mono, n_fft=n_fft, hop_length=hop, window="hann")
        S_db = librosa.amplitude_to_db(np.abs(S), ref=np.max)
        librosa.display.specshow(S_db, sr=sr, hop_length=hop,
                                  y_axis="linear", x_axis="time", ax=ax,
                                  cmap="viridis", vmin=-80, vmax=0)
        if fmax is None or fmax <= 0:
            fmax = sr * 0.5
        ax.set_ylim(fmin, fmax)
    ax.tick_params(colors="#888", labelsize=7)
    for spine in ax.spines.values():
        spine.set_color("#444")
    ax.set_xlabel("time (s)", color="#888", fontsize=8)
    ax.set_ylabel("Hz", color="#888", fontsize=8)
    return _figure_to_png(fig)


def render_envelope_png(samples: np.ndarray, sr: int,
                         width: int = 720, height: int = 160) -> bytes:
    """Per-channel RMS envelope PNG."""
    fig, ax = plt.subplots(figsize=_figsize(width, height))
    fig.patch.set_facecolor("#0E0E12")
    ax.set_facecolor("#0E0E12")
    if samples.size == 0:
        ax.text(0.5, 0.5, "no audio", ha="center", va="center", color="#888")
    else:
        if samples.ndim == 1:
            chans = [samples]
        else:
            chans = [samples[:, c] for c in range(samples.shape[1])]
        colors = ["#6BC0FF", "#FFB46B", "#A0E060", "#FF6B9D"]
        hop = max(1, sr // 200)  # 5 ms hop
        for i, ch in enumerate(chans):
            rms = librosa.feature.rms(y=ch, frame_length=hop * 2,
                                       hop_length=hop, center=True)[0]
            t = librosa.frames_to_time(np.arange(len(rms)),
                                        sr=sr, hop_length=hop)
            ax.fill_between(t, 0, rms, color=colors[i % len(colors)], alpha=0.6)
    ax.tick_params(colors="#888", labelsize=7)
    for spine in ax.spines.values():
        spine.set_color("#444")
    ax.set_xlabel("time (s)", color="#888", fontsize=8)
    ax.set_ylabel("rms", color="#888", fontsize=8)
    return _figure_to_png(fig)


def render_lane_strip_png(lanes: list[list[float]] | np.ndarray,
                           ids: list[list[int]] | None = None,
                           width: int = 720, lane_height: int = 32) -> bytes:
    """Stacked per-lane PNG. Each row is one lane's value over time.

    When `ids` is provided (one id per (lane, time) sample), each row is
    colored by the most-common id in that lane via golden-angle hashing.
    Without `ids`, rows alternate between two colors.
    """
    n_lanes = len(lanes)
    if n_lanes == 0:
        fig, ax = plt.subplots(figsize=_figsize(width, lane_height))
        fig.patch.set_facecolor("#0E0E12")
        ax.set_facecolor("#0E0E12")
        ax.text(0.5, 0.5, "no lane data", ha="center", va="center", color="#888")
        ax.axis("off")
        return _figure_to_png(fig)

    total_h = lane_height * n_lanes
    fig, axes = plt.subplots(n_lanes, 1, figsize=_figsize(width, total_h),
                              sharex=True, gridspec_kw={"hspace": 0.05})
    fig.patch.set_facecolor("#0E0E12")
    if n_lanes == 1:
        axes = [axes]

    # Normalize across all lanes so amplitude comparison is meaningful.
    arrays = [np.asarray(l, dtype=np.float32) for l in lanes]
    vmax = max((np.max(np.abs(a)) for a in arrays if a.size > 0), default=1.0)
    if vmax < 1e-9:
        vmax = 1.0

    def color_for(lane_idx: int) -> str:
        if ids is not None and lane_idx < len(ids) and ids[lane_idx]:
            # Most-common id in this lane (np.bincount on non-negative ints).
            arr = np.asarray(ids[lane_idx], dtype=np.int64)
            arr = arr[arr >= 0]
            if arr.size > 0:
                lane_id = int(np.bincount(arr).argmax())
                hue = (lane_id * 0.61803398875) % 1.0  # golden-angle
                rgb = matplotlib.colors.hsv_to_rgb([hue, 0.65, 1.0])
                return matplotlib.colors.to_hex(rgb)
        return "#6BC0FF" if lane_idx % 2 == 0 else "#FFB46B"

    for i, ax in enumerate(axes):
        ax.set_facecolor("#0E0E12")
        a = arrays[i] / vmax
        ax.plot(a, color=color_for(i), linewidth=1.2)
        ax.set_ylim(-1.05, 1.05)
        ax.set_xticks([])
        ax.tick_params(colors="#888", labelsize=6, length=2)
        for spine in ax.spines.values():
            spine.set_color("#333")
        ax.set_ylabel(f"L{i}", color="#888", fontsize=7, rotation=0,
                      labelpad=14, va="center")

    axes[-1].set_xlabel("frame (samples)", color="#888", fontsize=7)
    return _figure_to_png(fig)


def render_spectrogram_diff_png(ref: np.ndarray, ref_sr: int,
                                  cur: np.ndarray, cur_sr: int,
                                  width: int = 720, height: int = 240) -> bytes:
    """Side-by-side spectrogram (left = reference, right = current)."""
    fig, (axl, axr) = plt.subplots(1, 2, figsize=_figsize(width, height),
                                     sharey=True,
                                     gridspec_kw={"wspace": 0.04})
    fig.patch.set_facecolor("#0E0E12")
    axl.set_facecolor("#0E0E12")
    axr.set_facecolor("#0E0E12")
    fmax = min(ref_sr, cur_sr) * 0.5

    def _plot(ax, samples, sr, title):
        mono = to_mono(samples) if samples.ndim > 1 else samples
        if mono.size < 1024:
            ax.text(0.5, 0.5, "too short", ha="center", va="center", color="#888")
            return
        n_fft = 1024
        hop = n_fft // 4
        S = librosa.stft(mono, n_fft=n_fft, hop_length=hop, window="hann")
        S_db = librosa.amplitude_to_db(np.abs(S), ref=np.max)
        librosa.display.specshow(S_db, sr=sr, hop_length=hop,
                                  y_axis="linear", x_axis="time", ax=ax,
                                  cmap="viridis", vmin=-80, vmax=0)
        ax.set_ylim(0, fmax)
        ax.set_title(title, color="#aaa", fontsize=8)

    _plot(axl, ref, ref_sr, "reference")
    _plot(axr, cur, cur_sr, "current")
    for ax in (axl, axr):
        ax.tick_params(colors="#888", labelsize=7)
        for spine in ax.spines.values():
            spine.set_color("#444")
        ax.set_xlabel("time (s)", color="#888", fontsize=7)
    axl.set_ylabel("Hz", color="#888", fontsize=8)
    return _figure_to_png(fig)


# ---------------------------------------------------------------------------
# Scalar / time-series analyzers
# ---------------------------------------------------------------------------

def detect_discontinuities(samples: np.ndarray, sr: int,
                            threshold_multiplier: float = 8.0,
                            min_magnitude: float = 0.05) -> dict:
    """Sample-indexed click/discontinuity events.

    Flags samples where |x[n]-x[n-1]| > max(threshold_multiplier * MAD,
    min_magnitude). The MAD (median absolute diff) is robust against
    pure-tone signals where every adjacent diff is similar; clicks show
    up as outliers above the local baseline.

    Operates on the mono mix when samples are 2D — matches the semantics
    of the aggregate `discontinuity_count_per_sec` field returned by
    `analyze_scalars`.
    """
    mono = to_mono(samples) if samples.ndim > 1 else samples
    frame_count = int(mono.size)
    duration_ms = float(frame_count / max(1, sr) * 1000.0)
    if frame_count < 2:
        return {
            "sample_rate": int(sr),
            "frame_count": frame_count,
            "duration_ms": duration_ms,
            "mad": 0.0,
            "threshold_used": float(min_magnitude),
            "events": [],
            "discontinuity_count_per_sec": 0.0,
        }
    diffs = np.abs(np.diff(mono))
    mad = float(np.median(diffs))
    thresh = max(float(threshold_multiplier) * mad, float(min_magnitude))
    hit_idx = np.where(diffs > thresh)[0]
    # diffs[i] = |x[i+1] - x[i]| → the discontinuity is at sample i+1.
    events = [
        {
            "sample_idx": int(i + 1),
            "t_ms": float((i + 1) / max(1, sr) * 1000.0),
            "magnitude": float(diffs[i]),
        }
        for i in hit_idx
    ]
    dur_sec = max(1e-6, frame_count / max(1, sr))
    return {
        "sample_rate": int(sr),
        "frame_count": frame_count,
        "duration_ms": duration_ms,
        "mad": mad,
        "threshold_used": float(thresh),
        "events": events,
        "discontinuity_count_per_sec": float(len(events)) / dur_sec,
    }


def analyze_spectrum(samples: np.ndarray, sr: int,
                      fft_size: int = 4096,
                      mode: str = "single",
                      db_scale: bool = True,
                      fmin_hz: float = 0.0,
                      fmax_hz: float | None = None) -> dict:
    """Numeric FFT magnitudes over a captured window.

    mode="single"  → one Hann-windowed slice from the center of the buffer
    mode="average" → mean magnitude across an STFT (75% overlap)

    Auto-clamps `fft_size` down to the largest power of two that fits the
    input. The per-node 1024-sample ring caps `fft_size` at 1024. When
    clamped, `fft_size_used` and a `note` field appear in the response.
    """
    mono = to_mono(samples) if samples.ndim > 1 else samples
    out: dict = {"fft_size": int(fft_size), "sample_rate": int(sr),
                  "mode": str(mode)}

    if mono.size < 2:
        out["bins"] = []
        out["frames_averaged"] = 0
        out["note"] = "input too short for FFT"
        return out

    requested = int(fft_size)
    max_n = min(requested, int(mono.size))
    n_fft = 1
    while n_fft * 2 <= max_n:
        n_fft *= 2
    n_fft = max(2, n_fft)
    if n_fft != requested:
        out["fft_size_used"] = n_fft
        out["note"] = (f"fft_size clamped to {n_fft} "
                       f"(input length {int(mono.size)})")

    mono_f32 = mono.astype(np.float32)
    if mode == "average":
        hop = max(1, n_fft // 4)
        S = np.abs(librosa.stft(mono_f32, n_fft=n_fft,
                                  hop_length=hop, window="hann"))
        mag = np.mean(S, axis=1)
        frames_averaged = int(S.shape[1])
    else:
        # Single Hann-windowed slice from the center.
        start = max(0, (int(mono_f32.size) - n_fft) // 2)
        slice_ = mono_f32[start:start + n_fft]
        if slice_.size < n_fft:
            slice_ = np.pad(slice_, (0, n_fft - slice_.size))
        win = np.hanning(n_fft).astype(np.float32)
        spec = np.fft.rfft(slice_ * win)
        mag = np.abs(spec).astype(np.float32)
        frames_averaged = 1

    if db_scale:
        mag_out = librosa.amplitude_to_db(mag, ref=np.max)
    else:
        mag_out = mag

    freqs = librosa.fft_frequencies(sr=sr, n_fft=n_fft)
    fmax_eff = (float(fmax_hz) if (fmax_hz is not None and fmax_hz > 0)
                else float(sr) / 2.0)
    fmin_eff = float(fmin_hz) if fmin_hz > 0 else 0.0

    bins = [{"hz": float(freqs[i]), "magnitude": float(mag_out[i])}
            for i in range(len(freqs))
            if fmin_eff <= float(freqs[i]) <= fmax_eff]

    out["bins"] = bins
    out["frames_averaged"] = frames_averaged
    return out


def analyze_scalars(samples: np.ndarray, sr: int) -> dict:
    """Window-averaged scalars on one audio buffer.

    Replaces the C++ `AudioMetrics` extension that we removed in the pivot.
    All metrics computed via librosa where possible (better algorithms than
    the autocorrelation/FFT I'd hand-rolled).
    """
    if samples.size == 0:
        return {"rms": 0.0, "peak": 0.0, "dc_offset": 0.0,
                "fundamental_hz": 0.0, "pitch_confidence": 0.0,
                "spectral_centroid_hz": 0.0, "spectral_flatness": 0.0,
                "zero_crossing_rate": 0.0, "discontinuity_count_per_sec": 0.0,
                "inter_channel_correlation": 1.0}

    mono = to_mono(samples) if samples.ndim > 1 else samples
    rms = float(np.sqrt(np.mean(mono.astype(np.float64) ** 2)))
    peak = float(np.max(np.abs(mono)))
    dc_offset = float(np.mean(mono))

    # Pitch via pyin — better than autocorrelation for noisy signals.
    fundamental_hz = 0.0
    pitch_confidence = 0.0
    if mono.size >= 2048:
        try:
            f0, voiced_flag, voiced_prob = librosa.pyin(
                mono, sr=sr, fmin=50.0, fmax=2000.0,
                frame_length=2048, hop_length=512)
            if f0 is not None:
                voiced_f0 = f0[~np.isnan(f0)]
                if voiced_f0.size > 0:
                    fundamental_hz = float(np.median(voiced_f0))
                if voiced_prob is not None and voiced_prob.size > 0:
                    pitch_confidence = float(np.mean(voiced_prob))
        except Exception:
            pass

    # Spectral features via librosa.
    spectral_centroid_hz = 0.0
    spectral_flatness = 0.0
    if mono.size >= 1024:
        sc = librosa.feature.spectral_centroid(y=mono, sr=sr, n_fft=1024)
        spectral_centroid_hz = float(np.mean(sc))
        sf_arr = librosa.feature.spectral_flatness(y=mono, n_fft=1024)
        spectral_flatness = float(np.mean(sf_arr))

    # ZCR per librosa.
    zcr = librosa.feature.zero_crossing_rate(y=mono, frame_length=2048,
                                              hop_length=512)
    zero_crossing_rate = float(np.mean(zcr))

    # Discontinuity / click detection: shared with `detect_discontinuities`.
    discontinuity_count_per_sec = detect_discontinuities(
        samples, sr)["discontinuity_count_per_sec"]

    # Inter-channel correlation (1.0 for mono).
    inter_channel_correlation = 1.0
    if samples.ndim > 1 and samples.shape[1] >= 2:
        l = samples[:, 0].astype(np.float64)
        r = samples[:, 1].astype(np.float64)
        if l.std() > 1e-9 and r.std() > 1e-9:
            inter_channel_correlation = float(np.corrcoef(l, r)[0, 1])

    return {
        "rms": rms,
        "peak": peak,
        "dc_offset": dc_offset,
        "fundamental_hz": fundamental_hz,
        "pitch_confidence": pitch_confidence,
        "spectral_centroid_hz": spectral_centroid_hz,
        "spectral_flatness": spectral_flatness,
        "zero_crossing_rate": zero_crossing_rate,
        "discontinuity_count_per_sec": discontinuity_count_per_sec,
        "inter_channel_correlation": inter_channel_correlation,
    }


def analyze_detail(samples: np.ndarray, sr: int,
                    hop_ms: float = 50.0,
                    want_pitch_track: bool = True,
                    want_band_energies: bool = True,
                    want_onset_times: bool = True,
                    want_scalar_summary: bool = True) -> dict:
    """Time-series analysis — pitch track, band energies, onsets, scalar
    summary. Each component is opt-out via the want_* flags."""
    out: dict = {
        "sample_rate": sr,
        "channels": int(samples.shape[1]) if samples.ndim > 1 else 1,
        "frames": int(samples.shape[0] if samples.ndim > 0 else 0),
        "duration_ms": float(samples.shape[0] / max(1, sr) * 1000.0
                              if samples.ndim > 0 else 0.0),
        "hop_ms": hop_ms,
    }
    if samples.size == 0:
        return out

    mono = to_mono(samples) if samples.ndim > 1 else samples
    hop = max(1, int(sr * hop_ms / 1000.0))

    if want_scalar_summary:
        out["scalar"] = analyze_scalars(samples, sr)

    if want_pitch_track and mono.size >= 2048:
        try:
            f0, voiced_flag, voiced_prob = librosa.pyin(
                mono, sr=sr, fmin=50.0, fmax=2000.0,
                frame_length=2048, hop_length=hop)
            track = []
            if f0 is not None:
                times = librosa.times_like(f0, sr=sr, hop_length=hop)
                for i, hz in enumerate(f0):
                    track.append({
                        "t": float(times[i]),
                        "hz": float(hz) if not math.isnan(hz) else 0.0,
                        "conf": float(voiced_prob[i]) if voiced_prob is not None else 0.0,
                    })
            out["pitch_track"] = track
        except Exception:
            out["pitch_track"] = []

    if want_band_energies and mono.size >= 1024:
        # Bass / mid / treble bandsums via STFT.
        n_fft = 1024
        S = np.abs(librosa.stft(mono, n_fft=n_fft, hop_length=hop, window="hann"))
        freqs = librosa.fft_frequencies(sr=sr, n_fft=n_fft)
        bass = np.sqrt(np.sum(S[freqs <= 250.0] ** 2, axis=0))
        mid  = np.sqrt(np.sum(S[(freqs > 250.0) & (freqs <= 2000.0)] ** 2, axis=0))
        treble = np.sqrt(np.sum(S[freqs > 2000.0] ** 2, axis=0))
        times = librosa.times_like(bass, sr=sr, hop_length=hop)
        out["band_energies"] = [
            {"t": float(times[i]),
             "bass": float(bass[i]),
             "mid": float(mid[i]),
             "treble": float(treble[i])}
            for i in range(len(bass))
        ]

    if want_onset_times:
        try:
            onsets = librosa.onset.onset_detect(y=mono, sr=sr,
                                                 hop_length=hop, units="time")
            out["onset_times"] = [float(t) for t in onsets]
        except Exception:
            out["onset_times"] = []

    return out


def compare_to_reference(ref: np.ndarray, ref_sr: int,
                          cur: np.ndarray, cur_sr: int) -> dict:
    """Scalar deltas between current and a reference recording."""
    ref_m = analyze_scalars(ref, ref_sr)
    cur_m = analyze_scalars(cur, cur_sr)
    deltas = {k: cur_m[k] - ref_m[k] for k in ref_m if k in cur_m}
    return {
        "reference_metrics": ref_m,
        "current_metrics": cur_m,
        "deltas": deltas,
    }
