"""Re-export LanguageService with SCREAMING DetectedScript aliases for STT."""

from __future__ import annotations

from eaglegd.core.LanguageService import DetectedScript, LanguageService

# SCREAMING aliases used by the STT port
DetectedScript.LATIN = DetectedScript.Latin  # type: ignore[attr-defined]
DetectedScript.DEVANAGARI = DetectedScript.Devanagari  # type: ignore[attr-defined]
DetectedScript.TELUGU = DetectedScript.Telugu  # type: ignore[attr-defined]
DetectedScript.MALAYALAM = DetectedScript.Malayalam  # type: ignore[attr-defined]
DetectedScript.TAMIL = DetectedScript.Tamil  # type: ignore[attr-defined]
DetectedScript.KANNADA = DetectedScript.Kannada  # type: ignore[attr-defined]
DetectedScript.BENGALI = DetectedScript.Bengali  # type: ignore[attr-defined]
DetectedScript.GUJARATI = DetectedScript.Gujarati  # type: ignore[attr-defined]
DetectedScript.GURMUKHI = DetectedScript.Gurmukhi  # type: ignore[attr-defined]
DetectedScript.ARABIC = DetectedScript.Arabic  # type: ignore[attr-defined]
DetectedScript.OTHER = DetectedScript.Other  # type: ignore[attr-defined]

__all__ = ["DetectedScript", "LanguageService"]
