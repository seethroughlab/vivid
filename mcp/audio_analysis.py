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

# pyloudnorm is optional — only loaded when measure_loudness is called.
_pyln = None
def _pyloudnorm():
    global _pyln
    if _pyln is None:
        import pyloudnorm as pyln
        _pyln = pyln
    return _pyln


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


def render_lissajous_png(L: np.ndarray, R: np.ndarray,
                          size: int = 320,
                          downsample_to: int = 2000) -> bytes:
    """Goniometer / Lissajous plot — L vs R rotated 45° so mid (L+R) is
    vertical and side (L−R) is horizontal. Mono signals collapse to a
    vertical line; phase-inverted signals collapse to horizontal."""
    fig, ax = plt.subplots(figsize=_figsize(size, size))
    fig.patch.set_facecolor("#0E0E12")
    ax.set_facecolor("#0E0E12")
    L_arr = np.asarray(L, dtype=np.float64)
    R_arr = np.asarray(R, dtype=np.float64)
    n = int(min(L_arr.size, R_arr.size))
    if n == 0:
        ax.text(0.5, 0.5, "no audio", ha="center", va="center", color="#888")
        return _figure_to_png(fig)
    if n > downsample_to:
        idx = np.linspace(0, n - 1, downsample_to).astype(int)
        L_arr = L_arr[idx]
        R_arr = R_arr[idx]
    x = (L_arr - R_arr) / np.sqrt(2.0)
    y = (L_arr + R_arr) / np.sqrt(2.0)
    lim = max(1e-6, float(max(np.max(np.abs(x)), np.max(np.abs(y))))) * 1.1
    ax.scatter(x, y, s=1.2, alpha=0.4, color="#6BC0FF")
    ax.set_xlim(-lim, lim)
    ax.set_ylim(-lim, lim)
    ax.set_aspect("equal")
    ax.axhline(0, color="#444", linewidth=0.5)
    ax.axvline(0, color="#444", linewidth=0.5)
    ax.tick_params(colors="#888", labelsize=6)
    for spine in ax.spines.values():
        spine.set_color("#444")
    ax.set_xlabel("side (L−R)/√2", color="#888", fontsize=7)
    ax.set_ylabel("mid (L+R)/√2", color="#888", fontsize=7)
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


def analyze_discontinuity_pattern(event_sample_indices,
                                    sr: int,
                                    fft_size: int = 1024) -> dict:
    """Classify a list of discontinuity sample indices as periodic or random.

    Builds a sparse impulse train at the given indices, FFTs it, and reports
    whether the spectrum has a dominant peak relative to the noise floor.
    A high peak-to-mean ratio means the events fall on a regular grid (e.g.
    every 64 samples → sub-block boundary; every period of the fundamental
    → phase wrap). A flat spectrum means the events are random.

    Also reports `top_intervals` — the most common inter-event sample
    spacings — as a fallback diagnostic when the FFT is borderline.

    Returns `{"periodic": None, "reason": ...}` when fewer than 4 events.
    """
    indices = np.asarray(event_sample_indices, dtype=np.int64)
    n_events = int(indices.size)
    if n_events < 4:
        return {
            "periodic": None,
            "reason": "too few events (need ≥ 4)",
            "n_events": n_events,
        }
    indices = np.sort(indices)
    span = int(indices[-1] - indices[0])
    if span < 2:
        return {
            "periodic": None,
            "reason": "events span < 2 samples",
            "n_events": n_events,
        }
    n = max(int(fft_size), 1)
    while n < min(span + 1, 65536):
        n *= 2
    impulse = np.zeros(n, dtype=np.float32)
    rel = (indices - indices[0]).astype(np.int64)
    rel = rel[rel < n]
    impulse[rel] = 1.0
    spec = np.abs(np.fft.rfft(impulse))
    # Drop DC bin — it's just the event count.
    spec_ac = spec[1:]
    if spec_ac.size == 0:
        return {
            "periodic": None,
            "reason": "FFT too small for AC bins",
            "n_events": n_events,
        }
    peak_idx = int(np.argmax(spec_ac))
    peak_mag = float(spec_ac[peak_idx])
    mean_mag = float(np.mean(spec_ac))
    ratio = peak_mag / max(1e-9, mean_mag)
    peak_bin = peak_idx + 1
    period_samples = float(n) / float(peak_bin)
    period_hz = float(sr) / period_samples
    # Inter-event spacings — same data, simpler view.
    intervals = np.diff(indices)
    interval_counts: dict = {}
    for v in intervals.tolist():
        interval_counts[int(v)] = interval_counts.get(int(v), 0) + 1
    top_intervals = sorted(interval_counts.items(),
                            key=lambda kv: kv[1], reverse=True)[:5]
    interval_jitter = float(np.std(intervals)) if intervals.size > 1 else 0.0
    # Periodic-vs-random heuristic: the peak/mean ratio of a Bernoulli
    # impulse train is ≈ √(N) for periodic and small (<2) for random.
    # Threshold at 4× the random expectation.
    random_baseline = float(np.sqrt(max(1.0, float(rel.size))))
    periodic = bool(ratio > 4.0)
    return {
        "periodic": periodic,
        "n_events": n_events,
        "fft_size": int(n),
        "peak_bin": peak_bin,
        "peak_to_mean_ratio": ratio,
        "random_baseline_ratio": random_baseline,
        "period_samples": period_samples,
        "period_hz": period_hz,
        "interval_jitter_samples": interval_jitter,
        "top_intervals": [{"samples": int(k), "count": int(v)}
                           for k, v in top_intervals],
    }


