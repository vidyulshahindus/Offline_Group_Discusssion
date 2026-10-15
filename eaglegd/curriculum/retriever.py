from __future__ import annotations

import math
import re
import unicodedata
from dataclasses import dataclass

from eaglegd.curriculum.unit_pack import UnitPack

_ENGLISH_STOPWORDS = frozenset(
    {
        "a",
        "an",
        "and",
        "are",
        "as",
        "at",
        "be",
        "by",
        "for",
        "from",
        "in",
        "is",
        "it",
        "of",
        "on",
        "or",
        "that",
        "the",
        "to",
        "was",
        "with",
    }
)

_TOKEN_RE = re.compile(r"[\w]+", re.UNICODE)


@dataclass
class RetrievalHit:
    text: str
    score: float = 0.0


@dataclass
class _Doc:
    text: str
    tokens: list[str]
    length: int = 0


class Retriever:
    def __init__(self) -> None:
        self._docs: list[_Doc] = []
        self._avg_doc_len = 0.0
        self._df: dict[str, int] = {}

    def build_from_unit(self, pack: UnitPack, topic_index: int) -> None:
        self._docs.clear()
        self._df.clear()
        self._avg_doc_len = 0.0
        if not pack.id() or topic_index < 0:
            return
        clm = pack.document.get("clm")
        if not isinstance(clm, dict):
            return
        topics = clm.get("topics")
        if not isinstance(topics, list) or topic_index >= len(topics):
            return
        topic = topics[topic_index]
        if not isinstance(topic, dict):
            return

        def add_doc(text: str) -> None:
            trimmed = text.strip()
            if not trimmed:
                return
            tokens = self.tokenize(trimmed)
            if not tokens:
                return
            self._docs.append(_Doc(text=trimmed, tokens=tokens, length=len(tokens)))

        add_doc(str(topic.get("summary", "")))
        knowledge = topic.get("knowledge")
        if isinstance(knowledge, list):
            for v in knowledge:
                add_doc(str(v))
        key_terms = topic.get("key_terms")
        if isinstance(key_terms, list):
            for v in key_terms:
                add_doc(str(v))
        misconceptions = topic.get("misconceptions")
        if isinstance(misconceptions, list):
            for v in misconceptions:
                if isinstance(v, dict):
                    add_doc(str(v.get("text", "")))

        total_len = 0
        for doc in self._docs:
            total_len += doc.length
            seen: set[str] = set()
            for tok in doc.tokens:
                if tok in seen:
                    continue
                seen.add(tok)
                self._df[tok] = self._df.get(tok, 0) + 1
        if self._docs:
            self._avg_doc_len = total_len / len(self._docs)

    @staticmethod
    def tokenize(text: str) -> list[str]:
        out: list[str] = []
        for match in _TOKEN_RE.finditer(text):
            tok = match.group(0).casefold()
            if len(tok) < 2 or tok in _ENGLISH_STOPWORDS:
                continue
            out.append(tok)
        return out

    @staticmethod
    def _dominant_script(text: str) -> str:
        counts: dict[str, int] = {}
        for ch in text:
            if not ch.isalpha():
                continue
            name = unicodedata.name(ch, "")
            script = name.split()[0] if name else "UNKNOWN"
            counts[script] = counts.get(script, 0) + 1
        if not counts:
            return "LATIN"
        return max(counts, key=counts.get)

    @classmethod
    def scripts_mismatch(cls, a: str, b: str) -> bool:
        if not a or not b:
            return False
        return cls._dominant_script(a) != cls._dominant_script(b)

    def query(self, text: str, top_k: int = 2) -> list[RetrievalHit]:
        hits: list[RetrievalHit] = []
        if not self._docs:
            return hits
        q_tokens = self.tokenize(text)
        if not q_tokens:
            return hits
        n = len(self._docs)
        k1 = 1.2
        b = 0.75
        for doc in self._docs:
            if self.scripts_mismatch(text, doc.text):
                continue
            tf: dict[str, int] = {}
            for tok in doc.tokens:
                tf[tok] = tf.get(tok, 0) + 1
            score = 0.0
            for term in q_tokens:
                f = tf.get(term, 0)
                if f == 0:
                    continue
                df = self._df.get(term, 0)
                idf = math.log((n - df + 0.5) / (df + 0.5) + 1.0)
                denom = f + k1 * (1.0 - b + b * doc.length / max(1.0, self._avg_doc_len))
                score += idf * (f * (k1 + 1.0)) / denom
            if score > 0.0:
                hits.append(RetrievalHit(text=doc.text, score=score))
        hits.sort(key=lambda h: h.score, reverse=True)
        return hits[:top_k]

    def notes_for_turn(
        self, query_text: str, max_words: int = 160, min_score: float = 1.0
    ) -> str:
        hits = self.query(query_text, 2)
        parts: list[str] = []
        words = 0
        for hit in hits:
            if hit.score < min_score:
                continue
            tokens = [p for p in re.split(r"\s+", hit.text) if p]
            clipped: list[str] = []
            for token in tokens:
                if words >= max_words:
                    break
                clipped.append(token)
                words += 1
            if clipped:
                parts.append(" ".join(clipped))
            if words >= max_words:
                break
        return " ".join(parts)

    def is_empty(self) -> bool:
        return not self._docs

    def document_count(self) -> int:
        return len(self._docs)
