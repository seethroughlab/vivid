"""Pure-function tests for mcp/audio_analysis.py.

These tests feed synthetic numpy arrays into the analysis/render helpers and
assert PNG signatures, JSON shapes, and pitch values on known sines. No
Vivid runtime involved — these run anywhere librosa + matplotlib are
installed.
"""

import base64
import io
import unittest

import numpy as np

from mcp import audio_analysis as aa


SR = 48000
PI = 3.141592653589793


def sine(freq: float, amp: float, seconds: float, sr: int = SR) -> np.ndarray:
    n = int(seconds * sr)
    t = np.arange(n) / sr
    return (amp * np.sin(2.0 * PI * freq * t)).astype(np.float32)


def stereo(left: np.ndarray, right: np.ndarray) -> np.ndarray:
    return np.stack([left, right], axis=1)


def is_png(b: bytes) -> bool:
    return len(b) >= 8 and b[:8] == b"\x89PNG\r\n\x1a\n"


class WavRoundTripTests(unittest.TestCase):
    def test_write_then_load_round_trip(self):
        s = sine(440.0, 0.5, 0.1)
        wav = aa.write_wav_bytes(s, SR)
        import base64
        b64 = base64.b64encode(wav).decode("ascii")
        loaded, sr = aa.load_wav_bytes(b64)
        self.assertEqual(sr, SR)
        self.assertEqual(loaded.shape, s.shape)
        # soundfile float32 round-trip is exact.
        np.testing.assert_allclose(loaded, s, atol=1e-6)


