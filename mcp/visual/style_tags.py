"""SigLIP-based visual style/mood descriptors.

Computes style tags by comparing an image's SigLIP embedding against
pre-computed text embeddings for ~35 visual descriptors. Uses cosine
similarity to find the best-matching descriptors.

Parallel to analysis/mood_tags.py (CLAP-based audio mood tags).
"""

import importlib.util
import logging
from typing import Any

import numpy as np

logger = logging.getLogger(__name__)

_torch_available = importlib.util.find_spec("torch") is not None

# ── Descriptor definitions ─────────────────────────────────────────────────────

DESCRIPTORS: list[dict[str, str]] = [
    # Visual style (12)
    {"tag": "abstract", "category": "visual_style", "description": "an abstract image with non-representational forms and shapes"},
    {"tag": "photorealistic", "category": "visual_style", "description": "a photorealistic image with lifelike detail and clarity"},
    {"tag": "geometric", "category": "visual_style", "description": "a geometric image with precise shapes, lines, and patterns"},
    {"tag": "organic", "category": "visual_style", "description": "an organic image with natural flowing curves and textures"},
    {"tag": "minimal", "category": "visual_style", "description": "a minimalist image with sparse elements and empty space"},
    {"tag": "maximalist", "category": "visual_style", "description": "a maximalist image dense with detail, pattern, and visual elements"},
    {"tag": "glitch", "category": "visual_style", "description": "a glitch art image with digital artifacts, distortion, and corruption"},
    {"tag": "retro", "category": "visual_style", "description": "a retro vintage image with nostalgic old-fashioned aesthetic"},
    {"tag": "futuristic", "category": "visual_style", "description": "a futuristic sci-fi image with advanced technology and sleek design"},
    {"tag": "painterly", "category": "visual_style", "description": "a painterly artistic image with visible brushstrokes and artistic texture"},
    {"tag": "graphic", "category": "visual_style", "description": "a bold graphic image with strong contrast, flat colors, and clear shapes"},
    {"tag": "cinematic", "category": "visual_style", "description": "a cinematic image with dramatic lighting, wide framing, and film-like quality"},
    # Mood (8)
    {"tag": "serene", "category": "mood", "description": "a serene calm peaceful tranquil image"},
    {"tag": "chaotic", "category": "mood", "description": "a chaotic disordered turbulent complex image"},
    {"tag": "tense", "category": "mood", "description": "a tense dramatic suspenseful intense image"},
    {"tag": "peaceful", "category": "mood", "description": "a peaceful harmonious gentle soothing image"},
    {"tag": "dreamy", "category": "mood", "description": "a dreamy ethereal soft hazy otherworldly image"},
    {"tag": "aggressive", "category": "mood", "description": "an aggressive bold harsh striking forceful image"},
    {"tag": "mysterious", "category": "mood", "description": "a mysterious dark enigmatic shadowy atmospheric image"},
    {"tag": "playful", "category": "mood", "description": "a playful fun colorful whimsical lighthearted image"},
    # Movement (7)
    {"tag": "static", "category": "movement", "description": "a static still motionless stable image"},
    {"tag": "flowing", "category": "movement", "description": "a flowing fluid smooth continuous movement image"},
    {"tag": "pulsing", "category": "movement", "description": "a pulsing rhythmic throbbing beating energy image"},
    {"tag": "sharp", "category": "movement", "description": "a sharp angular jagged pointed crisp image"},
    {"tag": "gentle", "category": "movement", "description": "a gentle soft subtle delicate smooth image"},
    {"tag": "turbulent", "category": "movement", "description": "a turbulent stormy wild swirling dynamic image"},
    {"tag": "rhythmic", "category": "movement", "description": "a rhythmic repeating patterned regular cadence image"},
    # Color mood (8)
    {"tag": "warm", "category": "color_mood", "description": "a warm image with red orange yellow golden tones"},
    {"tag": "cool", "category": "color_mood", "description": "a cool image with blue green teal cyan tones"},
    {"tag": "neon", "category": "color_mood", "description": "a neon image with bright glowing fluorescent electric colors"},
    {"tag": "muted", "category": "color_mood", "description": "a muted image with desaturated subdued faded colors"},
    {"tag": "pastel", "category": "color_mood", "description": "a pastel image with soft light delicate pale colors"},
    {"tag": "monochrome", "category": "color_mood", "description": "a monochrome image in black and white or single color tones"},
    {"tag": "saturated", "category": "color_mood", "description": "a highly saturated image with vivid intense rich colors"},
    {"tag": "earthy", "category": "color_mood", "description": "an earthy image with brown green ochre natural muted tones"},
]

# Lazy-loaded model and embedding cache
_siglip_model = None
_siglip_processor = None
_descriptor_embeddings: np.ndarray | None = None
_descriptor_embeddings_failed = False


