"""Music evaluation sidecar service.

Start with:
    uv run uvicorn main:app --host 127.0.0.1 --port 9877

Or use the VIVID_MUSIC_EVAL_PORT env var to override the port:
    VIVID_MUSIC_EVAL_PORT=9878 uv run uvicorn main:app
"""

import asyncio
import logging
import threading
from contextlib import asynccontextmanager

import numpy as np
from fastapi import FastAPI
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

from audio_utils import decode_wav_b64, load_audio_file
from backends import BackendBase
from backends.stub import StubBackend
from config import settings

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [music_eval] %(levelname)s %(message)s",
)
log = logging.getLogger("music_eval")

# ---------------------------------------------------------------------------
# Global state
# ---------------------------------------------------------------------------

_backend: BackendBase = StubBackend()
_inference_lock = threading.Lock()
_queue_depth: int = 0
_queue_lock = threading.Lock()


def _make_backend(name: str, model_path: str, device: str, api_key: str = "") -> BackendBase:
    if name == "stub":
        return StubBackend()
    if name == "gemini":
        from backends.gemini import GeminiBackend  # noqa: PLC0415
        return GeminiBackend(model_id=model_path or "", api_key=api_key)
    raise ValueError(f"unknown backend: {name!r}")


@asynccontextmanager
async def lifespan(app: FastAPI):
    global _backend
    log.info("starting with backend=%s device=%s", settings.backend, settings.device)
    _backend = _make_backend(settings.backend, settings.model_path, settings.device, api_key=settings.api_key)
    log.info("ready on %s:%d", settings.host, settings.port)
    yield
    log.info("shutting down")


app = FastAPI(title="Vivid Music Eval", version="0.1.0", lifespan=lifespan)

# ---------------------------------------------------------------------------
# Request / response models
# ---------------------------------------------------------------------------

_VALID_MODES = {"caption", "theory", "reasoning"}
_VALID_BACKENDS = {"stub", "gemini"}


class EvaluateRequest(BaseModel):
    audio_b64: str = ""
    audio_path: str = ""
    sample_rate: int = 48000
    channels: int = 2
    start_seconds: float = 0.0
    duration_seconds: float | None = None
    mode: str = "caption"
    prompt: str = ""


class CompareRequest(BaseModel):
    audio_a_b64: str = ""
    audio_a_path: str = ""
    audio_b_b64: str = ""
    audio_b_path: str = ""
    intent: str = ""
    sample_rate: int = 48000
    channels: int = 2


class ConfigureRequest(BaseModel):
    backend: str
    model_path: str = ""
    api_key: str = ""
    device: str = "auto"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _err(code: str, message: str, status: int = 400) -> JSONResponse:
    return JSONResponse({"ok": False, "error": {"code": code, "message": message}}, status_code=status)


def _load_audio(
    b64: str,
    path: str,
    sample_rate: int,
    start_seconds: float = 0.0,
    duration_seconds: float | None = None,
) -> tuple[np.ndarray | None, JSONResponse | None]:
    """Decode audio from b64 or path. Returns (samples, None) or (None, error_response)."""
    if b64:
        try:
            samples, _ = decode_wav_b64(b64)
            return samples, None
        except ValueError as e:
            return None, _err("audio_decode_error", str(e))
    if path:
        try:
            samples, _ = load_audio_file(path, start_seconds=start_seconds, duration_seconds=duration_seconds)
            return samples, None
        except ValueError as e:
            return None, _err("reference_not_found", str(e))
    return None, _err("no_audio_source", "Provide audio_b64 or audio_path.")


def _run_inference(fn, *args, **kwargs):
    """Run a blocking inference call, tracking queue depth."""
    global _queue_depth
    with _queue_lock:
        _queue_depth += 1
    try:
        with _inference_lock:
            return fn(*args, **kwargs)
    finally:
        with _queue_lock:
            _queue_depth -= 1


# ---------------------------------------------------------------------------
# Endpoints
# ---------------------------------------------------------------------------

@app.post("/v1/evaluate")
async def evaluate(req: EvaluateRequest):
    if req.mode not in _VALID_MODES:
        return _err("invalid_mode", f"mode must be one of {sorted(_VALID_MODES)}")
    if req.start_seconds < 0:
        return _err("invalid_window", "start_seconds must be >= 0")
    if req.duration_seconds is not None and req.duration_seconds <= 0:
        return _err("invalid_window", "duration_seconds must be > 0")

    samples, err = _load_audio(
        req.audio_b64,
        req.audio_path,
        req.sample_rate,
        start_seconds=req.start_seconds,
        duration_seconds=req.duration_seconds,
    )
    if err:
        return err

    if len(samples) == 0:
        return _err("audio_too_short", "Captured audio buffer is empty.")

    loop = asyncio.get_event_loop()
    result = await loop.run_in_executor(
        None,
        lambda: _run_inference(_backend.evaluate, samples, req.sample_rate, req.mode, req.prompt),
    )
    log.info("evaluate mode=%s duration=%.1fs backend=%s timing=%dms",
             req.mode, len(samples) / req.sample_rate, _backend.name, result.get("timing_ms", 0))
    return JSONResponse(result)


@app.post("/v1/compare")
async def compare(req: CompareRequest):
    if not req.audio_a_b64 and not req.audio_a_path:
        return _err("no_audio_source", "Provide audio_a_b64 or audio_a_path for the current audio.")

    if not req.intent and not req.audio_b_b64 and not req.audio_b_path:
        return _err("no_audio_source", "Provide at least one of: audio_b_b64/audio_b_path (reference) or intent.")

    samples_a, err = _load_audio(req.audio_a_b64, req.audio_a_path, req.sample_rate)
    if err:
        return err

    samples_b: np.ndarray | None = None
    if req.audio_b_b64 or req.audio_b_path:
        samples_b, err = _load_audio(req.audio_b_b64, req.audio_b_path, req.sample_rate)
        if err:
            return err

    loop = asyncio.get_event_loop()
    result = await loop.run_in_executor(
        None,
        lambda: _run_inference(_backend.compare, samples_a, samples_b, req.intent, req.sample_rate),
    )
    log.info("compare has_ref=%s has_intent=%s backend=%s timing=%dms",
             samples_b is not None, bool(req.intent), _backend.name, result.get("timing_ms", 0))
    return JSONResponse(result)


@app.get("/v1/status")
async def status():
    with _queue_lock:
        qd = _queue_depth
    return JSONResponse({
        "backend": _backend.name,
        "model": _backend.model,
        "device": _backend.device,
        "ready": _backend.ready and qd == 0,
        "queue_depth": qd,
    })


@app.post("/v1/configure")
async def configure(req: ConfigureRequest):
    global _backend

    if req.backend not in _VALID_BACKENDS:
        return _err("invalid_backend", f"backend must be one of {sorted(_VALID_BACKENDS)}")

    effective_device = req.device if req.device != "auto" else settings.device

    try:
        new_backend = _make_backend(req.backend, req.model_path, effective_device, api_key=req.api_key)
    except Exception as e:
        return _err("inference_error", f"Failed to initialize backend: {e}", status=500)

    _backend = new_backend
    log.info("backend switched to %s", _backend.name)

    return JSONResponse({
        "ok": True,
        "backend": _backend.name,
        "model": _backend.model,
        "device": _backend.device,
        "message": "Backend switched." if req.backend != "stub" else "Stub backend active.",
    })
