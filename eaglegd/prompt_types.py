"""Canonical prompt/session types — re-export from llm with SCREAMING aliases."""

from __future__ import annotations

from eaglegd.llm.prompt_types import EagleMode, SessionLanguage, TurnInputs, UnitContext

# Aliases for modules that used SCREAMING_SNAKE enum members.
EagleMode.DOUBT = EagleMode.Doubt  # type: ignore[attr-defined]
EagleMode.PRACTICE = EagleMode.Practice  # type: ignore[attr-defined]
SessionLanguage.ENGLISH = SessionLanguage.English  # type: ignore[attr-defined]
SessionLanguage.HINDI = SessionLanguage.Hindi  # type: ignore[attr-defined]

__all__ = ["EagleMode", "SessionLanguage", "TurnInputs", "UnitContext"]
