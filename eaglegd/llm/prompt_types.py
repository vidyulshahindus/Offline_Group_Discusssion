"""Prompt and session types (ported from PromptTypes.h)."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum, auto


class EagleMode(Enum):
    Doubt = auto()
    Practice = auto()


class SessionLanguage(Enum):
    English = auto()
    Hindi = auto()


@dataclass
class UnitContext:
    has_unit: bool = False
    pack_id: str = ""
    topic_id: str = ""
    programme: str = "MYP"
    subject: str = "General"
    grade: str = "9"
    register_text: str = ""
    register_short: str = "Grade 9 MYP"
    unit_title: str = ""
    topic_title: str = ""
    concept: str = "Learning and growth"
    concept_meaning: str = ""
    relevance: str = ""
    wisdom_question: str = ""
    ib_links: str = ""
    key_facts: str = ""


@dataclass
class TurnInputs:
    mode: EagleMode = EagleMode.Doubt
    language: SessionLanguage = SessionLanguage.English
    word_limit: int = 75
    question: str = ""
    notes: str = ""
    misconception: str = ""
