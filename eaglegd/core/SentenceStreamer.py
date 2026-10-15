from __future__ import annotations

import re

from PySide6.QtCore import QObject, Signal

from eaglegd.core.TextPostProcessor import TextPostProcessor
from eaglegd.llm.PromptTypes import EagleMode, SessionLanguage

_TERMINAL_PUNCT = (
    ".",
    "!",
    "?",
    "।",
    "॥",
    "۔",
    "。",
    "！",
    "？",
)

_EAGLE_PREFIX = re.compile(r"^\s*Eagle\s*[:：]\s*", re.IGNORECASE)
_CLAUSE_EARLY = re.compile(r"^(.{12,}?)([,;:—–])\s+", re.DOTALL)
_WORD_SPLIT = re.compile(r"\s+")
_ABBREVS = {
    "dr",
    "mr",
    "mrs",
    "ms",
    "st",
    "vs",
    "etc",
    "e.g",
    "i.e",
    "no",
    "fig",
    "approx",
}
_CLAUSE_BREAK = re.compile(r"[,;:\u2013\u2014\u2012\u2014]")


def _ends_with_terminal(text: str) -> bool:
    trimmed = text.strip()
    if not trimmed:
        return False
    return any(trimmed.endswith(p) for p in _TERMINAL_PUNCT)


