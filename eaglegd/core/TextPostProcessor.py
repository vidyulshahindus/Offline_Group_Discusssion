from __future__ import annotations

import re
from dataclasses import dataclass

from eaglegd.core.HindiFeminineFixer import HindiFeminineFixer
from eaglegd.llm.PromptTypes import EagleMode, SessionLanguage

_THINK_RE = re.compile(
    r"<\s*redacted_thinking\s*>.*?</\s*redacted_thinking\s*>",
    re.DOTALL | re.IGNORECASE,
)
_TOOL_RE = re.compile(
    r"<\s*tool_code\s*>.*?</\s*tool_code\s*>",
    re.DOTALL | re.IGNORECASE,
)
_URL_RE = re.compile(r"https?://\S+")
_MD_LINK = re.compile(r"\[([^\]]+)\]\([^)]+\)")
_BACKTICKS = re.compile(r"`+")
_HEADING = re.compile(r"^#+\s*", re.MULTILINE)
_ROLE_RE = re.compile(
    r"^(?:Eagle|Student\s*[A-Z0-9]+|Teacher|Participant\s*\d+)\s*[:：\-–]\s*",
    re.IGNORECASE,
)
_LIST_MARKER = re.compile(r"^(?:[-*•]|\d+[.)])\s+", re.MULTILINE)

_BANNED = (
    "as an ai language model",
    "as of my last update",
    "my knowledge cutoff",
    "i'm just a robot",
    "what else would you like to discuss",
    "how can i help you today",
)


def _strip_tagged_blocks(text: str) -> str:
    text = _THINK_RE.sub("", text)
    text = _TOOL_RE.sub("", text)
    return text


def _strip_urls_and_markdown(text: str) -> str:
    text = _URL_RE.sub("", text)
    text = _MD_LINK.sub(r"\1", text)
    text = text.replace("**", "").replace("__", "")
    text = _BACKTICKS.sub("", text)
    text = _HEADING.sub("", text)
    return text


def _map_symbols(text: str, language: SessionLanguage) -> str:
    hindi = language == SessionLanguage.Hindi
    text = text.replace("&", "और" if hindi else "and")
    text = text.replace("%", "प्रतिशत" if hindi else "percent")
    return text


def _remove_role_label(text: str) -> str:
    return _ROLE_RE.sub("", text)


def _is_banned_phrase(sentence: str) -> bool:
    lower = sentence.lower()
    return any(phrase in lower for phrase in _BANNED)


def _normalize_whitespace(text: str) -> str:
    return " ".join(text.split()).strip()


class TextPostProcessor:
    @dataclass
    class Result:
        text: str = ""
        dropped: bool = False

    @staticmethod
    def stripForDisplay(raw: str) -> str:
        text = _strip_tagged_blocks(raw)
        kept: list[str] = []
        for line in text.split("\n"):
            trimmed = line.strip()
            if trimmed.lower().startswith("web_search(") or trimmed.lower().startswith(
                "print(web_search"
            ):
                continue
            kept.append(line)
        return _normalize_whitespace(_strip_urls_and_markdown("\n".join(kept)))

    @staticmethod
    def stripForSpeech(raw: str) -> str:
        return TextPostProcessor.stripForDisplay(raw)

    @staticmethod
    def processSentence(
        sentence: str, mode: EagleMode, language: SessionLanguage
    ) -> TextPostProcessor.Result:
        del mode  # reserved for mode-specific rules
        text = TextPostProcessor.stripForDisplay(sentence)
        text = _remove_role_label(text)
        text = _map_symbols(text, language)
        text = _LIST_MARKER.sub("", text)
        text = _normalize_whitespace(text)
        if not text:
            return TextPostProcessor.Result(dropped=True)
        if _is_banned_phrase(text):
            return TextPostProcessor.Result(dropped=True)
        if language == SessionLanguage.Hindi:
            text = HindiFeminineFixer.apply(text)
        if text and not text.endswith((".", "?", "\u0964")):
            text += "\u0964" if language == SessionLanguage.Hindi else "."
        return TextPostProcessor.Result(text=text)