def detect_dropouts(samples: np.ndarray, sr: int,
                     threshold: float = 0.001,
                     min_run_ms: float = 10.0) -> dict:
    """Find runs of |x| < threshold lasting at least min_run_ms.

    Operates on the mono mix. Catches voice dropouts in the middle of a
    capture that scalar metrics like RMS / peak miss because the dropout
    is averaged out across the surrounding signal.

    Returns each dropout with its start sample, time, and duration.
    """
    mono = to_mono(samples) if samples.ndim > 1 else samples
    frame_count = int(mono.size)
    if frame_count < 2:
        return {
            "sample_rate": int(sr),
            "frame_count": frame_count,
            "threshold": float(threshold),
            "min_run_ms": float(min_run_ms),
            "dropouts": [],
        }
    quiet = np.abs(mono) < float(threshold)
    # Bracket so np.diff finds run starts/ends even at the buffer edges.
    edges = np.diff(np.concatenate([[False], quiet, [False]]).astype(np.int8))
    starts = np.where(edges == 1)[0]
    ends = np.where(edges == -1)[0]
    min_run = int(round(min_run_ms / 1000.0 * sr))
    dropouts = []
    for s, e in zip(starts, ends):
        run_len = int(e - s)
        if run_len >= min_run:
            dropouts.append({
                "sample_idx": int(s),
                "t_ms": float(s / max(1, sr) * 1000.0),
                "duration_ms": float(run_len / max(1, sr) * 1000.0),
                "duration_samples": run_len,
            })
    return {
        "sample_rate": int(sr),
        "frame_count": frame_count,
        "threshold": float(threshold),
        "min_run_ms": float(min_run_ms),
        "dropouts": dropouts,
    }


def measure_clipping(samples: np.ndarray, sr: int,
                      threshold: float = 0.99) -> dict:
    """Count samples whose absolute value meets/exceeds `threshold`.

    Aggregate companion to `peak`: surfaces *how many* samples clipped
    and the longest clip run, not just the maximum value.

    For stereo input, scans each channel and reports per-channel counts
    plus the union total.
    """
    if samples.ndim == 1:
        chans = [samples]
    else:
        chans = [samples[:, c] for c in range(samples.shape[1])]
    per_channel = []
    union_count = 0
    longest_run_overall = 0
    first_idx_overall: int | None = None
    for ch in chans:
        mask = np.abs(ch) >= float(threshold)
        count = int(np.sum(mask))
        first_idx = int(np.argmax(mask)) if count > 0 else -1
        # Longest run of consecutive clipped samples.
        longest = 0
        if count > 0:
            edges = np.diff(np.concatenate([[False], mask, [False]])
                              .astype(np.int8))
            starts = np.where(edges == 1)[0]
            ends = np.where(edges == -1)[0]
            longest = int((ends - starts).max()) if starts.size > 0 else 0
        per_channel.append({
            "count": count,
            "ratio": float(count) / max(1, ch.size),
            "longest_run_samples": longest,
            "first_sample_idx": first_idx,
        })
        union_count += count
        if longest > longest_run_overall:
            longest_run_overall = longest
        if first_idx >= 0 and (first_idx_overall is None
                                or first_idx < first_idx_overall):
            first_idx_overall = first_idx
    total_samples = int(samples.shape[0]
                          if samples.ndim > 0 else samples.size)
    return {
        "sample_rate": int(sr),
        "threshold": float(threshold),
        "channels": len(chans),
        "frame_count": total_samples,
        "count": int(union_count),
        "ratio": float(union_count) / max(1, total_samples * len(chans)),
        "longest_run_samples": int(longest_run_overall),
        "first_sample_idx": int(first_idx_overall)
                              if first_idx_overall is not None else -1,
        "per_channel": per_channel,
    }


