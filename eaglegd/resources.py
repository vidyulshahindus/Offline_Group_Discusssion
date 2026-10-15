"""Map Qt resource paths (:/...) to files under resources/."""

from __future__ import annotations

import json
from pathlib import Path

_PACKAGE_ROOT = Path(__file__).resolve().parent
_REPO_ROOT = _PACKAGE_ROOT.parent
_RESOURCES = _REPO_ROOT / "resources"


def normalize_qrc_path(resource_path: str) -> str:
    path = resource_path.strip()
    if path.startswith(":/"):
        path = path[2:]
    return path.lstrip("/")


def resource_path(qrc_path: str) -> Path:
    """Resolve a Qt-style resource path to a filesystem path."""
    rel = normalize_qrc_path(qrc_path)
    return _RESOURCES / rel


def load_json_document(qrc_path: str) -> dict | list | None:
    path = resource_path(qrc_path)
    if not path.is_file():
        return None
    with path.open(encoding="utf-8") as fh:
        return json.load(fh)