def _get_device() -> str:
    """Get the best available device for inference."""
    if not _torch_available:
        return "cpu"
    import torch
    if torch.cuda.is_available():
        return "cuda"
    elif torch.backends.mps.is_available():
        return "mps"
    return "cpu"


def _load_siglip_model() -> tuple:
    """Load the SigLIP model (cached in module globals)."""
    global _siglip_model, _siglip_processor

    if _siglip_model is None:
        from transformers import AutoModel, AutoProcessor

        model_name = "google/siglip-base-patch16-224"
        logger.info(f"Loading SigLIP model: {model_name}")

        device = _get_device()
        logger.info(f"Using device: {device}")

        _siglip_processor = AutoProcessor.from_pretrained(model_name)
        _siglip_model = AutoModel.from_pretrained(model_name)
        _siglip_model = _siglip_model.to(device)
        _siglip_model.eval()

        logger.info("SigLIP model loaded successfully")

    return _siglip_model, _siglip_processor


def _extract_image_embedding(img) -> np.ndarray | None:
    """Extract SigLIP image embedding from a PIL Image.

    Returns:
        Embedding array or None on error.
    """
    if not _torch_available:
        logger.debug("SigLIP embeddings disabled (torch not available)")
        return None

    try:
        import torch

        model, processor = _load_siglip_model()
        device = _get_device()

        inputs = processor(images=img, return_tensors="pt")
        inputs = {k: v.to(device) for k, v in inputs.items()}

        with torch.no_grad():
            image_embed = model.get_image_features(**inputs)

        return image_embed.cpu().numpy().flatten()

    except Exception as e:
        logger.error(f"Error extracting SigLIP image embedding: {e}")
        return None


def _extract_text_embedding(text: str) -> np.ndarray | None:
    """Extract SigLIP text embedding."""
    if not _torch_available:
        return None

    try:
        import torch

        model, processor = _load_siglip_model()
        device = _get_device()

        inputs = processor(text=[text], return_tensors="pt", padding=True)
        inputs = {k: v.to(device) for k, v in inputs.items()}

        with torch.no_grad():
            text_embed = model.get_text_features(**inputs)

        return text_embed.cpu().numpy().flatten()

    except Exception as e:
        logger.error(f"Error extracting SigLIP text embedding for '{text}': {e}")
        return None


def _get_descriptor_embeddings() -> np.ndarray | None:
    """Get or compute descriptor text embeddings.

    Returns (N, D) array of SigLIP text embeddings for all descriptors,
    or None if SigLIP is not available.
    """
    global _descriptor_embeddings, _descriptor_embeddings_failed

    if _descriptor_embeddings is not None:
        return _descriptor_embeddings

    if _descriptor_embeddings_failed:
        return None

    try:
        embeddings = []
        for desc in DESCRIPTORS:
            emb = _extract_text_embedding(desc["description"])
            if emb is None:
                logger.warning("SigLIP not available for descriptor embeddings")
                _descriptor_embeddings_failed = True
                return None
            embeddings.append(emb)

        _descriptor_embeddings = np.array(embeddings, dtype=np.float32)
        logger.info(f"Computed {len(embeddings)} visual descriptor text embeddings")
        return _descriptor_embeddings

    except Exception as e:
        logger.warning(f"Failed to compute visual descriptor embeddings: {e}")
        _descriptor_embeddings_failed = True
        return None


def compute_style_tags(
    img,
    top_k: int = 8,
    min_confidence: float = 0.1,
) -> list[dict[str, Any]]:
    """Compute visual style tags for an image.

    Args:
        img: PIL Image
        top_k: Maximum number of tags to return
        min_confidence: Minimum cosine similarity threshold

    Returns:
        List of {"tag", "category", "confidence"} dicts, sorted by confidence descending.
    """
    # Get image embedding
    image_embedding = _extract_image_embedding(img)
    if image_embedding is None:
        return []

    desc_embeddings = _get_descriptor_embeddings()
    if desc_embeddings is None:
        return []

    # Normalize image embedding
    img_vec = image_embedding.astype(np.float32)
    img_norm = np.linalg.norm(img_vec)
    if img_norm < 1e-8:
        return []
    img_vec = img_vec / img_norm

    # Normalize descriptor embeddings
    desc_norms = np.linalg.norm(desc_embeddings, axis=1, keepdims=True)
    desc_normalized = desc_embeddings / (desc_norms + 1e-8)

    # Cosine similarity
    similarities = desc_normalized @ img_vec

    # Get top-K above threshold
    results: list[dict[str, Any]] = []
    for idx in np.argsort(similarities)[::-1]:
        sim = float(similarities[idx])
        if sim < min_confidence:
            break
        if len(results) >= top_k:
            break
        results.append({
            "tag": DESCRIPTORS[idx]["tag"],
            "category": DESCRIPTORS[idx]["category"],
            "confidence": round(sim, 3),
        })

    return results