def dump_envelope_data(samples: np.ndarray, sr: int,
                        hop_ms: float = 5.0) -> dict:
    """Per-channel RMS envelope as numeric arrays (the data behind the
    `capture_envelope_plot` PNG).

    Each entry in `envelope` is `{t_ms, rms}` per channel. Use this when
    you need to quantify attack/decay timings without reading a PNG.
    """
    if samples.ndim == 1:
        chans = [samples]
    else:
        chans = [samples[:, c] for c in range(samples.shape[1])]
    hop = max(1, int(round(sr * hop_ms / 1000.0)))
    frame_length = max(2, hop * 2)
    out_channels = []
    for ch in chans:
        ch_f32 = np.ascontiguousarray(ch, dtype=np.float32)
        if ch_f32.size < frame_length:
            out_channels.append({"envelope": [], "hop_samples": hop,
                                  "frame_length_samples": frame_length})
            continue
        rms = librosa.feature.rms(y=ch_f32, frame_length=frame_length,
                                    hop_length=hop, center=True)[0]
        times_ms = (np.arange(rms.size) * hop / max(1, sr) * 1000.0)
        envelope = [{"t_ms": float(t), "rms": float(r)}
                    for t, r in zip(times_ms, rms)]
        out_channels.append({
            "envelope": envelope,
            "hop_samples": hop,
            "frame_length_samples": frame_length,
        })
    return {
        "sample_rate": int(sr),
        "hop_ms": float(hop_ms),
        "channels": len(chans),
        "frame_count": int(samples.shape[0] if samples.ndim > 0 else 0),
        "per_channel": out_channels,
    }


