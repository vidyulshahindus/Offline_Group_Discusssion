"""LLM backend interface and request types (ported from ILlmBackend)."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Mapping

from PySide6.QtCore import QObject, Signal


@dataclass
class LlmMessage:
    role: str
    content: str


@dataclass(init=False)
class LlmMetrics:
    prompt_eval_count: int = 0
    prompt_eval_cached_count: int = -1
    prompt_eval_ns: int = 0
    eval_count: int = 0
    eval_ns: int = 0
    load_ns: int = 0
    total_ns: int = 0
    ms_to_first_token: int = -1
    done_reason: str = ""

    def __init__(self, **kwargs: Any) -> None:
        aliases = {
            "promptEvalCount": "prompt_eval_count",
            "promptEvalCachedCount": "prompt_eval_cached_count",
            "promptEvalNs": "prompt_eval_ns",
            "evalCount": "eval_count",
            "evalNs": "eval_ns",
            "loadNs": "load_ns",
            "totalNs": "total_ns",
            "msToFirstToken": "ms_to_first_token",
            "doneReason": "done_reason",
        }
        normalized: dict[str, Any] = {}
        for key, value in kwargs.items():
            normalized[aliases.get(key, key)] = value
        self.prompt_eval_count = int(normalized.get("prompt_eval_count", 0))
        self.prompt_eval_cached_count = int(normalized.get("prompt_eval_cached_count", -1))
        self.prompt_eval_ns = int(normalized.get("prompt_eval_ns", 0))
        self.eval_count = int(normalized.get("eval_count", 0))
        self.eval_ns = int(normalized.get("eval_ns", 0))
        self.load_ns = int(normalized.get("load_ns", 0))
        self.total_ns = int(normalized.get("total_ns", 0))
        self.ms_to_first_token = int(normalized.get("ms_to_first_token", -1))
        self.done_reason = str(normalized.get("done_reason", ""))

    def prompt_eval_new_count(self) -> int:
        if self.prompt_eval_cached_count >= 0 and self.prompt_eval_count >= self.prompt_eval_cached_count:
            return self.prompt_eval_count - self.prompt_eval_cached_count
        return self.prompt_eval_count

    # camelCase aliases (C++ / older ports)
    @property
    def promptEvalCount(self) -> int:
        return self.prompt_eval_count

    @property
    def promptEvalCachedCount(self) -> int:
        return self.prompt_eval_cached_count

    @property
    def promptEvalNs(self) -> int:
        return self.prompt_eval_ns

    @property
    def evalCount(self) -> int:
        return self.eval_count

    @property
    def evalNs(self) -> int:
        return self.eval_ns

    @property
    def loadNs(self) -> int:
        return self.load_ns

    @property
    def totalNs(self) -> int:
        return self.total_ns

    @property
    def msToFirstToken(self) -> int:
        return self.ms_to_first_token

    @property
    def doneReason(self) -> str:
        return self.done_reason

    def promptEvalNewCount(self) -> int:
        return self.prompt_eval_new_count()


@dataclass(init=False)
class LlmRequest:
    model: str = ""
    messages: List[LlmMessage] = field(default_factory=list)
    num_predict: int = 200
    temperature: float = 0.6
    top_p: float = 0.9
    top_k: int = 40
    repeat_penalty: float = 1.1
    stop: List[str] = field(default_factory=list)
    json_schema: Dict[str, Any] = field(default_factory=dict)
    purpose: str = ""
    turn_id: int = -1

    def __init__(self, **kwargs: Any) -> None:
        aliases = {
            "numPredict": "num_predict",
            "topP": "top_p",
            "topK": "top_k",
            "repeatPenalty": "repeat_penalty",
            "jsonSchema": "json_schema",
            "turnId": "turn_id",
        }
        normalized: dict[str, Any] = {}
        for key, value in kwargs.items():
            normalized[aliases.get(key, key)] = value
        self.model = str(normalized.get("model", ""))
        self.messages = list(normalized.get("messages", []) or [])
        self.num_predict = int(normalized.get("num_predict", 200))
        self.temperature = float(normalized.get("temperature", 0.6))
        self.top_p = float(normalized.get("top_p", 0.9))
        self.top_k = int(normalized.get("top_k", 40))
        self.repeat_penalty = float(normalized.get("repeat_penalty", 1.1))
        self.stop = list(normalized.get("stop", []) or [])
        schema = normalized.get("json_schema", {}) or {}
        self.json_schema = dict(schema) if isinstance(schema, Mapping) else {}
        self.purpose = str(normalized.get("purpose", ""))
        self.turn_id = int(normalized.get("turn_id", -1))

    @property
    def numPredict(self) -> int:
        return self.num_predict

    @numPredict.setter
    def numPredict(self, value: int) -> None:
        self.num_predict = int(value)

    @property
    def topP(self) -> float:
        return self.top_p

    @topP.setter
    def topP(self, value: float) -> None:
        self.top_p = float(value)

    @property
    def topK(self) -> int:
        return self.top_k

    @topK.setter
    def topK(self, value: int) -> None:
        self.top_k = int(value)

    @property
    def repeatPenalty(self) -> float:
        return self.repeat_penalty

    @repeatPenalty.setter
    def repeatPenalty(self, value: float) -> None:
        self.repeat_penalty = float(value)

    @property
    def jsonSchema(self) -> Dict[str, Any]:
        return self.json_schema

    @jsonSchema.setter
    def jsonSchema(self, value: Mapping[str, Any] | None) -> None:
        self.json_schema = dict(value or {})

    @property
    def turnId(self) -> int:
        return self.turn_id

    @turnId.setter
    def turnId(self, value: int) -> None:
        self.turn_id = int(value)


class ILlmBackend(QObject):
    token = Signal(int, str)
    finished = Signal(int, str, object)
    failed = Signal(int, str, bool)
    probed = Signal(bool, str, list)
    model_shown = Signal(str, list, int)
    preloaded = Signal(str, bool)

    def start(self, req: LlmRequest) -> None:
        raise NotImplementedError

    def abort(self) -> None:
        raise NotImplementedError

    def is_busy(self) -> bool:
        raise NotImplementedError

    def probe(self) -> None:
        raise NotImplementedError

    def show_model(self, model: str) -> None:
        raise NotImplementedError

    def preload(self, model: str) -> None:
        raise NotImplementedError
