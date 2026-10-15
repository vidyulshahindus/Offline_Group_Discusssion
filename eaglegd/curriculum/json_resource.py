from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from eaglegd.resources import normalize_qrc_path
from eaglegd.resources import resource_path as resolve_resource_path


def load_document(qrc_or_path: str) -> dict[str, Any] | None:
    """Load JSON from a Qt resource path (:/...) or filesystem path."""
    if qrc_or_path.startswith(":/"):
        path = resolve_resource_path(qrc_or_path)
    else:
        path = Path(qrc_or_path)
    if not path.is_file():
        return None
    with path.open(encoding="utf-8") as fh:
        data = json.load(fh)
    return data if isinstance(data, dict) else None


def load_document_any(qrc_or_path: str) -> Any:
    rel = normalize_qrc_path(qrc_or_path)
    if qrc_or_path.startswith(":/"):
        path = resolve_resource_path(qrc_or_path)
    else:
        path = Path(qrc_or_path)
    if not path.is_file():
        fallback = rel if rel.startswith("data/") else f"data/{rel}"
        path = resolve_resource_path(fallback)
    if not path.is_file():
        return None
    with path.open(encoding="utf-8") as fh:
        return json.load(fh)