def extract_adsr(samples: np.ndarray, sr: int,
                  sustain_ms: float,
                  hop_ms: float = 1.0,
                  noise_floor_factor: float = 0.05,
                  sustain_window_ms: float = 50.0) -> dict:
    """Auto-extract attack, decay, sustain level, release from a captured note.

    Assumes the typical `capture_note_response` timeline:
      0..attack_start: pre-onset silence
      attack_start..peak: attack ramp
      peak..sustain_start: decay
      sustain_start..sustain_ms: sustain
      sustain_ms..release_end: release tail

    `sustain_ms` is the time of NOTE_OFF (known from the capture inputs).
    Returns the four canonical synth parameters plus diagnostics, or
    `{"error": ...}` if the signal is too short / too quiet to analyze.
    """
    mono = to_mono(samples) if samples.ndim > 1 else samples
    if mono.size < 256:
        return {"error": "input too short for ADSR"}

    hop = max(1, int(round(sr * hop_ms / 1000.0)))
    frame_length = max(2, hop * 4)
    if mono.size < frame_length:
        return {"error": "input too short for ADSR"}
    rms = librosa.feature.rms(y=mono.astype(np.float32),
                                frame_length=frame_length,
                                hop_length=hop, center=True)[0]
    if rms.size == 0:
        return {"error": "envelope empty"}
    env_t_ms = np.arange(rms.size) * hop / max(1, sr) * 1000.0
    peak_rms = float(np.max(rms))
    if peak_rms <= 1e-9:
        return {"error": "no signal"}

    noise_floor = peak_rms * float(noise_floor_factor)
    above_floor = rms > noise_floor
    if not bool(np.any(above_floor)):
        return {"error": "no signal above noise floor"}

    attack_start_idx = int(np.argmax(above_floor))
    attack_start_t_ms = float(env_t_ms[attack_start_idx])

    if sustain_ms > 0:
        end_idx = int(np.searchsorted(env_t_ms, float(sustain_ms)))
        end_idx = max(end_idx, attack_start_idx + 1)
        end_idx = min(end_idx, rms.size)
    else:
        end_idx = rms.size
    peak_idx = attack_start_idx + int(np.argmax(rms[attack_start_idx:end_idx]))
    peak_t_ms = float(env_t_ms[peak_idx])
    attack_ms = max(0.0, peak_t_ms - attack_start_t_ms)

    sustain_level = 0.0
    if sustain_ms > 0 and float(sustain_ms) > peak_t_ms:
        sustain_start_t = max(peak_t_ms,
                                float(sustain_ms) - float(sustain_window_ms))
        sustain_start_idx = int(np.searchsorted(env_t_ms, sustain_start_t))
        sustain_region = rms[sustain_start_idx:end_idx]
        if sustain_region.size > 0:
            sustain_level = float(np.median(sustain_region))

    decay_ms = 0.0
    if peak_idx < end_idx and sustain_level < peak_rms:
        decay_target = sustain_level + (peak_rms - sustain_level) * 0.1
        post_peak = rms[peak_idx:end_idx]
        below = np.where(post_peak <= decay_target)[0]
        if below.size > 0:
            decay_end_idx = peak_idx + int(below[0])
            decay_ms = max(0.0, float(env_t_ms[decay_end_idx] - peak_t_ms))

    release_ms = 0.0
    release_end_t_ms = 0.0
    if sustain_ms > 0:
        release_start_idx = int(np.searchsorted(env_t_ms, float(sustain_ms)))
        if release_start_idx < rms.size:
            after_off = rms[release_start_idx:]
            below = np.where(after_off <= noise_floor)[0]
            if below.size > 0:
                release_end_idx = release_start_idx + int(below[0])
                release_end_t_ms = float(env_t_ms[release_end_idx])
                release_ms = max(0.0, release_end_t_ms - float(sustain_ms))

    # Attack curve classification: convex (fast initial rise, exp-like),
    # linear, concave (slow initial rise). Skipped for instant attacks.
    if attack_ms < 5.0:
        attack_curve = "instant"
    else:
        seg = rms[attack_start_idx:peak_idx + 1]
        if seg.size >= 4 and (seg[-1] - seg[0]) > 1e-6:
            norm = (seg - seg[0]) / max(1e-9, seg[-1] - seg[0])
            mid_value = float(norm[len(norm) // 2])
            if mid_value > 0.6:
                attack_curve = "convex"
            elif mid_value < 0.4:
                attack_curve = "concave"
            else:
                attack_curve = "linear"
        else:
            attack_curve = "instant"

    return {
        "attack_ms": float(attack_ms),
        "decay_ms": float(decay_ms),
        "sustain_level": float(sustain_level),
        "release_ms": float(release_ms),
        "attack_curve": attack_curve,
        "peak_rms": peak_rms,
        "peak_t_ms": peak_t_ms,
        "attack_start_t_ms": attack_start_t_ms,
        "release_end_t_ms": release_end_t_ms,
        "noise_floor": float(noise_floor),
        "sustain_ms_input": float(sustain_ms),
    }


def analyze_harmonic_profile(samples: np.ndarray, sr: int,
                              fundamental_hz: float | None = None,
                              n_partials: int = 10,
                              fft_size: int = 8192) -> dict:
    """Decompose a tonal signal into its strongest partials and report THD
    and inharmonicity.

    For each integer multiple n of the fundamental, finds the peak bin
    near n × f0 and reports its observed frequency + magnitude (linear
    and dB). THD is the power ratio Σ|A_n>1|² / |A_1|². Inharmonicity is
    the mean fractional deviation of observed partials from the perfect
    harmonic series — non-zero for stretched scales, FM bells, naïve
    wavetable interpolation, etc.

    If `fundamental_hz` is None, estimate via librosa.pyin (median).
    Returns `{"error": ...}` when the signal is non-tonal or too short.
    """
    mono = to_mono(samples) if samples.ndim > 1 else samples
    if mono.size < 2048:
        return {"error": "input too short for harmonic analysis"}

    if fundamental_hz is None:
        try:
            f0, voiced_flag, voiced_prob = librosa.pyin(
                mono.astype(np.float32), sr=sr, fmin=50.0, fmax=2000.0,
                frame_length=2048, hop_length=512)
            voiced_f0 = f0[~np.isnan(f0)] if f0 is not None else None
            if voiced_f0 is None or voiced_f0.size == 0:
                return {"error": "no fundamental detected"}
            fundamental_hz = float(np.median(voiced_f0))
        except Exception as exc:  # noqa: BLE001
            return {"error": f"pitch detection failed: {exc}"}

    if fundamental_hz <= 0:
        return {"error": "fundamental must be > 0"}

    requested = int(fft_size)
    max_n = min(requested, int(mono.size))
    n_fft = 1
    while n_fft * 2 <= max_n:
        n_fft *= 2
    n_fft = max(2, n_fft)

    start = max(0, (int(mono.size) - n_fft) // 2)
    slice_ = mono[start:start + n_fft].astype(np.float32)
    if slice_.size < n_fft:
        slice_ = np.pad(slice_, (0, n_fft - slice_.size))
    win = np.hanning(n_fft).astype(np.float32)
    spec = np.fft.rfft(slice_ * win)
    mag = np.abs(spec).astype(np.float64)
    freqs = librosa.fft_frequencies(sr=sr, n_fft=n_fft)
    bin_width_hz = float(sr) / float(n_fft)
    nyquist = float(sr) * 0.5
    peak_ref = float(np.max(mag)) + 1e-12

    partials = []
    for n in range(1, int(n_partials) + 1):
        expected = float(fundamental_hz) * n
        if expected >= nyquist:
            break
        target_bin = int(round(expected * n_fft / sr))
        lo = max(1, target_bin - 2)
        hi = min(len(mag), target_bin + 3)
        if hi <= lo:
            break
        local = lo + int(np.argmax(mag[lo:hi]))
        observed = float(freqs[local])
        amp_lin = float(mag[local])
        amp_db = float(20.0 * np.log10(amp_lin / peak_ref + 1e-12))
        partials.append({
            "n": int(n),
            "expected_hz": expected,
            "observed_hz": observed,
            "magnitude_linear": amp_lin,
            "magnitude_db": amp_db,
        })

    if len(partials) == 0:
        return {"error": "no partials in audible range"}

    a1 = partials[0]["magnitude_linear"]
    if a1 <= 1e-12:
        thd = 0.0
    else:
        higher_power = sum(p["magnitude_linear"] ** 2 for p in partials[1:])
        thd = float(np.sqrt(higher_power) / a1)
    thd_db = float(20.0 * np.log10(thd + 1e-12))

    deviations = [
        abs(p["observed_hz"] - p["expected_hz"]) / max(1.0, p["expected_hz"])
        for p in partials
    ]
    inharmonicity = float(np.mean(deviations))

    return {
        "fundamental_hz": float(fundamental_hz),
        "partials": partials,
        "thd": thd,
        "thd_db": thd_db,
        "inharmonicity": inharmonicity,
        "n_partials_found": len(partials),
        "fft_size_used": int(n_fft),
        "bin_width_hz": bin_width_hz,
    }


def analyze_aliasing(samples: np.ndarray, sr: int,
                      fundamental_hz: float | None = None,
                      n_partials: int = 20,
                      harmonic_window_bins: int = 2,
                      fft_size: int = 8192,
                      top_n_peaks: int = 8,
                      min_partials_present: int = 3,
                      flatness_max: float = 0.3,
                      pitch_confidence_min: float = 0.4) -> dict:
    """Detect non-harmonic / fold-back energy in a tonal signal.

    Aliasing in synthesis (naïve oscillators, broken wavetable interp,
    sample-rate handling bugs) shows up as energy at frequencies that
    aren't integer multiples of the fundamental — and especially as
    fold-back energy from would-be ultrasonic harmonics reflected below
    Nyquist.

    Two-stage gate before analyzing:
      1. Tonality — `pitch_confidence ≥ pitch_confidence_min` AND
         `spectral_flatness ≤ flatness_max`. Non-tonal signals (chords,
         noise, percussion) return `tonality_ok: False`.
      2. Partial presence — at least `min_partials_present` of the
         expected harmonics are actually visible above 5 % of peak.

    Heuristics for `is_likely_alias` on each non-harmonic peak:
      - **Above-comb**: peak frequency exceeds the highest visible
        harmonic by ≥ ½ × f0.
      - **Foldback**: the unfolded frequency `2·Nyquist − f` lands
        within ±harmonic_window_bins of an integer multiple of f0
        (i.e. the peak is the alias of an ultrasonic harmonic).

    Either firing flags the peak. Both can fire (above-comb foldbacks).
    """
    mono = to_mono(samples) if samples.ndim > 1 else samples
    if mono.size < 2048:
        return {"tonality_ok": False,
                "reason": "input too short for aliasing analysis"}

    scalars = analyze_scalars(samples, sr)
    flatness = float(scalars["spectral_flatness"])
    confidence = float(scalars["pitch_confidence"])
    failed = []
    if confidence < pitch_confidence_min:
        failed.append(f"pitch_confidence {confidence:.2f} < "
                      f"{pitch_confidence_min}")
    if flatness > flatness_max:
        failed.append(f"spectral_flatness {flatness:.2f} > {flatness_max}")
    if failed:
        return {
            "tonality_ok": False,
            "reason": "; ".join(failed),
            "spectral_flatness": flatness,
            "pitch_confidence": confidence,
        }

    if fundamental_hz is None or float(fundamental_hz) <= 0:
        f0 = float(scalars["fundamental_hz"])
        if f0 <= 0:
            return {
                "tonality_ok": False,
                "reason": "no fundamental detected",
                "spectral_flatness": flatness,
                "pitch_confidence": confidence,
            }
    else:
        f0 = float(fundamental_hz)

    requested = int(fft_size)
    max_n = min(requested, int(mono.size))
    n_fft = 1
    while n_fft * 2 <= max_n:
        n_fft *= 2
    n_fft = max(2, n_fft)

    start = max(0, (int(mono.size) - n_fft) // 2)
    slice_ = mono[start:start + n_fft].astype(np.float32)
    if slice_.size < n_fft:
        slice_ = np.pad(slice_, (0, n_fft - slice_.size))
    win = np.hanning(n_fft).astype(np.float32)
    spec = np.fft.rfft(slice_ * win)
    mag = np.abs(spec).astype(np.float64)
    freqs = librosa.fft_frequencies(sr=sr, n_fft=n_fft)
    bin_width = float(sr) / float(n_fft)
    nyquist = float(sr) * 0.5
    peak_ref = float(np.max(mag)) + 1e-12

    # Build harmonic mask + count partials actually present.
    harmonic_mask = np.zeros(len(mag), dtype=bool)
    partials_present = 0
    max_n_actual = 0
    presence_threshold = peak_ref * 0.05
    for n in range(1, int(n_partials) + 1):
        expected = f0 * n
        if expected >= nyquist:
            break
        target_bin = int(round(expected * n_fft / sr))
        lo = max(1, target_bin - harmonic_window_bins)
        hi = min(len(mag), target_bin + harmonic_window_bins + 1)
        if hi <= lo:
            continue
        harmonic_mask[lo:hi] = True
        local_max = float(np.max(mag[lo:hi]))
        if local_max > presence_threshold:
            partials_present += 1
            max_n_actual = n

    if partials_present < int(min_partials_present):
        return {
            "tonality_ok": False,
            "reason": f"too few partials present ({partials_present})",
            "fundamental_hz": f0,
            "spectral_flatness": flatness,
            "pitch_confidence": confidence,
            "n_partials_present": partials_present,
        }

    # Energy split — exclude DC + sub-fundamental noise (bins below f0/2).
    f0_half_bin = max(1, int(round((f0 / 2.0) * n_fft / sr)))
    valid_range = np.zeros(len(mag), dtype=bool)
    valid_range[f0_half_bin:] = True

    h_mask = harmonic_mask & valid_range
    nh_mask = (~harmonic_mask) & valid_range
    harmonic_energy = float(np.sum(mag[h_mask]))
    non_harmonic_energy = float(np.sum(mag[nh_mask]))
    total_energy = harmonic_energy + non_harmonic_energy
    aliasing_score = (non_harmonic_energy / total_energy
                      if total_energy > 1e-12 else 0.0)

    # Find top non-harmonic local-maxima peaks.
    nh_mag = mag.copy()
    nh_mag[~nh_mask] = 0.0
    peak_min = peak_ref * 0.01
    diffs_left = nh_mag[1:-1] > nh_mag[:-2]
    diffs_right = nh_mag[1:-1] > nh_mag[2:]
    above_min = nh_mag[1:-1] > peak_min
    peak_idx = np.where(diffs_left & diffs_right & above_min)[0] + 1
    peak_pairs = sorted(((int(i), float(nh_mag[i])) for i in peak_idx),
                          key=lambda kv: kv[1], reverse=True)
    peak_pairs = peak_pairs[:int(top_n_peaks)]

    top_peaks = []
    comb_extent_hz = f0 * (max_n_actual + 0.5)
    fold_window_hz = float(harmonic_window_bins) * bin_width
    for bin_idx, mag_lin in peak_pairs:
        f = float(freqs[bin_idx])
        n_nearest = max(1, int(round(f / f0)))
        nearest_hz = f0 * n_nearest
        distance_hz = abs(f - nearest_hz)
        above_comb = bool(f > comb_extent_hz)
        is_foldback = False
        f_unfolded = 2.0 * nyquist - f
        if f_unfolded > nyquist and f0 > 0:
            n_unfold = max(1, int(round(f_unfolded / f0)))
            if abs(f_unfolded - n_unfold * f0) <= fold_window_hz:
                is_foldback = True
        is_likely_alias = bool(above_comb or is_foldback)
        amp_db = float(20.0 * np.log10(mag_lin / peak_ref + 1e-12))
        top_peaks.append({
            "hz": f,
            "magnitude_db": amp_db,
            "magnitude_linear": float(mag_lin),
            "nearest_harmonic_n": int(n_nearest),
            "distance_hz": float(distance_hz),
            "is_likely_alias": is_likely_alias,
            "above_comb": above_comb,
            "is_foldback": is_foldback,
        })

    return {
        "tonality_ok": True,
        "fundamental_hz": float(f0),
        "spectral_flatness": flatness,
        "pitch_confidence": confidence,
        "n_partials_present": int(partials_present),
        "max_partial_n": int(max_n_actual),
        "harmonic_energy": harmonic_energy,
        "non_harmonic_energy": non_harmonic_energy,
        "aliasing_score": float(aliasing_score),
        "top_non_harmonic_peaks": top_peaks,
        "fft_size_used": int(n_fft),
        "bin_width_hz": float(bin_width),
    }


def measure_loudness(samples: np.ndarray, sr: int,
                      short_term: bool = True) -> dict:
    """ITU-R BS.1770 / EBU R128 perceptual loudness via pyloudnorm.

    Returns:
      integrated_lufs   — single LUFS value over the whole window
      short_term_lufs[] — sliding 3 s window every 0.1 s (when buffer ≥ 3 s)
      peak / peak_db    — max sample magnitude
      true_peak / true_peak_db — 4× oversampled peak (catches inter-sample
                                  peaks per BS.1770)
    """
    if samples.size == 0:
        return {"error": "empty input"}

    pyln = _pyloudnorm()
    data = samples.astype(np.float32)

    integrated_lufs = float("-inf")
    try:
        meter = pyln.Meter(int(sr))
        integrated_lufs = float(meter.integrated_loudness(data))
        if not np.isfinite(integrated_lufs):
            integrated_lufs = float("-inf")
    except Exception:
        integrated_lufs = float("-inf")

    short_term_track: list = []
    if short_term:
        window_samples = int(3.0 * sr)
        hop_samples = max(1, int(0.1 * sr))
        n_frames = int(data.shape[0] if data.ndim > 0 else 0)
        if n_frames >= window_samples:
            try:
                meter_st = pyln.Meter(int(sr))
                for i in range(0, n_frames - window_samples + 1, hop_samples):
                    chunk = data[i:i + window_samples]
                    try:
                        lufs = float(meter_st.integrated_loudness(chunk))
                        if np.isfinite(lufs):
                            short_term_track.append({
                                "t_ms": float(i / max(1, sr) * 1000.0),
                                "lufs": lufs,
                            })
                    except Exception:
                        continue
            except Exception:
                pass

    peak_lin = float(np.max(np.abs(data))) if data.size else 0.0
    peak_db = float(20.0 * np.log10(peak_lin + 1e-12))

    # True peak: 4× oversample each channel, take max.
    if data.ndim == 1:
        chans = [data]
    else:
        chans = [data[:, c] for c in range(data.shape[1])]
    true_peak_lin = peak_lin
    for ch in chans:
        if ch.size < 2:
            continue
        try:
            up = librosa.resample(ch.astype(np.float32),
                                    orig_sr=int(sr), target_sr=int(sr) * 4,
                                    res_type="kaiser_fast")
            ch_peak = float(np.max(np.abs(up)))
            if ch_peak > true_peak_lin:
                true_peak_lin = ch_peak
        except Exception:
            continue
    true_peak_db = float(20.0 * np.log10(true_peak_lin + 1e-12))

    return {
        "integrated_lufs": integrated_lufs,
        "short_term_lufs": short_term_track,
        "peak": peak_lin,
        "peak_db": peak_db,
        "true_peak": true_peak_lin,
        "true_peak_db": true_peak_db,
        "sample_rate": int(sr),
        "channels": int(samples.shape[1]) if samples.ndim > 1 else 1,
    }


def analyze_stereo_image(samples: np.ndarray, sr: int,
                          lissajous_size: int = 320,
                          downsample_to: int = 2000) -> dict:
    """Mid/side decomposition + balance + correlation, plus a Lissajous PNG.

    Mono input returns reduced fields and `lissajous_png_bytes: None`.
    """
    if samples.ndim == 1 or (samples.ndim > 1 and samples.shape[1] < 2):
        mono = samples if samples.ndim == 1 else samples[:, 0]
        rms = float(np.sqrt(np.mean(mono.astype(np.float64) ** 2)))
        return {
            "is_stereo": False,
            "left_rms": rms,
            "right_rms": rms,
            "balance_db": 0.0,
            "mid_rms": rms,
            "side_rms": 0.0,
            "mid_side_ratio_db": float("inf"),
            "correlation": 1.0,
            "lissajous_png_bytes": None,
        }

    L = samples[:, 0].astype(np.float64)
    R = samples[:, 1].astype(np.float64)
    L_rms = float(np.sqrt(np.mean(L * L)))
    R_rms = float(np.sqrt(np.mean(R * R)))
    balance_db = float(20.0 * np.log10((L_rms + 1e-12) / (R_rms + 1e-12)))

    mid = 0.5 * (L + R)
    side = 0.5 * (L - R)
    mid_rms = float(np.sqrt(np.mean(mid * mid)))
    side_rms = float(np.sqrt(np.mean(side * side)))
    if side_rms < 1e-9:
        ms_ratio_db = float("inf")
    else:
        ms_ratio_db = float(20.0 * np.log10(mid_rms / side_rms))

    if L.std() > 1e-9 and R.std() > 1e-9:
        correlation = float(np.corrcoef(L, R)[0, 1])
    else:
        correlation = 1.0

    png = render_lissajous_png(L, R, size=int(lissajous_size),
                                  downsample_to=int(downsample_to))

    return {
        "is_stereo": True,
        "left_rms": L_rms,
        "right_rms": R_rms,
        "balance_db": balance_db,
        "mid_rms": mid_rms,
        "side_rms": side_rms,
        "mid_side_ratio_db": ms_ratio_db,
        "correlation": correlation,
        "lissajous_png_bytes": png,
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

    # ZCR per librosa. Clamp the frame to fit short per-node ring buffers.
    zcr_frame = max(64, min(2048, int(mono.size)))
    zcr_hop = max(1, zcr_frame // 4)
    zcr = librosa.feature.zero_crossing_rate(y=mono, frame_length=zcr_frame,
                                              hop_length=zcr_hop)
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


def aggregate_scalar_runs(runs: list) -> dict:
    """Reduce a list of `analyze_scalars` outputs into per-metric statistics.

    Defensive against missing keys: a metric is only included in the
    output when at least one run has a finite numeric value for it.
    Returns `{"n_runs": 0, "stats": {}}` for an empty list.
    """
    if not runs:
        return {"n_runs": 0, "stats": {}}

    # Union of keys across runs; per-key vals filtered to finite floats.
    all_keys: set = set()
    for r in runs:
        if isinstance(r, dict):
            all_keys |= set(r.keys())

    stats: dict = {}
    for k in sorted(all_keys):
        vals = []
        for r in runs:
            if not isinstance(r, dict):
                continue
            v = r.get(k)
            if isinstance(v, bool):
                continue
            if isinstance(v, (int, float)) and np.isfinite(float(v)):
                vals.append(float(v))
        if not vals:
            continue
        arr = np.asarray(vals, dtype=np.float64)
        stats[k] = {
            "mean": float(np.mean(arr)),
            "std": float(np.std(arr, ddof=0)),
            "min": float(np.min(arr)),
            "max": float(np.max(arr)),
            "n": int(arr.size),
        }

    return {"n_runs": len(runs), "stats": stats}


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


def compute_mfcc_fingerprint(samples: np.ndarray, sr: int,
                              n_mfcc: int = 13,
                              n_fft: int = 2048,
                              hop_length: int = 512) -> np.ndarray:
    """Mean MFCC vector — a perception-aligned timbre summary.

    Returns a (n_mfcc,) array. Two recordings with similar timbre have
    similar fingerprints; Euclidean distance between them is a useful
    perceptual-similarity scalar.
    """
    mono = to_mono(samples) if samples.ndim > 1 else samples
    if mono.size < n_fft:
        return np.zeros(int(n_mfcc), dtype=np.float64)
    mfccs = librosa.feature.mfcc(y=mono.astype(np.float32), sr=int(sr),
                                   n_mfcc=int(n_mfcc), n_fft=int(n_fft),
                                   hop_length=int(hop_length))
    return np.mean(mfccs, axis=1).astype(np.float64)


def compare_to_reference(ref: np.ndarray, ref_sr: int,
                          cur: np.ndarray, cur_sr: int,
                          n_mfcc: int = 13) -> dict:
    """Scalar deltas + perceptual MFCC distance between current and reference.

    `mfcc_distance` is the Euclidean distance between mean-MFCC vectors;
    smaller = more timbrally similar. `mfcc_per_coefficient` is the
    per-coefficient delta — high values flag specific perceptual axes
    (low-order coefficients ≈ overall spectral shape; higher-order ≈
    finer timbral detail).
    """
    ref_m = analyze_scalars(ref, ref_sr)
    cur_m = analyze_scalars(cur, cur_sr)
    deltas = {k: cur_m[k] - ref_m[k] for k in ref_m if k in cur_m}

    ref_mfcc = compute_mfcc_fingerprint(ref, ref_sr, n_mfcc=n_mfcc)
    cur_mfcc = compute_mfcc_fingerprint(cur, cur_sr, n_mfcc=n_mfcc)
    if ref_mfcc.size == cur_mfcc.size and ref_mfcc.size > 0:
        diff = cur_mfcc - ref_mfcc
        mfcc_distance = float(np.linalg.norm(diff))
        mfcc_per_coefficient = [float(x) for x in diff.tolist()]
    else:
        mfcc_distance = float("nan")
        mfcc_per_coefficient = []

    return {
        "reference_metrics": ref_m,
        "current_metrics": cur_m,
        "deltas": deltas,
        "mfcc_distance": mfcc_distance,
        "mfcc_per_coefficient": mfcc_per_coefficient,
        "mfcc_n": int(n_mfcc),
    }
