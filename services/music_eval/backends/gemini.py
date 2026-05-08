"""Gemini audio backend — routes inference through Google's Gemini API.

Requires: google-genai
Install:  uv add google-genai

Default model: gemini-2.5-flash
Override via model_path (treated as model ID): e.g. "gemini-2.5-pro", "gemini-2.0-flash"

API key priority:
  1. api_key passed to configure_music_eval_backend
  2. GOOGLE_API_KEY environment variable
"""

import io
import os
import re
import time

import numpy as np
import soundfile as sf

from . import BackendBase

_DEFAULT_MODEL = "gemini-2.5-flash"


def _parse_key_tempo(text: str) -> tuple[str | None, float | None]:
    key = None
    tempo = None
    key_match = re.search(
        r"\b([A-G][#b]?\s*(?:major|minor|lydian|dorian|phrygian|mixolydian|locrian|aeolian))\b",
        text,
        re.IGNORECASE,
    )
    if key_match:
        key = key_match.group(1).strip()
    tempo_match = re.search(r"(\d{2,3}(?:\.\d)?)\s*(?:BPM|bpm|beats\s*per\s*minute)", text)
    if tempo_match:
        try:
            tempo = float(tempo_match.group(1))
        except ValueError:
            pass
    return key, tempo


def _parse_score(pattern: str, text: str) -> float | None:
    m = re.search(pattern, text, re.IGNORECASE)
    if m:
        try:
            return min(1.0, max(0.0, float(m.group(1))))
        except ValueError:
            pass
    return None


def _to_wav_bytes(samples: np.ndarray, sample_rate: int) -> bytes:
    buf = io.BytesIO()
    sf.write(buf, samples, sample_rate, format="WAV", subtype="PCM_16")
    return buf.getvalue()


