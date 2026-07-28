from __future__ import annotations

import os
from pathlib import Path
from typing import Any

import yaml
from dotenv import load_dotenv


PROJECT_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_CONFIG = PROJECT_ROOT / "evaluation/ragas/config.yaml"
EXAMPLE_CONFIG = PROJECT_ROOT / "evaluation/ragas/config.example.yaml"


def load_settings(config_path: str | None = None) -> dict[str, Any]:
    load_dotenv(PROJECT_ROOT / ".env", override=False)
    path = Path(config_path).resolve() if config_path else DEFAULT_CONFIG
    if not path.exists():
        path = EXAMPLE_CONFIG
    with path.open("r", encoding="utf-8") as handle:
        settings = yaml.safe_load(handle) or {}
    settings["_config_path"] = str(path)
    return settings


def env_required(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise RuntimeError(f"required environment variable is missing: {name}")
    return value


def env_or_default(name: str, default: str) -> str:
    return os.getenv(name, "").strip() or default


def project_path(value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else PROJECT_ROOT / path
