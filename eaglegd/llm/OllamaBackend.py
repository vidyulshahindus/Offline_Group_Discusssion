from __future__ import annotations

import json
import logging
import re
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import TYPE_CHECKING

from PySide6.QtCore import QElapsedTimer, QTimer, QUrl, Signal
from PySide6.QtNetwork import QNetworkAccessManager, QNetworkReply, QNetworkRequest

from eaglegd.llm.ILlmBackend import ILlmBackend, LlmMessage, LlmMetrics, LlmRequest

if TYPE_CHECKING:
    from eaglegd.app.AppConfig import AppConfig

_log = logging.getLogger("eagle.ollama")


class _AuxKind(Enum):
    Show = auto()
    Preload = auto()
    WarmUp = auto()


@dataclass
class _AuxJob:
    kind: _AuxKind
    model: str = ""
    systemPrompt: str = ""


@dataclass
class _LoadOptions:
    numCtx: int = 4096
    numThread: int = 0
    numBatch: int = 256


class OllamaBackend(ILlmBackend):
    warmUpFinished = Signal(bool)

    def __init__(self, config: AppConfig, parent=None) -> None:
        super().__init__(parent)
        self._config = config
        self._network = QNetworkAccessManager(self)
        self._load_options = _LoadOptions(
            numCtx=config.llmNumCtx(),
            numThread=config.llmNumThread(),
            numBatch=config.llmNumBatch(),
        )
        self._first_token_timer = QTimer(self)
        self._first_token_timer.setSingleShot(True)
        # Cold load of a 4B model + long school system prompt often exceeds 12s on CPU.
        self._first_token_timer.setInterval(60000)
        self._first_token_timer.timeout.connect(self._onFirstTokenTimeout)
        self._overall_timer = QTimer(self)
        self._overall_timer.setSingleShot(True)
        self._overall_timer.setInterval(config.llmRequestTimeoutMs())
        self._overall_timer.timeout.connect(self._onOverallTimeout)

        self._stream_reply: QNetworkReply | None = None
        self._aux_reply: QNetworkReply | None = None
        self._aux_queue: list[_AuxJob] = []
        self._aux_purpose = ""
        self._installed_models: list[str] = []
        self._models_require_think_false: set[str] = set()
        self._model_info: dict = {}

        self._active_request = LlmRequest()
        self._turn_id = -1
        self._stream_text = ""
        self._line_buffer = b""
        self._got_first_token = False
        self._first_token_ms = -1
        self._stream_purpose = ""
        self._turn_thinking_retry_used = False
        self._pending_think_false_retry = False
        self._current_request_sent_think_false = False
        self._request_timer = QElapsedTimer()
        self._stream_post_timer = QElapsedTimer()

        self._in_startup_sequence = False
        self._startup_primary_model = ""
        self._startup_fallback_model = ""
        self._startup_system_prompt = ""

    def _base_url(self) -> str:
        url = self._config.llmBaseUrl().strip()
        if url.endswith("/"):
            url = url[:-1]
        return url

    def isBusy(self) -> bool:
        return self._stream_reply is not None or self._aux_reply is not None

    def isStreamInFlight(self) -> bool:
        return self._stream_reply is not None

    def scheduleWarmUp(self, model: str, system_prompt: str) -> None:
        self._aux_queue.append(
            _AuxJob(_AuxKind.WarmUp, model=model, systemPrompt=system_prompt)
        )
        self._pump_aux_queue()

    def abort(self) -> None:
        self._safe_abort_stream()
        self._safe_abort_aux()
        self._aux_queue.clear()
        self._in_startup_sequence = False

    def abortActiveTurn(self) -> None:
        self._safe_abort_stream()

    def _safe_abort_stream(self) -> None:
        reply = self._stream_reply
        if not reply:
            return
        self._stream_reply = None
        reply.disconnect(self)
        reply.abort()
        self._first_token_timer.stop()
        self._overall_timer.stop()
        self._line_buffer = b""
        reply.deleteLater()

    def _safe_abort_aux(self) -> None:
        reply = self._aux_reply
        if not reply:
            return
        self._aux_reply = None
        self._aux_purpose = ""
        reply.disconnect(self)
        reply.abort()
        reply.deleteLater()

    def _should_send_think_false(self, model: str) -> bool:
        if model in self._models_require_think_false:
            return True
        if re.search(r"eagle-clm|qwen3", model, re.I):
            return True
        return False

    def _build_options(self, req: LlmRequest) -> dict:
        options: dict = {
            "num_ctx": self._load_options.numCtx,
            "num_batch": self._load_options.numBatch,
            "num_predict": req.numPredict,
            "temperature": req.temperature,
            "top_p": req.topP,
            "top_k": req.topK,
            "repeat_penalty": req.repeatPenalty,
        }
        if self._load_options.numThread > 0:
            options["num_thread"] = self._load_options.numThread
        if req.stop:
            options["stop"] = req.stop
        return options

    def _build_chat_body(
        self,
        model: str,
        messages: list[LlmMessage],
        req: LlmRequest,
        for_warmup: bool,
        stream: bool,
    ) -> dict:
        msg_array = [{"role": m.role, "content": m.content} for m in messages]
        body: dict = {
            "model": model,
            "messages": msg_array,
            "stream": stream,
            "keep_alive": self._config.llmKeepAlive(),
        }
        if self._should_send_think_false(model):
            body["think"] = False
        effective = LlmRequest(
            model=req.model,
            messages=req.messages,
            numPredict=2 if for_warmup else req.numPredict,
            temperature=0.1 if for_warmup else req.temperature,
            topP=req.topP,
            topK=req.topK,
            repeatPenalty=req.repeatPenalty,
            stop=req.stop,
            jsonSchema=req.jsonSchema,
            purpose=req.purpose,
            turnId=req.turnId,
        )
        body["options"] = self._build_options(effective)
        if req.jsonSchema:
            body["format"] = req.jsonSchema
        return body

    def start(self, req: LlmRequest) -> None:
        self._safe_abort_stream()
        if self._aux_reply:
            _log.warning("Turn start blocked: aux busy (%s)", self._aux_purpose)
            self.failed.emit(
                req.turnId,
                "Eagle is still preparing the model — try again.",
                True,
            )
            return
        self._active_request = req
        self._turn_id = req.turnId
        self._turn_thinking_retry_used = False
        self._pending_think_false_retry = False
        self._stream_purpose = f"turn:{req.purpose}"
        self._request_timer.start()
        self._begin_turn_stream()

    def _begin_turn_stream(self) -> None:
        self._stream_text = ""
        self._line_buffer = b""
        self._got_first_token = False
        self._first_token_ms = -1
        self._stream_post_timer.start()
        self._current_request_sent_think_false = self._should_send_think_false(
            self._active_request.model
        )
        body = self._build_chat_body(
            self._active_request.model,
            self._active_request.messages,
            self._active_request,
            False,
            True,
        )
        url = QUrl(f"{self._base_url()}/api/chat")
        request = QNetworkRequest(url)
        request.setHeader(
            QNetworkRequest.KnownHeaders.ContentTypeHeader, "application/json"
        )
        self._stream_reply = self._network.post(
            request, json.dumps(body).encode("utf-8")
        )
        self._stream_reply.readyRead.connect(self._on_stream_ready_read)
        self._stream_reply.finished.connect(self._on_stream_finished)
        self._first_token_timer.start()
        self._overall_timer.start()

    def _parse_stream_line(self, line: bytes) -> None:
        if not line or not self._stream_reply:
            return
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            return
        if "error" in obj:
            self._finish_stream_turn(False, str(obj["error"]))
            return
        message = obj.get("message") or {}
        thinking = message.get("thinking") or ""
        content = message.get("content") or ""
        if thinking and not content and not self._stream_text and self._stream_reply:
            if not self._current_request_sent_think_false and not self._turn_thinking_retry_used:
                self._models_require_think_false.add(self._active_request.model)
                self._turn_thinking_retry_used = True
                self._pending_think_false_retry = True
                self._abort_stream_for_thinking_retry()
                return
        if content:
            if not self._got_first_token:
                self._got_first_token = True
                self._first_token_ms = self._request_timer.elapsed()
                self._first_token_timer.stop()
            self._stream_text += content
            self.token.emit(int(self._turn_id), content)
        if obj.get("done"):
            metrics = LlmMetrics(
                promptEvalCount=int(obj.get("prompt_eval_count", 0)),
                promptEvalCachedCount=int(obj.get("prompt_eval_cached_count", -1)),
                promptEvalNs=int(obj.get("prompt_eval_duration", 0)),
                evalCount=int(obj.get("eval_count", 0)),
                evalNs=int(obj.get("eval_duration", 0)),
                loadNs=int(obj.get("load_duration", 0)),
                totalNs=int(obj.get("total_duration", 0)),
                msToFirstToken=self._first_token_ms,
                doneReason=str(obj.get("done_reason", "")),
            )
            self._overall_timer.stop()
            if not self._stream_text:
                self._finish_stream_turn(
                    False,
                    "Ollama finished with no speakable content. If the model is Qwen 3.5 / "
                    "eagle-clm, ensure think:false is set (see eagle.ollama logs).",
                )
                return
            self.finished.emit(int(self._turn_id), self._stream_text, metrics)

    def _abort_stream_for_thinking_retry(self) -> None:
        reply = self._stream_reply
        if not reply:
            return
        self._stream_reply = None
        self._first_token_timer.stop()
        self._overall_timer.stop()
        reply.abort()

    def _finish_stream_turn(self, success: bool, error: str) -> None:
        self._overall_timer.stop()
        self._first_token_timer.stop()
        if not success:
            self.failed.emit(int(self._turn_id), error, True)

    def _on_stream_ready_read(self) -> None:
        if not self._stream_reply:
            return
        self._line_buffer += self._stream_reply.readAll().data()
        while True:
            idx = self._line_buffer.find(b"\n")
            if idx < 0:
                break
            line = self._line_buffer[:idx].strip()
            self._line_buffer = self._line_buffer[idx + 1 :]
            self._parse_stream_line(line)

    def _on_stream_finished(self) -> None:
        if self._pending_think_false_retry:
            self._pending_think_false_retry = False
            self._begin_turn_stream()
            return
        reply = self._stream_reply
        if reply:
            self._stream_reply = None
            reply.deleteLater()

    def _onFirstTokenTimeout(self) -> None:
        self._finish_stream_turn(False, "Timed out waiting for first token from Ollama.")

    def _onOverallTimeout(self) -> None:
        self._safe_abort_stream()
        self.failed.emit(int(self._turn_id), "Ollama request timed out.", True)

    def probe(self) -> None:
        url = QUrl(f"{self._base_url()}/api/tags")
        reply = self._network.get(QNetworkRequest(url))
        reply.finished.connect(lambda: self._on_probe_finished(reply))

    def _on_probe_finished(self, reply: QNetworkReply) -> None:
        reply.deleteLater()
        if reply.error() != QNetworkReply.NetworkError.NoError:
            self.probed.emit(False, "", [])
            return
        doc = json.loads(reply.readAll().data())
        models: list[str] = []
        for value in doc.get("models", []):
            name = value.get("name", "")
            if name:
                models.append(name)
        self._installed_models = models
        self.probed.emit(True, "", models)

    def preload(self, model: str) -> None:
        self._aux_queue.append(_AuxJob(_AuxKind.Preload, model=model))
        self._pump_aux_queue()

    def showModel(self, model: str) -> None:
        self._aux_queue.append(_AuxJob(_AuxKind.Show, model=model))
        self._pump_aux_queue()

    def runStartupSequence(
        self, model: str, fallback_model: str, system_prompt: str
    ) -> None:
        self._aux_queue.clear()
        self._in_startup_sequence = True
        self._startup_primary_model = model
        self._startup_fallback_model = fallback_model
        self._startup_system_prompt = system_prompt
        self._aux_queue.append(_AuxJob(_AuxKind.Show, model=model))
        if fallback_model and fallback_model != model:
            self._aux_queue.append(_AuxJob(_AuxKind.Show, model=fallback_model))
        self._aux_queue.append(_AuxJob(_AuxKind.Preload, model=model))
        self._pump_aux_queue()

    def _pump_aux_queue(self) -> None:
        if self._aux_reply or not self._aux_queue:
            return
        job = self._aux_queue.pop(0)
        self._start_aux_job(job)

    def _start_aux_job(self, job: _AuxJob) -> None:
        if job.kind == _AuxKind.Show:
            url = QUrl(f"{self._base_url()}/api/show")
            request = QNetworkRequest(url)
            request.setHeader(
                QNetworkRequest.KnownHeaders.ContentTypeHeader, "application/json"
            )
            body = json.dumps({"model": job.model}).encode("utf-8")
            self._aux_purpose = f"show:{job.model}"
            self._aux_reply = self._network.post(request, body)
            self._aux_reply.finished.connect(self._on_aux_finished)
            return
        body: dict
        if job.kind == _AuxKind.Preload:
            self._aux_purpose = "preload"
            body = {
                "model": job.model,
                "messages": [],
                "stream": False,
                "keep_alive": self._config.llmKeepAlive(),
            }
        else:
            self._aux_purpose = "warmup"
            messages = [
                {"role": "system", "content": job.systemPrompt},
                {"role": "user", "content": "Reply with one word: Ready."},
            ]
            req = LlmRequest(model=job.model, purpose="warmup", numPredict=2, temperature=0.1)
            body = self._build_chat_body(job.model, [], req, True, False)
            body["messages"] = messages
        url = QUrl(f"{self._base_url()}/api/chat")
        request = QNetworkRequest(url)
        request.setHeader(
            QNetworkRequest.KnownHeaders.ContentTypeHeader, "application/json"
        )
        self._aux_reply = self._network.post(request, json.dumps(body).encode("utf-8"))
        self._aux_reply.finished.connect(self._on_aux_finished)

    def _on_aux_finished(self) -> None:
        reply = self._aux_reply
        if not reply:
            return
        purpose = self._aux_purpose
        self._aux_reply = None
        ok = reply.error() == QNetworkReply.NetworkError.NoError
        reply.deleteLater()
        self._pump_aux_queue()
        if purpose == "warmup":
            self.warmUpFinished.emit(ok)
        if self._in_startup_sequence and not self._aux_queue and ok:
            self._in_startup_sequence = False

    @property
    def installedModels(self) -> list[str]:
        return self._installed_models

    @property
    def loadOptions(self) -> _LoadOptions:
        return self._load_options