class SentenceStreamer(QObject):
    sentenceReady = Signal(int, int, str)
    wordBudgetReached = Signal(int)
    watchdogFlush = Signal(int, str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._turn_id = -1
        self._mode = EagleMode.Doubt
        self._language = SessionLanguage.English
        self._word_limit = 75
        self._hard_word_cap = 112
        self._overflow_ratio = 0.5
        self._buffer = ""
        self._hold_tag = ""
        self._sentence_index = 0
        self._emitted_words = 0
        self._any_sentence_emitted = False
        self._budget_stopped = False
        self._spoken_sentences: list[str] = []
        self._first_token_clock_ms = -1
        self._clock_ms = 0
        self._finished = False

    def reset(
        self,
        turn_id: int,
        mode: EagleMode,
        language: SessionLanguage,
        word_limit: int,
        overflow_ratio: float = 0.5,
    ) -> None:
        self._turn_id = turn_id
        self._mode = mode
        self._language = language
        self._word_limit = word_limit
        self._overflow_ratio = overflow_ratio
        self._hard_word_cap = int(word_limit * (1.0 + overflow_ratio))
        self._buffer = ""
        self._hold_tag = ""
        self._sentence_index = 0
        self._emitted_words = 0
        self._any_sentence_emitted = False
        self._budget_stopped = False
        self._spoken_sentences.clear()
        self._first_token_clock_ms = -1
        self._clock_ms = 0
        self._finished = False

    def buffer(self) -> str:
        return self._buffer

    def emittedWordCount(self) -> int:
        return self._emitted_words

    def spokenText(self) -> str:
        return " ".join(self._spoken_sentences)

    def setClockMs(self, ms_since_t0: int) -> None:
        self._clock_ms = ms_since_t0

    def pushToken(self, chunk: str) -> None:
        if self._finished or self._budget_stopped or not chunk:
            return
        if self._first_token_clock_ms < 0:
            self._first_token_clock_ms = self._clock_ms
        self._buffer += self._clean_incremental(chunk)
        self._try_early_flush()
        self._try_emit_sentences()
        self._try_watchdog_flush()

    def finishStream(self, hit_token_limit: bool = False) -> None:
        if self._finished:
            return
        self._finished = True
        self._try_emit_sentences()
        tail = self._buffer.strip()
        self._buffer = ""
        if not tail or self._budget_stopped:
            return
        if hit_token_limit and not _ends_with_terminal(tail):
            return
        words = self._count_words(tail)
        if words >= 2 and self._emitted_words < self._word_limit:
            self._emit_sentence(tail)
            if self._emitted_words >= self._word_limit:
                self._stop_after_budget()

    def _clean_incremental(self, chunk: str) -> str:
        text = chunk
        if not self._buffer and not self._hold_tag:
            text = _EAGLE_PREFIX.sub("", text)
        combined = self._hold_tag + text
        self._hold_tag = ""

        def strip_span(open_tag: str, close_tag: str) -> None:
            nonlocal combined
            start = 0
            while True:
                start = combined.lower().find(open_tag.lower(), start)
                if start == -1:
                    return
                end = combined.lower().find(close_tag.lower(), start)
                if end == -1:
                    self._hold_tag = combined[start:]
                    combined = combined[:start]
                    return
                combined = combined[:start] + combined[end + len(close_tag) :]
                start = 0

        strip_span("<think>", "</think>")
        strip_span("<tool_code>", "</tool_code>")
        return combined

    def _try_early_flush(self) -> None:
        if self._any_sentence_emitted or self._word_limit <= 0 or self._budget_stopped:
            return
        if self._first_token_clock_ms < 0 or self._clock_ms - self._first_token_clock_ms < 1200:
            return
        if self._count_words(self._buffer) < 10:
            return
        match = _CLAUSE_EARLY.match(self._buffer)
        if not match:
            return
        chunk_body = match.group(1)
        if len(_WORD_SPLIT.split(chunk_body.strip())) < 6:
            return
        chunk = (match.group(1) + match.group(2)).strip()
        self._buffer = self._buffer[match.end() :]
        processed = TextPostProcessor.processSentence(chunk, self._mode, self._language)
        if not processed.dropped and processed.text:
            self._any_sentence_emitted = True
            self._emitted_words += self._count_words(processed.text)
            self._spoken_sentences.append(processed.text)
            self.sentenceReady.emit(self._turn_id, self._sentence_index, processed.text)
            self._sentence_index += 1
            if self._emitted_words >= self._word_limit:
                self._stop_after_budget()

    def _try_watchdog_flush(self) -> None:
        if self._any_sentence_emitted or self._clock_ms < 8500 or self._budget_stopped:
            return
        if self._count_words(self._buffer) < 6:
            return
        chunk = self._buffer.strip()
        self._buffer = ""
        self._any_sentence_emitted = True
        self.watchdogFlush.emit(self._turn_id, chunk)
        processed = TextPostProcessor.processSentence(chunk, self._mode, self._language)
        if not processed.dropped and processed.text:
            self._emitted_words += self._count_words(processed.text)
            self._spoken_sentences.append(processed.text)
            self.sentenceReady.emit(self._turn_id, self._sentence_index, processed.text)
            self._sentence_index += 1
            if self._emitted_words >= self._word_limit:
                self._stop_after_budget()

    def _is_abbreviation_dot(self, buffer: str, dot_index: int) -> bool:
        word_start = buffer.rfind(" ", 0, dot_index)
        word = buffer[word_start + 1 : dot_index].strip()
        if word.lower() in _ABBREVS:
            return True
        if (
            dot_index >= 2
            and buffer[dot_index - 1].isalpha()
            and buffer[dot_index - 2] == " "
            and dot_index + 1 < len(buffer)
            and buffer[dot_index + 1].isspace()
        ):
            return True
        return False

    def _emit_sentence(self, sentence: str) -> None:
        processed = TextPostProcessor.processSentence(sentence, self._mode, self._language)
        if processed.dropped or not processed.text:
            return
        self._any_sentence_emitted = True
        self._emitted_words += self._count_words(processed.text)
        self._spoken_sentences.append(processed.text)
        self.sentenceReady.emit(self._turn_id, self._sentence_index, processed.text)
        self._sentence_index += 1

    def _stop_after_budget(self) -> None:
        if self._budget_stopped:
            return
        self._budget_stopped = True
        self._buffer = ""
        self._finished = True
        self.wordBudgetReached.emit(self._turn_id)

    def _trim_run_on_to_word_cap(self, sentence: str, max_words: int) -> str:
        if max_words <= 0:
            return ""
        words = _WORD_SPLIT.split(sentence.strip())
        words = [w for w in words if w]
        if len(words) <= max_words:
            return sentence.strip()
        subset = " ".join(words[:max_words])
        best_break = -1
        for match in _CLAUSE_BREAK.finditer(subset):
            best_break = match.start()
        trimmed = subset[: best_break + 1].strip() if best_break >= 0 else subset.strip()
        if not trimmed:
            return ""
        if not _ends_with_terminal(trimmed):
            trimmed += "।" if self._language == SessionLanguage.Hindi else "."
        return trimmed

    def _try_emit_sentences(self) -> None:
        if self._budget_stopped:
            self._buffer = ""
            return
        scan = 0
        while scan < len(self._buffer):
            if self._emitted_words >= self._word_limit:
                self._buffer = ""
                return
            boundary = -1
            punct = ""
            for p in _TERMINAL_PUNCT:
                idx = self._buffer.find(p, scan)
                if idx >= 0 and (boundary < 0 or idx < boundary):
                    boundary = idx
                    punct = p
            if boundary < 0:
                break
            if punct == "." and self._is_abbreviation_dot(self._buffer, boundary):
                scan = boundary + 1
                continue
            after = boundary + len(punct)
            if after < len(self._buffer) and not self._buffer[after].isspace() and self._buffer[
                after
            ] != "\n":
                scan = boundary + 1
                continue
            sentence = self._buffer[:after].strip()
            self._buffer = self._buffer[after:].strip()
            scan = 0
            if not sentence:
                continue
            if self._count_words(sentence) < 3 and self._buffer:
                self._buffer = sentence + " " + self._buffer
                break
            room_to_cap = self._hard_word_cap - self._emitted_words
            if self._count_words(sentence) > room_to_cap:
                sentence = self._trim_run_on_to_word_cap(sentence, room_to_cap)
                if not sentence:
                    self._stop_after_budget()
                    return
            self._emit_sentence(sentence)
            if self._emitted_words >= self._word_limit:
                self._stop_after_budget()
                return

    def _count_words(self, text: str) -> int:
        return len([w for w in _WORD_SPLIT.split(text.strip()) if w])
