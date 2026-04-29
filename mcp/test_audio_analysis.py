"""Pure-function tests for mcp/audio_analysis.py.

These tests feed synthetic numpy arrays into the analysis/render helpers and
assert PNG signatures, JSON shapes, and pitch values on known sines. No
Vivid runtime involved — these run anywhere librosa + matplotlib are
installed.
"""

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


if __name__ == "__main__":
    unittest.main()
