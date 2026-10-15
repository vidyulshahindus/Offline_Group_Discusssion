"""Token estimation helpers (ported from TokenEstimator)."""

from __future__ import annotations

import math
from typing import Mapping

from eaglegd.llm.config import AppConfigProtocol
from eaglegd.llm.prompt_types import SessionLanguage


def _lang_key(language: SessionLanguage) -> str:
    if language == SessionLanguage.Hindi:
        return "hi"
    return "en"


class TokenEstimator:
    def __init__(self, config: AppConfigProtocol) -> None:
        self._config = config

    def _chars_per_token(self, language: SessionLanguage) -> float:
        if language == SessionLanguage.Hindi:
            return 1.6
        return 3.6

    def estimate_tokens(self, text: str, language: SessionLanguage) -> int:
        chars = len(text)
        if chars <= 0:
            return 0
        return max(1, math.ceil(chars / self._chars_per_token(language)))

    def tokens_per_word(self, language: SessionLanguage) -> float:
        llm = self._config.json().get("llm", {})
        per_word: Mapping[str, object] = llm.get("tokens_per_word", {})  # type: ignore[assignment]
        key = _lang_key(language)
        if key in per_word:
            return float(per_word[key])  # type: ignore[arg-type]
        return float(per_word.get("default", 3.0))  # type: ignore[arg-type]

    def num_predict_for_word_limit(self, word_limit: int, language: SessionLanguage) -> int:
        overflow = self._config.discussion_word_limit_overflow()
        word_cap = word_limit * (1.0 + overflow)
        raw = word_cap * self.tokens_per_word(language) * 1.25 + 24.0
        return min(600, max(32, math.ceil(raw)))

    def calibrate(
        self,
        language: SessionLanguage,
        prompt_eval_count: int,
        estimated_new_tokens: int,
    ) -> None:
        if prompt_eval_count <= 0 or estimated_new_tokens <= 0:
            return
        # EMA calibration persisted to config in M10 benchmark; hook reserved for M1.
