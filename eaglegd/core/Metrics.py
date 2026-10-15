from __future__ import annotations

import math
from dataclasses import dataclass, field

from eaglegd.core.TurnTimestamps import TurnTimestamps
from eaglegd.llm.ILlmBackend import LlmMetrics


@dataclass
class TurnMetrics:
    turnId: int = -1
    model: str = ""
    requestSentMs: int = 0
    firstTokenMs: int = -1
    llmDoneMs: int = -1
    llm: LlmMetrics = field(default_factory=LlmMetrics)
    estimatedNewPromptTokens: int = 0
    cacheLikelyHit: bool = False
    timestamps: TurnTimestamps = field(default_factory=TurnTimestamps)
    t0ToT1Ms: int = -1
    ttsFirstSynthMs: int = -1
    ttsWarmSynthMs: int = -1
    ttsWorkerColdSynthMs: int = -1
    ttsWorkerWarmSynthMs: int = -1


def _percentile(samples: list[int], p: float) -> float:
    if not samples:
        return 0.0
    ordered = sorted(samples)
    index = max(0, min(len(ordered) - 1, int(math.ceil(p * len(ordered))) - 1))
    return float(ordered[index])


class Metrics:
    def __init__(self) -> None:
        self._last = TurnMetrics()
        self._first_token_samples: list[int] = []
        self._t0_to_t1_samples: list[int] = []

    def recordTurn(self, turn: TurnMetrics) -> None:
        self._last = turn
        if turn.firstTokenMs >= 0:
            self._first_token_samples.append(turn.firstTokenMs)
            if len(self._first_token_samples) > 50:
                self._first_token_samples = self._first_token_samples[-50:]
        if turn.t0ToT1Ms >= 0:
            self._t0_to_t1_samples.append(turn.t0ToT1Ms)
            if len(self._t0_to_t1_samples) > 50:
                self._t0_to_t1_samples = self._t0_to_t1_samples[-50:]

    def lastTurn(self) -> TurnMetrics:
        return self._last

    def recentFirstTokenMs(self) -> list[int]:
        return list(self._first_token_samples)

    def rollingP50FirstToken(self) -> float:
        return _percentile(self._first_token_samples, 0.5)

    def rollingP95FirstToken(self) -> float:
        return _percentile(self._first_token_samples, 0.95)

    def rollingP50T0ToT1(self) -> float:
        return _percentile(self._t0_to_t1_samples, 0.5)

    def rollingP95T0ToT1(self) -> float:
        return _percentile(self._t0_to_t1_samples, 0.95)
