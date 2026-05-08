"""Service configuration via environment variables and defaults."""

from dotenv import load_dotenv
from pydantic_settings import BaseSettings, SettingsConfigDict

load_dotenv()


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_prefix="VIVID_MUSIC_EVAL_")

    port: int = 9877
    host: str = "127.0.0.1"

    # Active backend — one of: stub, gemini
    backend: str = "stub"

    # Override HF repo ID or local path for the active backend
    model_path: str = ""

    # Gemini API key (memory only, never persisted)
    api_key: str = ""

    # Device selection: auto, cuda:0, mps, cpu
    device: str = "auto"

    # Set to 1 to load the model at startup rather than on first request
    preload_model: bool = False


settings = Settings()
