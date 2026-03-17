"""CLAP audio embedding extraction.

Extracted from familiar/backend/app/services/analysis.py.
Loads the CLAP model lazily, extracts audio and text embeddings.
No database or Redis dependencies — pure inference.
"""

import importlib.util
import logging
import os

import numpy as np

# Check torch availability without importing it
_torch_available = importlib.util.find_spec("torch") is not None

logger = logging.getLogger(__name__)

# Lazy-loaded model globals
_clap_model = None
_clap_processor = None


def get_device() -> str:
    """Get the best available device for inference."""
    if not _torch_available:
        return "cpu"

    import torch

    if torch.cuda.is_available():
        return "cuda"
    elif torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def load_clap_model() -> tuple:
    """Load the CLAP model (cached in module globals)."""
    global _clap_model, _clap_processor

    if _clap_model is None:
        from transformers import ClapModel, ClapProcessor

        model_name = "laion/clap-htsat-unfused"
        logger.info(f"Loading CLAP model: {model_name}")

        device = get_device()
        logger.info(f"Using device: {device}")

        _clap_processor = ClapProcessor.from_pretrained(model_name)
        _clap_model = ClapModel.from_pretrained(model_name)
        _clap_model = _clap_model.to(device)
        _clap_model.eval()

        logger.info("CLAP model loaded successfully")

    return _clap_model, _clap_processor


def extract_embedding(file_path: str, target_sr: int = 48000) -> list[float] | None:
    """Extract CLAP audio embedding from file.

    Args:
        file_path: Path to audio file
        target_sr: Target sample rate for CLAP (48kHz recommended)

    Returns:
        512-dimensional embedding as list of floats, or None on error
    """
    if not _torch_available:
        logger.debug("CLAP embeddings disabled (torch not available)")
        return None

    try:
        import librosa
        import torch

        # Load audio file
        audio, sr = librosa.load(file_path, sr=target_sr, mono=True)

        # Limit to 10 seconds for embedding (CLAP works best with short clips)
        max_samples = target_sr * 10
        if len(audio) > max_samples:
            # Take middle section
            start = (len(audio) - max_samples) // 2
            audio = audio[start:start + max_samples]

        # Load model
        model, processor = load_clap_model()
        device = get_device()

        # Process audio
        inputs = processor(
            audio=audio,
            sampling_rate=target_sr,
            return_tensors="pt",
        )
        inputs = {k: v.to(device) for k, v in inputs.items()}

        # Get embedding
        with torch.no_grad():
            audio_embed = model.get_audio_features(**inputs)

        # Convert to list
        embedding = audio_embed.cpu().numpy().flatten().tolist()
        return embedding

    except Exception as e:
        logger.error(f"Error extracting embedding from {file_path}: {e}")
        return None


def extract_text_embedding(text: str) -> list[float] | None:
    """Extract CLAP text embedding from a text description.

    Returns:
        512-dimensional embedding as list of floats, or None if CLAP is disabled
    """
    if not _torch_available:
        logger.debug("CLAP text embeddings disabled (torch not available)")
        return None

    try:
        import torch

        model, processor = load_clap_model()
        device = get_device()

        inputs = processor(
            text=[text],
            return_tensors="pt",
            padding=True,
        )
        inputs = {k: v.to(device) for k, v in inputs.items()}

        with torch.no_grad():
            text_embed = model.get_text_features(**inputs)

        embedding = text_embed.cpu().numpy().flatten().tolist()
        return embedding

    except Exception as e:
        logger.error(f"Error extracting text embedding for '{text}': {e}")
        return None