class GeminiBackend(BackendBase):
    def __init__(self, model_id: str = "", api_key: str = "") -> None:
        self._model_id = model_id or _DEFAULT_MODEL
        self._api_key = api_key or os.environ.get("GOOGLE_API_KEY", "")
        self._client = None

    def _ensure_client(self):
        if self._client is not None:
            return
        try:
            from google import genai
        except ImportError:
            raise RuntimeError(
                "Gemini backend requires google-genai. "
                "Install with: uv add google-genai"
            )
        if not self._api_key:
            raise RuntimeError(
                "Gemini backend requires an API key. "
                "Pass api_key to configure_music_eval_backend or set GOOGLE_API_KEY."
            )
        self._client = genai.Client(api_key=self._api_key)

    @property
    def name(self) -> str:
        return "gemini"

    @property
    def model(self) -> str:
        return self._model_id

    @property
    def device(self) -> str:
        return "api"

    @property
    def ready(self) -> bool:
        return True

    def evaluate(
        self,
        samples: np.ndarray,
        sample_rate: int,
        mode: str,
        prompt: str,
    ) -> dict:
        t0 = time.monotonic()
        try:
            self._ensure_client()
        except RuntimeError as exc:
            return {"ok": False, "error": {"code": "model_not_loaded", "message": str(exc)}}

        _prompts = {
            "caption": (
                "Analyze this audio track and provide:\n"
                "1. The musical key (e.g. 'F minor', 'C# major')\n"
                "2. The tempo in BPM\n"
                "3. Instrumentation and production style\n"
                "4. Overall mood and aesthetic character\n"
                "5. Any notable structural or harmonic features\n"
                "Be specific and concise."
            ),
            "theory": (
                "Provide a detailed music theory analysis of this audio track:\n"
                "1. Key and mode (e.g. 'D Dorian')\n"
                "2. Tempo in BPM and rhythmic feel\n"
                "3. Chord progressions and harmonic language\n"
                "4. Rhythmic structure and groove\n"
                "5. Instrumentation and timbral character\n"
                "6. Overall form and arrangement structure\n"
                "Use precise music theory terminology."
            ),
            "reasoning": (
                "Think step by step about this audio track. "
                "First identify the rhythm and tempo, then the harmonic content and key, "
                "then the instrumentation and production style, then the overall mood. "
                "State the key (e.g. 'F minor') and tempo in BPM explicitly. "
                "Show your reasoning process."
            ),
        }
        text = _prompts.get(mode, _prompts["caption"])
        if prompt:
            text += f"\n\nAdditional focus: {prompt}"

        try:
            from google.genai import types as genai_types
            wav = _to_wav_bytes(samples, sample_rate)
            response = self._client.models.generate_content(
                model=self._model_id,
                contents=[text, genai_types.Part.from_bytes(data=wav, mime_type="audio/wav")],
            )
            result_text = response.text
            key, tempo = _parse_key_tempo(result_text)
            elapsed_ms = int((time.monotonic() - t0) * 1000)
            return {
                "ok": True,
                "key": key,
                "tempo_bpm": tempo,
                "summary": result_text,
                "mode": mode,
                "model_response": result_text,
                "cot_trace": "",
                "timing_ms": elapsed_ms,
                "backend": self.name,
            }
        except Exception as exc:
            return {"ok": False, "error": {"code": "inference_error", "message": str(exc)}}

    def compare(
        self,
        samples_a: np.ndarray,
        samples_b: np.ndarray | None,
        intent: str,
        sample_rate: int,
    ) -> dict:
        t0 = time.monotonic()
        try:
            self._ensure_client()
        except RuntimeError as exc:
            return {"ok": False, "error": {"code": "model_not_loaded", "message": str(exc)}}

        try:
            from google.genai import types as genai_types
            audio_a = genai_types.Part.from_bytes(
                data=_to_wav_bytes(samples_a, sample_rate), mime_type="audio/wav"
            )

            if samples_b is not None:
                audio_b = genai_types.Part.from_bytes(
                    data=_to_wav_bytes(samples_b, sample_rate), mime_type="audio/wav"
                )
                text = (
                    "Compare these two audio clips. The first is the current output; "
                    "the second is the reference."
                )
                if intent:
                    text += f" The intended sound is: {intent}."
                text += (
                    "\n\nFor each axis, give a score 0.0–1.0 on its own line in the format "
                    "'label: score' then explain:\n"
                    "harmony_match: <score>\n"
                    "rhythm_match: <score>\n"
                    "timbre_match: <score>\n"
                    "structure_match: <score>\n"
                    "match_score: <score>\n\n"
                    "Then list the key deviations (most important first), "
                    "one per line starting with a dash.\n"
                    "End with a one-sentence summary."
                )
                parts = [text, audio_a, audio_b]
            else:
                text = (
                    f"Evaluate this audio against the following intent: {intent}\n\n"
                    "For each axis, give a score 0.0–1.0 on its own line in the format "
                    "'label: score' then explain:\n"
                    "harmony_match: <score>\n"
                    "rhythm_match: <score>\n"
                    "timbre_match: <score>\n"
                    "structure_match: <score>\n"
                    "match_score: <score>\n\n"
                    "Then list the key deviations (most important first), "
                    "one per line starting with a dash.\n"
                    "End with a one-sentence summary."
                )
                parts = [text, audio_a]

            response = self._client.models.generate_content(
                model=self._model_id,
                contents=parts,
            )
            result_text = response.text

            match_score = _parse_score(r"match[_\s]score[:\s]+([0-9.]+)", result_text) or 0.5
            harmony  = _parse_score(r"harmony[_\s]match[:\s]+([0-9.]+)", result_text)
            rhythm   = _parse_score(r"rhythm[_\s]match[:\s]+([0-9.]+)", result_text)
            timbre   = _parse_score(r"timbre[_\s]match[:\s]+([0-9.]+)", result_text)
            structure = _parse_score(r"structure[_\s]match[:\s]+([0-9.]+)", result_text)
            harmony   = harmony   if harmony   is not None else match_score
            rhythm    = rhythm    if rhythm    is not None else match_score
            timbre    = timbre    if timbre    is not None else match_score
            structure = structure if structure is not None else match_score

            deviations: list[str] = []
            for line in result_text.splitlines():
                stripped = line.strip().lstrip("•-* ")
                if stripped and any(
                    w in stripped.lower()
                    for w in ("differ", "mismatch", "deviation", "unlike", "whereas", "however", "but ")
                ):
                    deviations.append(stripped)
            if not deviations and result_text:
                deviations = [result_text.split(".")[0].strip() + "."]

            summary = next(
                (p.strip() for p in result_text.split("\n\n") if p.strip()),
                result_text[:300],
            )
            elapsed_ms = int((time.monotonic() - t0) * 1000)

            return {
                "ok": True,
                "match_score": match_score,
                "key_deviations": deviations[:5],
                "summary": summary,
                "harmony_match": harmony,
                "rhythm_match": rhythm,
                "timbre_match": timbre,
                "structure_match": structure,
                "model_response": result_text,
                "timing_ms": elapsed_ms,
                "backend": self.name,
            }
        except Exception as exc:
            return {"ok": False, "error": {"code": "inference_error", "message": str(exc)}}
