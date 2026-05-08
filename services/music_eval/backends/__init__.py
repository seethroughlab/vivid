"""Backend ABC for LALM inference."""

from abc import ABC, abstractmethod

import numpy as np


class BackendBase(ABC):
    """All inference backends implement this interface.

    Methods receive float32 numpy arrays at the source sample rate.
    Backends are responsible for any resampling they need internally.
    """

    @property
    @abstractmethod
    def name(self) -> str:
        """Backend identifier matching the config enum."""
        ...

    @property
    @abstractmethod
    def model(self) -> str:
        """Human-readable model name or HF repo ID."""
        ...

    @property
    @abstractmethod
    def device(self) -> str:
        """Active compute device string, e.g. 'cuda:0', 'mps', 'cpu'."""
        ...

    @property
    def ready(self) -> bool:
        """True when the backend is loaded and able to serve requests."""
        return True

    @abstractmethod
    def evaluate(
        self,
        samples: np.ndarray,
        sample_rate: int,
        mode: str,
        prompt: str,
    ) -> dict:
        """Run single-clip analysis.

        Args:
            samples: float32 array (frames, channels)
            sample_rate: source sample rate (e.g. 48000)
            mode: "caption", "theory", or "reasoning"
            prompt: optional free-text hint for the model

        Returns dict with keys: ok, key, tempo_bpm, summary, mode,
            model_response, cot_trace, timing_ms, backend.
        """
        ...

    @abstractmethod
    def compare(
        self,
        samples_a: np.ndarray,
        samples_b: np.ndarray | None,
        intent: str,
        sample_rate: int,
    ) -> dict:
        """Compare two clips or one clip against an intent description.

        Args:
            samples_a: float32 array (frames, channels) — current audio
            samples_b: float32 array (frames, channels) or None — reference clip
            intent: free-text intent description (may be empty)
            sample_rate: source sample rate

        Returns dict with keys: ok, match_score, key_deviations, summary,
            harmony_match, rhythm_match, timbre_match, structure_match,
            model_response, timing_ms, backend.
        """
        ...
