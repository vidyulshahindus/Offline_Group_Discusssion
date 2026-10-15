from __future__ import annotations

import json
import re
from enum import Enum, auto
from typing import TYPE_CHECKING

from eaglegd._repo import resource_path
from eaglegd.llm.PromptTypes import SessionLanguage

if TYPE_CHECKING:
    from eaglegd.app.AppConfig import AppConfig
    from eaglegd.app.Paths import Paths


class DetectedScript(Enum):
    Latin = auto()
    Devanagari = auto()
    Telugu = auto()
    Malayalam = auto()
    Tamil = auto()
    Kannada = auto()
    Bengali = auto()
    Gujarati = auto()
    Gurmukhi = auto()
    Arabic = auto()
    Other = auto()


_SCRIPT_RULES: list[tuple[DetectedScript, re.Pattern[str]]] = [
    (DetectedScript.Devanagari, re.compile(r"[\u0900-\u097F]")),
    (DetectedScript.Telugu, re.compile(r"[\u0C00-\u0C7F]")),
    (DetectedScript.Malayalam, re.compile(r"[\u0D00-\u0D7F]")),
    (DetectedScript.Tamil, re.compile(r"[\u0B80-\u0BFF]")),
    (DetectedScript.Kannada, re.compile(r"[\u0C80-\u0CFF]")),
    (DetectedScript.Bengali, re.compile(r"[\u0980-\u09FF]")),
    (DetectedScript.Gujarati, re.compile(r"[\u0A80-\u0AFF]")),
    (DetectedScript.Gurmukhi, re.compile(r"[\u0A00-\u0A7F]")),
    (DetectedScript.Arabic, re.compile(r"[\u0600-\u06FF]")),
    (DetectedScript.Latin, re.compile(r"[A-Za-z]")),
]


def _count_letters(text: str, pattern: re.Pattern[str]) -> int:
    return sum(len(m.group(0)) for m in pattern.finditer(text))


class LanguageService:
    def __init__(self) -> None:
        self._devanagari_lang = "hi"
        self._urdu_script_as_hindi = True
        self._hinglish_reply = "hindi"
        self._hinglish_ratio = 0.25
        self._hinglish_min_hits = 3
        self._hinglish_tokens: set[str] = set()
        self._hallucinations: list[str] = []
        self._prompt_language: dict[str, str] = {}
        self._voices: dict[str, str] = {}

    def load(self, config: AppConfig, paths: Paths) -> bool:
        self._urdu_script_as_hindi = config.sttUrduScriptAsHindi()
        self._devanagari_lang = config.sttDevanagariLang()

        lang_path = resource_path("data", "lang", "languages.json")
        if not lang_path.is_file():
            lang_path = paths.dataDir() / "lang/languages.json"
        if lang_path.is_file():
            root = json.loads(lang_path.read_text(encoding="utf-8"))
            for entry in root.get("languages", []):
                code = entry.get("code", "")
                self._prompt_language[code] = entry.get("prompt_language", "")
                self._voices[code] = entry.get("voice", "")

        hi_path = resource_path("data", "lang", "hinglish.json")
        if not hi_path.is_file():
            hi_path = paths.dataDir() / "lang/hinglish.json"
        if not hi_path.is_file():
            self._hinglish_tokens = {"hai", "kya", "nahi", "mujhe", "aap", "hum"}
            self._hallucinations = ["Thank you.", "Thanks for watching!", "[Music]"]
            return True

        root = json.loads(hi_path.read_text(encoding="utf-8"))
        self._hinglish_ratio = float(root.get("threshold_ratio", 0.25))
        self._hinglish_min_hits = int(root.get("min_hits", 3))
        self._hinglish_reply = root.get("hinglish_reply", "hindi")
        self._hinglish_tokens = {
            str(t).lower() for t in root.get("hinglish_tokens", [])
        }
        self._hallucinations = [str(h) for h in root.get("whisper_hallucinations", [])]
        return bool(self._hinglish_tokens)

    def detectScript(self, text: str) -> DetectedScript:
        total = 0
        counts: dict[DetectedScript, int] = {}
        for script, pattern in _SCRIPT_RULES:
            n = _count_letters(text, pattern)
            counts[script] = n
            total += n
        if total <= 0:
            return DetectedScript.Latin
        best = DetectedScript.Other
        best_count = 0
        for script, count in counts.items():
            if count > best_count:
                best_count = count
                best = script
        if best_count * 2 <= total:
            return DetectedScript.Other
        return best

    def scriptName(self, script: DetectedScript) -> str:
        return script.name if script != DetectedScript.Other else "Other"

    def isHinglish(self, text: str) -> bool:
        tokens = [t for t in re.split(r"\W+", text.lower()) if t]
        if len(tokens) < self._hinglish_min_hits:
            return False
        hits = sum(1 for tok in tokens if tok in self._hinglish_tokens)
        return hits >= self._hinglish_min_hits and hits / len(tokens) >= self._hinglish_ratio

    def filterWhisperHallucination(self, text: str) -> str:
        trimmed = text.strip()
        for h in self._hallucinations:
            if trimmed.lower() == h.lower():
                return ""
        if trimmed.lower() == "you":
            return ""
        return trimmed

    def replyLanguageFor(
        self,
        transcript: str,
        locked_fallback: SessionLanguage,
        note_out: list[str] | None = None,
    ) -> SessionLanguage:
        script = self.detectScript(transcript)
        if script == DetectedScript.Devanagari:
            return SessionLanguage.Hindi
        if script == DetectedScript.Latin:
            if self.isHinglish(transcript) and self._hinglish_reply == "hindi":
                return SessionLanguage.Hindi
            return SessionLanguage.English
        if script == DetectedScript.Arabic and self._urdu_script_as_hindi:
            return SessionLanguage.Hindi
        code = ""
        if script == DetectedScript.Telugu:
            code = "te"
        elif script == DetectedScript.Malayalam:
            code = "ml"
        if code and not self.voiceIdFor(code) and note_out is not None:
            note_out.append(
                f"No voice for {self.scriptName(script)} installed; replying in English"
            )
        if code and self.voiceIdFor(code):
            return locked_fallback
        return locked_fallback

    def promptLanguageString(self, code: str) -> str:
        return self._prompt_language.get(code, "English")

    def voiceIdFor(self, code: str) -> str:
        return self._voices.get(code, "")

    def whisperHallucinations(self) -> list[str]:
        return list(self._hallucinations)

    # --- snake_case aliases for STT port ---
    def detect_script(self, text: str) -> DetectedScript:
        return self.detectScript(text)

    def script_name(self, script: DetectedScript) -> str:
        return self.scriptName(script)

    def is_hinglish(self, text: str) -> bool:
        return self.isHinglish(text)

    def filter_whisper_hallucination(self, text: str) -> str:
        return self.filterWhisperHallucination(text)

    def reply_language_for(
        self,
        transcript: str,
        locked_fallback: SessionLanguage,
        note_out: list[str] | None = None,
    ) -> SessionLanguage:
        return self.replyLanguageFor(transcript, locked_fallback, note_out)

    def prompt_language_string(self, code: str) -> str:
        return self.promptLanguageString(code)

    def voice_id_for(self, code: str) -> str:
        return self.voiceIdFor(code)

    def whisper_hallucinations(self) -> list[str]:
        return self.whisperHallucinations()
