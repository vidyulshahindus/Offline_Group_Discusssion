"""Minimal app config access for the LLM layer."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping, Protocol, Sequence


def _value_at(document: Mapping[str, Any], path: Sequence[str], default: Any = None) -> Any:
    node: Any = document
    for key in path:
        if not isinstance(node, Mapping) or key not in node:
            return default
        node = node[key]
    return node


class AppConfigProtocol(Protocol):
    def json(self) -> Mapping[str, Any]: ...

    def discussion_word_limit_overflow(self) -> float: ...

    def school_avoid_meta_school_words(self) -> bool: ...

    def llm_first_sentence_hint(self) -> bool: ...

    def llm_base_url(self) -> str: ...

    def llm_fallback_model(self) -> str: ...

    def llm_keep_alive(self) -> int: ...

    def llm_num_ctx(self) -> int: ...

    def llm_num_thread(self) -> int: ...

    def llm_num_batch(self) -> int: ...

    def llm_request_timeout_ms(self) -> int: ...


class AppConfig:
    """JSON-backed config with LLM-related accessors matching the C++ AppConfig."""

    def __init__(self, document: Mapping[str, Any] | None = None) -> None:
        self._document: dict[str, Any] = dict(document or {})

    @classmethod
    def load(cls, file_path: Path | str) -> AppConfig:
        path = Path(file_path)
        with path.open(encoding="utf-8") as f:
            return cls(json.load(f))

    def json(self) -> Mapping[str, Any]:
        return self._document

    def discussion_word_limit_overflow(self) -> float:
        val = _value_at(self._document, ("discussion", "word_limit_overflow"), 0.5)
        return float(val)

    def school_avoid_meta_school_words(self) -> bool:
        val = _value_at(self._document, ("school", "avoid_meta_school_words"), True)
        return bool(val)

    def llm_first_sentence_hint(self) -> bool:
        val = _value_at(self._document, ("llm", "first_sentence_hint"), True)
        return bool(val)

    def llm_base_url(self) -> str:
        return str(_value_at(self._document, ("llm", "base_url"), "http://127.0.0.1:11434"))

    def llm_fallback_model(self) -> str:
        return str(_value_at(self._document, ("llm", "fallback_model"), "qwen3.5:2b-q4_K_M"))

    def llm_keep_alive(self) -> int:
        return int(_value_at(self._document, ("llm", "keep_alive"), -1))

    def llm_num_ctx(self) -> int:
        return int(_value_at(self._document, ("llm", "num_ctx"), 4096))

    def llm_num_thread(self) -> int:
        return int(_value_at(self._document, ("llm", "num_thread"), 0))

    def llm_num_batch(self) -> int:
        return int(_value_at(self._document, ("llm", "num_batch"), 256))

    def llm_request_timeout_ms(self) -> int:
        return int(_value_at(self._document, ("llm", "request_timeout_ms"), 60000))
