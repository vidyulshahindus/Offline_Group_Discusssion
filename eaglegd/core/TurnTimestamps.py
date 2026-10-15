from __future__ import annotations

from dataclasses import dataclass


@dataclass
class TurnTimestamps:
    t0Ms: int = 0
    sttDoneMs: int = -1
    promptBuiltMs: int = -1
    requestSentMs: int = -1
    firstTokenMs: int = -1
    firstSentenceMs: int = -1
    firstTtsDoneMs: int = -1
    firstAudioMs: int = -1
    llmDoneMs: int = -1
    speechDoneMs: int = -1