class PlotRendererTests(unittest.TestCase):
    def test_waveform_returns_png(self):
        s = sine(440.0, 0.4, 0.2)
        png = aa.render_waveform_png(s, SR, width=320, height=80)
        self.assertTrue(is_png(png))

    def test_waveform_handles_empty(self):
        png = aa.render_waveform_png(np.zeros(0, dtype=np.float32), SR)
        self.assertTrue(is_png(png))  # "no audio" fallback still produces a PNG

    def test_spectrogram_returns_png(self):
        s = sine(1000.0, 0.5, 0.5)
        png = aa.render_spectrogram_png(s, SR, fmin=0.0, fmax=4000.0,
                                          width=320, height=120)
        self.assertTrue(is_png(png))

    def test_envelope_returns_png(self):
        # half silence, half sine
        s = np.concatenate([np.zeros(SR // 4, dtype=np.float32),
                            sine(220.0, 0.5, 0.25)])
        png = aa.render_envelope_png(s, SR)
        self.assertTrue(is_png(png))

    def test_lane_strip_returns_png(self):
        lanes = [
            list(sine(200.0, 0.5, 0.05)),
            list(sine(400.0, 0.5, 0.05)),
            list(sine(600.0, 0.5, 0.05)),
        ]
        png = aa.render_lane_strip_png(lanes, width=320, lane_height=24)
        self.assertTrue(is_png(png))

    def test_lane_strip_with_ids_uses_color_by_id(self):
        # Two lanes with the same id (10) should render in the same color;
        # a third lane with a different id should render differently.
        # Smoke check only — we just verify it returns a valid PNG.
        lanes = [
            [0.5, -0.5] * 50,
            [0.5, -0.5] * 50,
            [0.5, -0.5] * 50,
        ]
        ids = [
            [10] * 100,
            [10] * 100,
            [20] * 100,
        ]
        png = aa.render_lane_strip_png(lanes, ids=ids)
        self.assertTrue(is_png(png))

    def test_spectrogram_diff_returns_png(self):
        ref = sine(440.0, 0.5, 0.3)
        cur = sine(450.0, 0.5, 0.3)
        png = aa.render_spectrogram_diff_png(ref, SR, cur, SR,
                                              width=400, height=160)
        self.assertTrue(is_png(png))


class ScalarAnalysisTests(unittest.TestCase):
    def test_pure_sine_scalars(self):
        s = sine(440.0, 0.5, 0.5)
        m = aa.analyze_scalars(s, SR)
        self.assertAlmostEqual(m["fundamental_hz"], 440.0, delta=5.0)
        self.assertGreater(m["pitch_confidence"], 0.5)
        self.assertAlmostEqual(m["dc_offset"], 0.0, places=3)
        self.assertGreater(m["rms"], 0.3)
        self.assertLess(m["rms"], 0.4)
        self.assertEqual(m["inter_channel_correlation"], 1.0)

    def test_dc_offset_detected(self):
        s = sine(440.0, 0.3, 0.5) + 0.1
        m = aa.analyze_scalars(s.astype(np.float32), SR)
        self.assertAlmostEqual(m["dc_offset"], 0.1, places=3)

    def test_white_noise_high_flatness(self):
        rng = np.random.default_rng(42)
        s = rng.standard_normal(SR // 2).astype(np.float32) * 0.3
        m = aa.analyze_scalars(s, SR)
        self.assertGreater(m["spectral_flatness"], 0.1)

    def test_stereo_uncorrelated(self):
        l = sine(440.0, 0.4, 0.2)
        r = sine(911.0, 0.4, 0.2)
        m = aa.analyze_scalars(stereo(l, r), SR)
        self.assertLess(abs(m["inter_channel_correlation"]), 0.5)


class DetailAnalysisTests(unittest.TestCase):
    def test_pitch_track_steady_tone(self):
        s = sine(440.0, 0.5, 0.5)
        d = aa.analyze_detail(s, SR, hop_ms=50.0)
        self.assertIn("pitch_track", d)
        # Most points should be near 440 Hz.
        near_440 = sum(1 for p in d["pitch_track"]
                        if 0.0 < p["hz"] < 600.0 and abs(p["hz"] - 440.0) < 15.0)
        self.assertGreater(near_440, 0)

    def test_band_energies_localized(self):
        # 100 ms of bass tone, then 100 ms of treble tone.
        s = np.concatenate([sine(200.0, 0.5, 0.1), sine(5000.0, 0.5, 0.1)])
        d = aa.analyze_detail(s, SR, hop_ms=20.0,
                                want_pitch_track=False,
                                want_onset_times=False)
        self.assertIn("band_energies", d)
        early = [p for p in d["band_energies"] if p["t"] < 0.08]
        late  = [p for p in d["band_energies"] if p["t"] > 0.12]
        self.assertTrue(any(p["bass"] > p["treble"] * 1.5 for p in early))
        self.assertTrue(any(p["treble"] > p["bass"] * 1.5 for p in late))

    def test_opt_out_keys(self):
        s = sine(440.0, 0.4, 0.2)
        d = aa.analyze_detail(s, SR,
                                want_pitch_track=False,
                                want_band_energies=False,
                                want_onset_times=False,
                                want_scalar_summary=False)
        self.assertNotIn("pitch_track", d)
        self.assertNotIn("band_energies", d)
        self.assertNotIn("onset_times", d)
        self.assertNotIn("scalar", d)
        # Metadata fields stay regardless.
        self.assertEqual(d["sample_rate"], SR)


class CompareToReferenceTests(unittest.TestCase):
    def test_identical_signals_zero_deltas(self):
        s = sine(440.0, 0.4, 0.2)
        out = aa.compare_to_reference(s, SR, s, SR)
        self.assertIn("deltas", out)
        self.assertAlmostEqual(out["deltas"]["rms"], 0.0, places=5)

    def test_pitch_shift_shows_in_delta(self):
        ref = sine(440.0, 0.4, 0.3)
        cur = sine(880.0, 0.4, 0.3)
        out = aa.compare_to_reference(ref, SR, cur, SR)
        self.assertGreater(out["deltas"]["fundamental_hz"], 100.0)


class DumpSamplesTests(unittest.TestCase):
    def test_round_trip_mono(self):
        s = sine(440.0, 0.5, 0.1)
        out = aa.dump_audio_samples(s, SR)
        self.assertEqual(out["format"], "f32le_base64")
        self.assertEqual(out["sample_rate"], SR)
        self.assertEqual(out["channels"], 1)
        self.assertEqual(out["frame_count"], s.shape[0])
        self.assertEqual(len(out["samples_b64"]), 1)
        decoded = np.frombuffer(base64.b64decode(out["samples_b64"][0]),
                                  dtype="<f4")
        np.testing.assert_allclose(decoded, s, atol=1e-6)

    def test_round_trip_stereo(self):
        left = sine(440.0, 0.5, 0.05)
        right = sine(660.0, 0.4, 0.05)
        s = stereo(left, right)
        out = aa.dump_audio_samples(s, SR)
        self.assertEqual(out["channels"], 2)
        self.assertEqual(len(out["samples_b64"]), 2)
        decoded_l = np.frombuffer(base64.b64decode(out["samples_b64"][0]),
                                    dtype="<f4")
        decoded_r = np.frombuffer(base64.b64decode(out["samples_b64"][1]),
                                    dtype="<f4")
        np.testing.assert_allclose(decoded_l, left, atol=1e-6)
        np.testing.assert_allclose(decoded_r, right, atol=1e-6)


class SpectrumTests(unittest.TestCase):
    def test_single_mode_peak_near_440(self):
        s = sine(440.0, 0.5, 0.5)
        out = aa.analyze_spectrum(s, SR, fft_size=4096, mode="single",
                                    db_scale=False)
        self.assertEqual(out["fft_size"], 4096)
        self.assertEqual(out["frames_averaged"], 1)
        peak = max(out["bins"], key=lambda b: b["magnitude"])
        # bin width = SR / fft_size ≈ 11.7 Hz at 4096 — allow within 1 bin.
        self.assertLess(abs(peak["hz"] - 440.0), SR / 4096.0 + 1.0)

    def test_average_mode_peak_near_440(self):
        s = sine(440.0, 0.5, 0.5)
        out = aa.analyze_spectrum(s, SR, fft_size=4096, mode="average",
                                    db_scale=False)
        self.assertGreater(out["frames_averaged"], 1)
        peak = max(out["bins"], key=lambda b: b["magnitude"])
        self.assertLess(abs(peak["hz"] - 440.0), SR / 4096.0 + 1.0)

    def test_auto_clamp_for_short_input(self):
        # 1024-sample input mimics the per-node waveform ring.
        s = sine(440.0, 0.5, 1024 / SR)
        out = aa.analyze_spectrum(s, SR, fft_size=4096, mode="single")
        self.assertEqual(out["fft_size_used"], 1024)
        self.assertIn("note", out)
        self.assertGreater(len(out["bins"]), 0)

    def test_band_filter(self):
        s = sine(440.0, 0.5, 0.3)
        out = aa.analyze_spectrum(s, SR, fft_size=4096, mode="single",
                                    fmin_hz=100.0, fmax_hz=2000.0)
        for b in out["bins"]:
            self.assertGreaterEqual(b["hz"], 100.0)
            self.assertLessEqual(b["hz"], 2000.0)

    def test_too_short_input(self):
        out = aa.analyze_spectrum(np.zeros(1, dtype=np.float32), SR,
                                    fft_size=1024)
        self.assertEqual(out["bins"], [])
        self.assertEqual(out["frames_averaged"], 0)


class DiscontinuityTests(unittest.TestCase):
    def test_clean_sine_has_no_events(self):
        s = sine(440.0, 0.5, 0.5)
        out = aa.detect_discontinuities(s, SR)
        self.assertEqual(out["events"], [])
        self.assertEqual(out["discontinuity_count_per_sec"], 0.0)

    def test_injected_click_is_detected(self):
        s = sine(440.0, 0.3, 1.0).copy()
        s[12000] = s[12000] + 0.5
        out = aa.detect_discontinuities(s, SR)
        # The discontinuity in x[12000] vs x[11999] surfaces at sample_idx
        # 12000; the symmetric drop x[12001] vs x[12000] may show too —
        # accept either exactly one or two adjacent events near 12000.
        self.assertGreaterEqual(len(out["events"]), 1)
        idxs = [e["sample_idx"] for e in out["events"]]
        self.assertTrue(any(11999 <= i <= 12001 for i in idxs))
        self.assertGreater(out["mad"], 0.0)
        self.assertGreaterEqual(out["threshold_used"], 0.05)

    def test_t_ms_consistent_with_sample_idx(self):
        s = sine(440.0, 0.3, 1.0).copy()
        s[24000] = s[24000] + 0.5
        out = aa.detect_discontinuities(s, SR)
        for e in out["events"]:
            expected = e["sample_idx"] / SR * 1000.0
            self.assertAlmostEqual(e["t_ms"], expected, places=3)

    def test_threshold_sweep(self):
        rng = np.random.default_rng(0)
        s = sine(440.0, 0.3, 0.5).copy()
        # Sprinkle a few clicks of varying magnitude.
        for idx in [4000, 8000, 12000]:
            s[idx] += 0.2
        out_loose = aa.detect_discontinuities(s, SR,
                                                threshold_multiplier=4.0)
        out_strict = aa.detect_discontinuities(s, SR,
                                                 threshold_multiplier=20.0)
        self.assertGreaterEqual(len(out_loose["events"]),
                                 len(out_strict["events"]))

    def test_per_sec_matches_events_length(self):
        s = sine(440.0, 0.3, 1.0).copy()
        s[10000] = s[10000] + 0.4
        s[20000] = s[20000] + 0.4
        out = aa.detect_discontinuities(s, SR)
        dur = s.shape[0] / SR
        self.assertAlmostEqual(out["discontinuity_count_per_sec"],
                                len(out["events"]) / dur, places=3)


class ClippingTests(unittest.TestCase):
    def test_clean_sine_no_clip(self):
        s = sine(440.0, 0.5, 0.5)
        out = aa.measure_clipping(s, SR)
        self.assertEqual(out["count"], 0)
        self.assertEqual(out["longest_run_samples"], 0)
        self.assertEqual(out["first_sample_idx"], -1)

    def test_full_amplitude_square_clips(self):
        # A constant ±1.0 signal: every sample clips at threshold 0.99.
        n = SR // 10
        s = np.ones(n, dtype=np.float32)
        out = aa.measure_clipping(s, SR)
        self.assertEqual(out["count"], n)
        self.assertEqual(out["longest_run_samples"], n)
        self.assertEqual(out["first_sample_idx"], 0)
        self.assertAlmostEqual(out["ratio"], 1.0, places=5)

    def test_short_clip_run_counted(self):
        s = sine(440.0, 0.3, 0.5).copy()
        s[1000:1100] = 1.0  # 100-sample clip
        out = aa.measure_clipping(s, SR)
        self.assertEqual(out["count"], 100)
        self.assertEqual(out["longest_run_samples"], 100)
        self.assertEqual(out["first_sample_idx"], 1000)

    def test_per_channel(self):
        left = np.zeros(SR // 10, dtype=np.float32)
        right = np.ones(SR // 10, dtype=np.float32)
        out = aa.measure_clipping(stereo(left, right), SR)
        self.assertEqual(out["channels"], 2)
        self.assertEqual(out["per_channel"][0]["count"], 0)
        self.assertEqual(out["per_channel"][1]["count"], right.size)


class DiscontinuityPatternTests(unittest.TestCase):
    def test_periodic_events(self):
        period = 256
        indices = list(range(period, SR, period))[:50]
        out = aa.analyze_discontinuity_pattern(indices, SR, fft_size=4096)
        self.assertTrue(out["periodic"])
        self.assertAlmostEqual(out["period_samples"], period, delta=2.0)
        # period_hz = SR / period
        self.assertAlmostEqual(out["period_hz"], SR / period, delta=1.0)
        # The most common interval should match.
        top = out["top_intervals"][0]
        self.assertEqual(top["samples"], period)

    def test_random_events(self):
        rng = np.random.default_rng(0)
        # Sparse random indices spanning a wide range — peak/mean stays low.
        indices = sorted(rng.choice(SR, size=20, replace=False).tolist())
        out = aa.analyze_discontinuity_pattern(indices, SR, fft_size=4096)
        # Random indices should NOT be classified as periodic.
        self.assertFalse(out["periodic"])

    def test_too_few_events(self):
        out = aa.analyze_discontinuity_pattern([10, 20], SR)
        self.assertIsNone(out["periodic"])
        self.assertIn("too few events", out["reason"])

    def test_zero_events(self):
        out = aa.analyze_discontinuity_pattern([], SR)
        self.assertIsNone(out["periodic"])

    def test_jitter_reported(self):
        # Slightly jittered periodic events.
        period = 200
        rng = np.random.default_rng(7)
        indices = [int(period * i + rng.integers(-3, 4))
                    for i in range(1, 40)]
        indices = sorted(indices)
        out = aa.analyze_discontinuity_pattern(indices, SR)
        self.assertGreater(out["interval_jitter_samples"], 0.0)


class DropoutTests(unittest.TestCase):
    def test_clean_sine_no_dropouts(self):
        s = sine(440.0, 0.5, 0.5)
        out = aa.detect_dropouts(s, SR, threshold=0.001, min_run_ms=10.0)
        self.assertEqual(out["dropouts"], [])

    def test_injected_gap_detected(self):
        s = sine(440.0, 0.5, 0.5).copy()
        # 50 ms gap centered at 200 ms.
        gap_start = SR // 5  # 200 ms in samples
        gap_len = SR // 20   # 50 ms
        s[gap_start:gap_start + gap_len] = 0.0
        out = aa.detect_dropouts(s, SR, threshold=0.001, min_run_ms=10.0)
        self.assertEqual(len(out["dropouts"]), 1)
        d = out["dropouts"][0]
        self.assertAlmostEqual(d["sample_idx"], gap_start, delta=2)
        self.assertAlmostEqual(d["duration_samples"], gap_len, delta=2)

    def test_below_min_run_ignored(self):
        s = sine(440.0, 0.5, 0.5).copy()
        # 5 ms gap — under default 10 ms minimum.
        gap_start = SR // 4
        gap_len = SR // 200  # 5 ms
        s[gap_start:gap_start + gap_len] = 0.0
        out = aa.detect_dropouts(s, SR, threshold=0.001, min_run_ms=10.0)
        self.assertEqual(out["dropouts"], [])

    def test_silent_buffer_is_one_dropout(self):
        s = np.zeros(SR // 4, dtype=np.float32)  # 250 ms of silence
        out = aa.detect_dropouts(s, SR, threshold=0.001, min_run_ms=10.0)
        self.assertEqual(len(out["dropouts"]), 1)
        self.assertAlmostEqual(out["dropouts"][0]["duration_samples"],
                                s.size, delta=1)


class EnvelopeDataTests(unittest.TestCase):
    def test_steady_sine_flat_envelope(self):
        s = sine(440.0, 0.5, 0.5)
        out = aa.dump_envelope_data(s, SR, hop_ms=5.0)
        self.assertEqual(out["channels"], 1)
        env = out["per_channel"][0]["envelope"]
        self.assertGreater(len(env), 10)
        # All RMS values should sit near 0.5/√2 ≈ 0.354.
        rms_vals = [p["rms"] for p in env]
        # Skip first/last few — windowing can pull them down slightly.
        mid = rms_vals[5:-5]
        self.assertGreater(min(mid), 0.30)
        self.assertLess(max(mid), 0.40)

    def test_attack_decay_shape(self):
        n = int(0.4 * SR)
        ramp = np.linspace(0.0, 0.8, n // 2, dtype=np.float32)
        decay = np.linspace(0.8, 0.0, n - n // 2, dtype=np.float32)
        carrier = sine(880.0, 1.0, 0.4)[:n]
        s = (np.concatenate([ramp, decay]) * carrier).astype(np.float32)
        out = aa.dump_envelope_data(s, SR, hop_ms=5.0)
        rms_vals = [p["rms"] for p in out["per_channel"][0]["envelope"]]
        # Peak should sit near the middle.
        peak_idx = int(np.argmax(rms_vals))
        self.assertGreater(peak_idx, len(rms_vals) // 4)
        self.assertLess(peak_idx, 3 * len(rms_vals) // 4)
        # First and last values should be small.
        self.assertLess(rms_vals[0], 0.1)
        self.assertLess(rms_vals[-1], 0.1)

    def test_too_short_input_returns_empty(self):
        out = aa.dump_envelope_data(np.zeros(5, dtype=np.float32), SR)
        self.assertEqual(out["per_channel"][0]["envelope"], [])


class AnalyzeDetailNodeIdTests(unittest.TestCase):
    """Regression: analyze_detail handles the per-node short-buffer case."""

    def test_full_window_unchanged(self):
        s = sine(440.0, 0.5, 0.5)
        d = aa.analyze_detail(s, SR, hop_ms=50.0)
        # All four blocks present with a ≥ 0.5 s sine.
        self.assertIn("scalar", d)
        self.assertIn("pitch_track", d)
        self.assertIn("band_energies", d)
        self.assertIn("onset_times", d)
        self.assertEqual(d["sample_rate"], SR)

    def test_short_per_node_skips_pitch(self):
        # 1024-sample input mimics the per-node waveform ring.
        s = sine(440.0, 0.5, 1024 / SR)
        d = aa.analyze_detail(s, SR, hop_ms=10.0)
        # Scalar is unconditional; pitch_track requires ≥ 2048 samples.
        self.assertIn("scalar", d)
        self.assertNotIn("pitch_track", d)
        # band_energies requires ≥ 1024 — should still be present.
        self.assertIn("band_energies", d)


class AnalyzeScalarsRefactorRegressionTests(unittest.TestCase):
    """Confirm the MAD factor-out into detect_discontinuities() did not
    drift the existing analyze_scalars output for known inputs."""

    def test_clean_sine_zero_count(self):
        s = sine(440.0, 0.5, 0.5)
        m = aa.analyze_scalars(s, SR)
        self.assertEqual(m["discontinuity_count_per_sec"], 0.0)

    def test_clicky_signal_nonzero_count(self):
        s = sine(440.0, 0.3, 1.0).copy()
        for idx in [10000, 20000, 30000]:
            s[idx] = s[idx] + 0.5
        m = aa.analyze_scalars(s, SR)
        self.assertGreater(m["discontinuity_count_per_sec"], 0.0)

    def test_count_matches_detect_helper(self):
        s = sine(440.0, 0.3, 1.0).copy()
        for idx in [10000, 20000]:
            s[idx] = s[idx] + 0.5
        m = aa.analyze_scalars(s, SR)
        d = aa.detect_discontinuities(s, SR)
        self.assertAlmostEqual(m["discontinuity_count_per_sec"],
                                d["discontinuity_count_per_sec"],
                                places=6)


class ExtractADSRTests(unittest.TestCase):
    def _ad_envelope_signal(self, sustain_ms: float):
        """Synthesize a 440 Hz tone with a piecewise AD-S-R envelope at SR.

        Attack 30 ms, decay 50 ms to sustain 0.4, sustain until sustain_ms,
        release 80 ms. Returns float32 mono samples.
        """
        n_attack = int(SR * 0.030)
        n_decay = int(SR * 0.050)
        n_release = int(SR * 0.080)
        n_sustain = max(0, int(SR * sustain_ms / 1000.0) - n_attack - n_decay)
        attack = np.linspace(0.0, 1.0, n_attack)
        decay = np.linspace(1.0, 0.4, n_decay)
        sustain = np.full(n_sustain, 0.4)
        release = np.linspace(0.4, 0.0, n_release)
        env = np.concatenate([attack, decay, sustain, release])
        carrier = sine(440.0, 1.0, env.size / SR)
        return (env * carrier).astype(np.float32)

    def test_extracts_attack_decay_sustain_release(self):
        s = self._ad_envelope_signal(sustain_ms=400.0)
        out = aa.extract_adsr(s, SR, sustain_ms=400.0)
        self.assertNotIn("error", out)
        # Attack: 30 ms, allow ±10 ms slack for envelope smoothing.
        self.assertAlmostEqual(out["attack_ms"], 30.0, delta=10.0)
        # Decay: 50 ms (peak → near sustain), ±20 ms.
        self.assertAlmostEqual(out["decay_ms"], 50.0, delta=20.0)
        # Sustain level: env is 0.4 modulating a sine — RMS ≈ 0.4/√2 ≈ 0.283.
        self.assertGreater(out["sustain_level"], 0.20)
        self.assertLess(out["sustain_level"], 0.35)
        # Release: 80 ms, ±25 ms.
        self.assertAlmostEqual(out["release_ms"], 80.0, delta=25.0)
        self.assertIn(out["attack_curve"], ("linear", "convex",
                                              "concave", "instant"))

    def test_too_short_returns_error(self):
        out = aa.extract_adsr(np.zeros(50, dtype=np.float32),
                                SR, sustain_ms=10.0)
        self.assertIn("error", out)

    def test_no_signal_returns_error(self):
        out = aa.extract_adsr(np.zeros(SR, dtype=np.float32),
                                SR, sustain_ms=300.0)
        self.assertIn("error", out)


class HarmonicProfileTests(unittest.TestCase):
    def test_pure_sine_low_thd(self):
        s = sine(440.0, 0.5, 0.5)
        out = aa.analyze_harmonic_profile(s, SR, n_partials=8)
        self.assertNotIn("error", out)
        self.assertAlmostEqual(out["fundamental_hz"], 440.0, delta=5.0)
        # Pure sine: fundamental dominates, higher partials are tiny.
        self.assertLess(out["thd"], 0.1)
        # Inharmonicity should be small (< 5 % of expected).
        self.assertLess(out["inharmonicity"], 0.05)
        self.assertEqual(out["partials"][0]["n"], 1)

    def test_square_wave_high_thd(self):
        # Build a band-limited approximation of a square wave: odd harmonics
        # at amplitude 1/n. THD should be substantial (≥ 0.4).
        n = SR // 2
        t = np.arange(n) / SR
        s = np.zeros(n, dtype=np.float32)
        for k in range(1, 12, 2):  # odd harmonics
            s += (1.0 / k) * np.sin(2.0 * np.pi * 440.0 * k * t).astype(np.float32)
        s *= 0.3
        out = aa.analyze_harmonic_profile(s, SR, n_partials=10)
        self.assertNotIn("error", out)
        self.assertGreater(out["thd"], 0.3)
        # Even harmonics (2nd, 4th) should be much weaker than odd.
        partials = {p["n"]: p["magnitude_linear"] for p in out["partials"]}
        if 2 in partials and 3 in partials:
            self.assertLess(partials[2], partials[3])

    def test_explicit_fundamental_skips_pyin(self):
        s = sine(880.0, 0.4, 0.5)
        out = aa.analyze_harmonic_profile(s, SR, fundamental_hz=880.0,
                                            n_partials=4)
        self.assertNotIn("error", out)
        self.assertEqual(out["fundamental_hz"], 880.0)

    def test_too_short_returns_error(self):
        out = aa.analyze_harmonic_profile(np.zeros(100, dtype=np.float32), SR)
        self.assertIn("error", out)


class LoudnessTests(unittest.TestCase):
    def test_louder_signal_higher_lufs(self):
        quiet = sine(440.0, 0.05, 4.0)
        loud = sine(440.0, 0.5, 4.0)
        q = aa.measure_loudness(quiet, SR, short_term=False)
        l = aa.measure_loudness(loud, SR, short_term=False)
        self.assertGreater(l["integrated_lufs"], q["integrated_lufs"])
        # Louder signal: peak_db should also be higher.
        self.assertGreater(l["peak_db"], q["peak_db"])

    def test_short_term_track_when_buffer_long(self):
        s = sine(440.0, 0.3, 4.0)
        out = aa.measure_loudness(s, SR, short_term=True)
        self.assertGreater(len(out["short_term_lufs"]), 0)
        # All entries should have finite LUFS values.
        self.assertTrue(all(np.isfinite(p["lufs"])
                              for p in out["short_term_lufs"]))

    def test_silent_buffer_returns_minus_inf(self):
        s = np.zeros(SR * 4, dtype=np.float32)
        out = aa.measure_loudness(s, SR, short_term=False)
        self.assertEqual(out["integrated_lufs"], float("-inf"))
        self.assertEqual(out["peak"], 0.0)

    def test_short_term_skipped_when_buffer_too_short(self):
        s = sine(440.0, 0.3, 0.5)  # 0.5 s — under the 3 s window
        out = aa.measure_loudness(s, SR, short_term=True)
        self.assertEqual(out["short_term_lufs"], [])


class StereoImageTests(unittest.TestCase):
    def test_mono_input(self):
        s = sine(440.0, 0.5, 0.2)
        out = aa.analyze_stereo_image(s, SR)
        self.assertFalse(out["is_stereo"])
        self.assertEqual(out["correlation"], 1.0)
        self.assertIsNone(out["lissajous_png_bytes"])

    def test_identical_channels_correlation_one(self):
        s = sine(440.0, 0.5, 0.2)
        out = aa.analyze_stereo_image(stereo(s, s), SR)
        self.assertTrue(out["is_stereo"])
        self.assertAlmostEqual(out["correlation"], 1.0, places=4)
        # All energy is in mid; side ≈ 0.
        self.assertGreater(out["mid_rms"], 0.1)
        self.assertLess(out["side_rms"], 1e-3)
        self.assertTrue(is_png(out["lissajous_png_bytes"]))

    def test_inverted_channels_negative_correlation(self):
        s = sine(440.0, 0.5, 0.2)
        out = aa.analyze_stereo_image(stereo(s, -s), SR)
        self.assertAlmostEqual(out["correlation"], -1.0, places=3)
        # All energy is in side; mid ≈ 0.
        self.assertLess(out["mid_rms"], 1e-3)
        self.assertGreater(out["side_rms"], 0.1)

    def test_imbalance_reported(self):
        loud = sine(440.0, 0.5, 0.2)
        soft = sine(440.0, 0.05, 0.2)
        out = aa.analyze_stereo_image(stereo(loud, soft), SR)
        # Left is 10× louder ⇒ ~+20 dB balance.
        self.assertGreater(out["balance_db"], 15.0)

    def test_uncorrelated_channels_low_correlation(self):
        rng = np.random.default_rng(11)
        l = rng.standard_normal(SR // 5).astype(np.float32) * 0.3
        r = rng.standard_normal(SR // 5).astype(np.float32) * 0.3
        out = aa.analyze_stereo_image(stereo(l, r), SR)
        self.assertLess(abs(out["correlation"]), 0.2)


class MFCCDistanceTests(unittest.TestCase):
    def test_identical_zero_distance(self):
        s = sine(440.0, 0.4, 0.5)
        out = aa.compare_to_reference(s, SR, s, SR)
        self.assertIn("mfcc_distance", out)
        self.assertAlmostEqual(out["mfcc_distance"], 0.0, places=4)
        self.assertEqual(len(out["mfcc_per_coefficient"]), 13)

    def test_different_timbres_nonzero_distance(self):
        # Pure sine vs. white noise — wildly different spectra.
        sine_sig = sine(440.0, 0.4, 0.5)
        rng = np.random.default_rng(99)
        noise = rng.standard_normal(int(0.5 * SR)).astype(np.float32) * 0.3
        out = aa.compare_to_reference(sine_sig, SR, noise, SR)
        self.assertGreater(out["mfcc_distance"], 5.0)

    def test_similar_timbres_smaller_distance(self):
        # Two sines — same timbre family — should have small MFCC distance.
        a = sine(440.0, 0.4, 0.5)
        b = sine(660.0, 0.4, 0.5)
        out_close = aa.compare_to_reference(a, SR, b, SR)
        # Sine vs. noise from above (different timbres):
        rng = np.random.default_rng(7)
        noise = rng.standard_normal(int(0.5 * SR)).astype(np.float32) * 0.3
        out_far = aa.compare_to_reference(a, SR, noise, SR)
        self.assertLess(out_close["mfcc_distance"], out_far["mfcc_distance"])


class AliasingTests(unittest.TestCase):
    def _harmonic_stack(self, f0: float, partials: int, seconds: float = 0.5):
        """Sum of sines at f0, 2f0, 3f0, ... (1/n amplitude). Tonal, clean."""
        n = int(seconds * SR)
        t = np.arange(n) / SR
        s = np.zeros(n, dtype=np.float64)
        for k in range(1, partials + 1):
            s += (1.0 / k) * np.sin(2.0 * np.pi * f0 * k * t)
        return (s * 0.25).astype(np.float32)

    def test_clean_tonal_signal_low_aliasing(self):
        s = self._harmonic_stack(220.0, partials=8)
        out = aa.analyze_aliasing(s, SR, fundamental_hz=220.0)
        self.assertTrue(out["tonality_ok"])
        self.assertLess(out["aliasing_score"], 0.10)

    def test_injected_non_harmonic_raises_score(self):
        clean = self._harmonic_stack(220.0, partials=8)
        n = clean.size
        t = np.arange(n) / SR
        # Inject a small non-harmonic sine at 350 Hz (between harmonics 1
        # and 2 of f0=220). Amplitude is low (4 % of fundamental peak)
        # so pyin still locks onto 220 — the tonality gate stays open.
        bad = clean + (0.04 * np.sin(2.0 * np.pi * 350.0 * t)).astype(np.float32)
        out_clean = aa.analyze_aliasing(clean, SR, fundamental_hz=220.0)
        out_bad = aa.analyze_aliasing(bad, SR, fundamental_hz=220.0)
        self.assertTrue(out_bad["tonality_ok"])
        self.assertGreater(out_bad["aliasing_score"],
                            out_clean["aliasing_score"] + 0.005)
        # The injected peak should appear in the top-N.
        hits = [p for p in out_bad["top_non_harmonic_peaks"]
                if abs(p["hz"] - 350.0) < 10.0]
        self.assertTrue(len(hits) > 0)

    def test_above_comb_peak_flagged_as_alias(self):
        # f0 = 220 Hz, harmonics up to 8×220 = 1760 Hz. Inject sine at 8000 Hz.
        clean = self._harmonic_stack(220.0, partials=8)
        n = clean.size
        t = np.arange(n) / SR
        bad = clean + (0.1 * np.sin(2.0 * np.pi * 8000.0 * t)).astype(np.float32)
        out = aa.analyze_aliasing(bad, SR, fundamental_hz=220.0, n_partials=8)
        # The injected peak is well above the comb extent (8 × 220 = 1760 Hz)
        # and should be flagged.
        hits = [p for p in out["top_non_harmonic_peaks"]
                if abs(p["hz"] - 8000.0) < 50.0]
        self.assertTrue(len(hits) > 0)
        self.assertTrue(hits[0]["above_comb"])
        self.assertTrue(hits[0]["is_likely_alias"])

    def test_too_few_partials_returns_not_ok(self):
        # Pure sine — only 1 partial in the comb.
        s = sine(440.0, 0.5, 0.5)
        out = aa.analyze_aliasing(s, SR, fundamental_hz=440.0)
        self.assertFalse(out["tonality_ok"])
        self.assertIn("partials", out["reason"])

    def test_white_noise_rejected(self):
        rng = np.random.default_rng(33)
        s = rng.standard_normal(SR // 2).astype(np.float32) * 0.3
        out = aa.analyze_aliasing(s, SR)
        self.assertFalse(out["tonality_ok"])

    def test_explicit_fundamental_used_directly(self):
        s = self._harmonic_stack(330.0, partials=6)
        out = aa.analyze_aliasing(s, SR, fundamental_hz=330.0)
        self.assertTrue(out["tonality_ok"])
        self.assertEqual(out["fundamental_hz"], 330.0)


class AggregationTests(unittest.TestCase):
    def test_mean_and_spread_three_runs(self):
        runs = [
            {"rms": 0.1, "peak": 1.0, "fundamental_hz": 440.0},
            {"rms": 0.2, "peak": 1.0, "fundamental_hz": 440.0},
            {"rms": 0.3, "peak": 1.0, "fundamental_hz": 440.0},
        ]
        out = aa.aggregate_scalar_runs(runs)
        self.assertEqual(out["n_runs"], 3)
        self.assertAlmostEqual(out["stats"]["rms"]["mean"], 0.2, places=6)
        self.assertAlmostEqual(out["stats"]["rms"]["std"], 0.0816,
                                places=3)
        self.assertEqual(out["stats"]["rms"]["min"], 0.1)
        self.assertEqual(out["stats"]["rms"]["max"], 0.3)
        self.assertEqual(out["stats"]["rms"]["n"], 3)
        # Constant metric: std == 0.
        self.assertEqual(out["stats"]["peak"]["std"], 0.0)

    def test_single_run(self):
        out = aa.aggregate_scalar_runs([{"rms": 0.42}])
        self.assertEqual(out["n_runs"], 1)
        self.assertEqual(out["stats"]["rms"]["mean"], 0.42)
        self.assertEqual(out["stats"]["rms"]["std"], 0.0)
        self.assertEqual(out["stats"]["rms"]["min"], 0.42)
        self.assertEqual(out["stats"]["rms"]["max"], 0.42)

    def test_empty_runs(self):
        out = aa.aggregate_scalar_runs([])
        self.assertEqual(out["n_runs"], 0)
        self.assertEqual(out["stats"], {})

    def test_mixed_keys_skips_missing(self):
        # One run is missing 'peak' — that metric should still compute
        # over the runs that have it, not crash.
        runs = [
            {"rms": 0.1, "peak": 0.5},
            {"rms": 0.2},  # no peak
            {"rms": 0.3, "peak": 0.7},
        ]
        out = aa.aggregate_scalar_runs(runs)
        self.assertEqual(out["stats"]["rms"]["n"], 3)
        self.assertEqual(out["stats"]["peak"]["n"], 2)
        self.assertAlmostEqual(out["stats"]["peak"]["mean"], 0.6, places=6)

    def test_non_numeric_values_skipped(self):
        runs = [
            {"rms": 0.5, "label": "test"},
            {"rms": 0.6, "label": "test"},
        ]
        out = aa.aggregate_scalar_runs(runs)
        self.assertIn("rms", out["stats"])
        # Non-numeric 'label' should not appear in stats.
        self.assertNotIn("label", out["stats"])

    def test_against_real_analyze_scalars(self):
        # End-to-end: run analyze_scalars on three near-identical sines,
        # check that aggregation returns sensible stats.
        s1 = sine(440.0, 0.5, 0.3)
        s2 = sine(440.0, 0.5, 0.3)
        s3 = sine(440.0, 0.5, 0.3)
        runs = [aa.analyze_scalars(s, SR) for s in (s1, s2, s3)]
        out = aa.aggregate_scalar_runs(runs)
        self.assertEqual(out["n_runs"], 3)
        # All three runs are identical — std should be ~0 across metrics.
        self.assertLess(out["stats"]["rms"]["std"], 1e-6)
        self.assertLess(out["stats"]["fundamental_hz"]["std"], 1.0)


if __name__ == "__main__":
    unittest.main()
