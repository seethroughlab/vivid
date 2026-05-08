"""Stub backend — returns deterministic canned responses for testing."""

import time

import numpy as np

from . import BackendBase


class StubBackend(BackendBase):
    @property
    def name(self) -> str:
        return "stub"

    @property
    def model(self) -> str:
        return "stub"

    @property
    def device(self) -> str:
        return "cpu"

    def evaluate(self, samples: np.ndarray, sample_rate: int, mode: str, prompt: str) -> dict:
        t0 = time.monotonic()
        duration = len(samples) / sample_rate
        time.sleep(min(0.2, duration * 0.01))  # simulate brief inference time
        elapsed = int((time.monotonic() - t0) * 1000)

        cot = ""
        if mode == "reasoning":
            cot = "<think>This is a stub response. No real reasoning is performed.</think>"

        return {
            "ok": True,
            "key": "C major",
            "tempo_bpm": 120.0,
            "summary": f"[stub] {duration:.1f}s of audio captured. Mode: {mode}.",
            "mode": mode,
            "model_response": f"[stub] Key: C major. Tempo: 120 BPM. {prompt or ''}".strip(),
            "cot_trace": cot,
            "timing_ms": elapsed,
            "backend": self.name,
        }

    def compare(
        self,
        samples_a: np.ndarray,
        samples_b: np.ndarray | None,
        intent: str,
        sample_rate: int,
    ) -> dict:
        t0 = time.monotonic()
        time.sleep(0.2)
        elapsed = int((time.monotonic() - t0) * 1000)

        has_ref = samples_b is not None
        summary = "[stub] "
        if has_ref and intent:
            summary += "Compared against reference clip and intent."
        elif has_ref:
            summary += "Compared against reference clip."
        else:
            summary += f"Evaluated against intent: {intent!r}."

        return {
            "ok": True,
            "match_score": 0.75,
            "key_deviations": ["[stub] No real deviations computed."],
            "summary": summary,
            "harmony_match": 0.75,
            "rhythm_match": 0.75,
            "timbre_match": 0.75,
            "structure_match": 0.75,
            "model_response": "[stub] Match analysis not available in stub mode.",
            "timing_ms": elapsed,
            "backend": self.name,
        }
