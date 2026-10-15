"""Conversation history trimming (ported from HistoryManager)."""

from __future__ import annotations

import logging
from typing import List

from eaglegd.llm.config import AppConfigProtocol
from eaglegd.llm.il_llm_backend import LlmMessage
from eaglegd.llm.prompt_types import SessionLanguage
from eaglegd.llm.token_estimator import TokenEstimator

_log = logging.getLogger("eagle.history")


class HistoryManager:
    def __init__(self, config: AppConfigProtocol, estimator: TokenEstimator) -> None:
        self._config = config
        self._estimator = estimator
        self._messages: List[LlmMessage] = []

    def reset(self) -> None:
        self._messages.clear()

    def is_empty(self) -> bool:
        return len(self._messages) == 0

    @property
    def messages(self) -> List[LlmMessage]:
        return self._messages

    def append_turn(
        self,
        user_turn_message: str,
        spoken_assistant_text: str,
        language: SessionLanguage,
    ) -> None:
        self._messages.append(LlmMessage(role="user", content=user_turn_message))
        self._messages.append(LlmMessage(role="assistant", content=spoken_assistant_text))
        self._trim_if_needed(language)

    def append_follow_up_to_last_assistant(self, follow_up_question: str) -> None:
        if not self._messages or self._messages[-1].role != "assistant":
            return
        content = self._messages[-1].content
        if not content.endswith((".", "?", "\u0964")):
            content += "."
        content += " "
        content += follow_up_question.strip()
        self._messages[-1].content = content

    def trimmed_history(self, language: SessionLanguage) -> List[LlmMessage]:
        return list(self._messages)

    def _trim_if_needed(self, language: SessionLanguage) -> None:
        llm = self._config.json().get("llm", {})
        budget = int(llm.get("history_budget_tokens", 2200))
        trim_to = int(llm.get("history_trim_to_tokens", 1400))

        total = sum(self._estimator.estimate_tokens(m.content, language) for m in self._messages)
        if total <= budget:
            return

        while total > trim_to and len(self._messages) > 4:
            del self._messages[0:2]
            total = sum(
                self._estimator.estimate_tokens(m.content, language) for m in self._messages
            )
            _log.info("Trimmed oldest history turn; tokens now about %s", total)
