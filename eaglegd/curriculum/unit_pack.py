from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass
class UnitPackValidation:
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)

    def is_valid(self) -> bool:
        return not self.errors


def _count_words(text: str) -> int:
    return len([p for p in re.split(r"\s+", text.strip()) if p])


def _concept_word_count_ok(concept: str) -> bool:
    n = _count_words(concept.strip())
    return 2 <= n <= 4


def _require_string(obj: dict[str, Any], key: str, errors: list[str]) -> str:
    if key not in obj or not isinstance(obj[key], str):
        errors.append(f"Missing or invalid string field '{key}'.")
        return ""
    value = obj[key].strip()
    if not value:
        errors.append(f"Required field '{key}' is empty.")
    return value


class UnitPack:
    def __init__(self, doc: dict[str, Any] | None = None) -> None:
        self._doc: dict[str, Any] = doc or {}
        self._source_path = ""
        self._builtin = False

    @classmethod
    def from_json(cls, raw: bytes | str, error_out: list[str] | None = None) -> UnitPack:
        try:
            if isinstance(raw, bytes):
                data = json.loads(raw.decode("utf-8"))
            else:
                data = json.loads(raw)
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            if error_out is not None:
                error_out.append(f"JSON parse error: {exc}")
            return cls()
        if not isinstance(data, dict):
            if error_out is not None:
                error_out.append("JSON root must be an object.")
            return cls()
        return cls(data)

    @classmethod
    def from_file(cls, path: str | Path, error_out: list[str] | None = None) -> UnitPack:
        p = Path(path)
        if not p.is_file():
            if error_out is not None:
                error_out.append(f"Could not read {p}")
            return cls()
        try:
            pack = cls.from_json(p.read_bytes(), error_out)
        except OSError as exc:
            if error_out is not None:
                error_out.append(str(exc))
            return cls()
        pack.set_source_path(str(p))
        return pack

    def save_to_file(self, path: str | Path, error_out: list[str] | None = None) -> bool:
        p = Path(path)
        try:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(self.to_json_bytes(compact=False))
            return True
        except OSError as exc:
            if error_out is not None:
                error_out.append(f"Could not write {p}: {exc}")
            return False

    def to_json_bytes(self, compact: bool = False) -> bytes:
        text = json.dumps(
            self._doc,
            ensure_ascii=False,
            indent=None if compact else 2,
            separators=(",", ":") if compact else (",", ": "),
        )
        if not compact:
            text += "\n"
        return text.encode("utf-8")

    @property
    def document(self) -> dict[str, Any]:
        return self._doc

    @property
    def source_path(self) -> str:
        return self._source_path

    def set_source_path(self, path: str) -> None:
        self._source_path = path

    @property
    def is_builtin(self) -> bool:
        return self._builtin

    def set_builtin(self, builtin: bool) -> None:
        self._builtin = builtin

    def schema(self) -> str:
        return str(self._doc.get("schema", ""))

    def id(self) -> str:
        return str(self._doc.get("id", ""))

    def title(self) -> str:
        return str(self._doc.get("title", ""))

    def programme(self) -> str:
        return str(self._doc.get("programme", ""))

    def grade(self) -> int:
        try:
            return int(self._doc.get("grade", 0))
        except (TypeError, ValueError):
            return 0

    def subject(self) -> str:
        return str(self._doc.get("subject", ""))

    def approval_status(self) -> str:
        authoring = self._doc.get("authoring")
        if isinstance(authoring, dict):
            return str(authoring.get("status", ""))
        return ""

    def topic_count(self) -> int:
        clm = self._doc.get("clm")
        if not isinstance(clm, dict):
            return 0
        topics = clm.get("topics")
        return len(topics) if isinstance(topics, list) else 0

    def topic_id(self, index: int) -> str:
        topics = self._topics_array()
        if index < 0 or index >= len(topics):
            return ""
        topic = topics[index]
        if isinstance(topic, dict):
            return str(topic.get("id", ""))
        return ""

    def topic_title(self, index: int) -> str:
        topics = self._topics_array()
        if index < 0 or index >= len(topics):
            return ""
        topic = topics[index]
        if isinstance(topic, dict):
            return str(topic.get("title", ""))
        return ""

    def _topics_array(self) -> list[Any]:
        clm = self._doc.get("clm")
        if not isinstance(clm, dict):
            return []
        topics = clm.get("topics")
        return topics if isinstance(topics, list) else []

    def validate(self) -> UnitPackValidation:
        result = UnitPackValidation()
        if not self._doc:
            result.errors.append("Unit pack document is empty.")
            return result

        schema = _require_string(self._doc, "schema", result.errors)
        if schema and schema != "eagle.unitpack.v1":
            result.errors.append(
                f"Unsupported schema '{schema}' (expected eagle.unitpack.v1)."
            )
        _require_string(self._doc, "id", result.errors)
        _require_string(self._doc, "title", result.errors)
        _require_string(self._doc, "programme", result.errors)
        _require_string(self._doc, "subject", result.errors)
        _require_string(self._doc, "subject_group", result.errors)

        grade_val = self._doc.get("grade")
        if not isinstance(grade_val, (int, float)):
            result.errors.append("Missing or invalid integer field 'grade'.")
        else:
            grade = int(grade_val)
            if grade < 6 or grade > 12:
                result.errors.append("Field 'grade' must be between 6 and 12.")

        clm = self._doc.get("clm")
        if not isinstance(clm, dict) or not clm:
            result.errors.append("Missing object 'clm'.")
            return result

        concept = _require_string(clm, "concept", result.errors)
        if concept and not _concept_word_count_ok(concept):
            result.errors.append("clm.concept must be 2–4 words.")
        _require_string(clm, "concept_meaning", result.errors)
        _require_string(clm, "relevance", result.errors)
        _require_string(clm, "application", result.errors)
        _require_string(clm, "wisdom_question", result.errors)

        transfers = clm.get("transfers")
        if not isinstance(transfers, list) or len(transfers) < 3:
            result.errors.append("clm.transfers must contain at least 3 items.")

        topics = clm.get("topics")
        if not isinstance(topics, list) or not topics:
            result.errors.append("clm.topics must contain at least one topic.")

        reflection = clm.get("reflection")
        goals: list[Any] = []
        if isinstance(reflection, dict):
            lg = reflection.get("learning_goals")
            if isinstance(lg, list):
                goals = lg
        if not goals:
            result.errors.append("clm.reflection.learning_goals is required.")

        authoring = self._doc.get("authoring")
        status = ""
        if isinstance(authoring, dict):
            status = _require_string(authoring, "status", result.errors)
        if status and status not in ("draft", "reviewed", "approved"):
            result.errors.append("authoring.status must be draft, reviewed, or approved.")

        if isinstance(topics, list):
            for topic in topics:
                if not isinstance(topic, dict):
                    continue
                _require_string(topic, "id", result.errors)
                _require_string(topic, "title", result.errors)
                _require_string(topic, "summary", result.errors)
                _require_string(topic, "key_facts", result.errors)
                key_facts = str(topic.get("key_facts", ""))
                if key_facts and _count_words(key_facts) > 120:
                    result.errors.append(
                        f"Topic '{topic.get('id', '')}' key_facts exceeds 120 words."
                    )
                terms = topic.get("key_terms")
                if not isinstance(terms, list) or not terms:
                    result.errors.append(
                        f"Topic '{topic.get('id', '')}' must include key_terms."
                    )
                ctqs = topic.get("ctqs")
                if status == "approved":
                    if len(topics) != 3:
                        result.errors.append("Approved packs must have exactly 3 topics.")
                    if not isinstance(ctqs, list) or len(ctqs) != 3:
                        result.errors.append(
                            f"Approved topic '{topic.get('id', '')}' must have exactly 3 CTQs."
                        )
        return result
