"""Ollama HTTP backend with streaming chat (ported from OllamaBackend)."""

from __future__ import annotations

import json
import logging
import re
from collections import deque
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Deque, Dict, List, Optional, Set

import httpx
from PySide6.QtCore import QElapsedTimer, QObject, QThread, QTimer, Signal, Slot

from eaglegd.llm.config import AppConfigProtocol
from eaglegd.llm.il_llm_backend import ILlmBackend, LlmMessage, LlmMetrics, LlmRequest

_log = logging.getLogger("eagle.ollama")

_THINKING_NAME_RE = re.compile(r"eagle-clm|qwen3", re.IGNORECASE)


def _base_url_trimmed(config: AppConfigProtocol) -> str:
    url = config.llm_base_url().strip()
    if url.endswith("/"):
        url = url[:-1]
    return url


@dataclass
class OllamaLoadOptions:
    num_ctx: int = 4096
    num_thread: int = 0
    num_batch: int = 256


class _StartupStep(Enum):
    Idle = auto()
    ShowPrimary = auto()
    ShowFallback = auto()
    Preload = auto()
    WarmUp = auto()


class _AuxKind(Enum):
    Show = auto()
    Preload = auto()
    WarmUp = auto()


@dataclass
class _AuxJob:
    kind: _AuxKind = _AuxKind.Preload
    model: str = ""
    system_prompt: str = ""


@dataclass
class _ModelInfo:
    capabilities: List[str] = field(default_factory=list)
    size_bytes: int = 0


class _OllamaHttpWorker(QObject):
    """Runs blocking httpx calls on a dedicated QThread."""

    probe_finished = Signal(bool, bytes, str)
    post_finished = Signal(int, bool, int, bytes, str)
    stream_line = Signal(int, bytes)
    stream_http_finished = Signal(int, bool, int, str)

    run_probe = Signal(str, float)
    run_post = Signal(int, str, bytes, float)
    run_stream = Signal(int, str, bytes, float)
    abort_stream = Signal()

    def __init__(self) -> None:
        super().__init__()
        self._stream_response: Optional[httpx.Response] = None
        self._abort_requested = False

        self.run_probe.connect(self._do_probe)
        self.run_post.connect(self._do_post)
        self.run_stream.connect(self._do_stream)
        self.abort_stream.connect(self._do_abort_stream)

    @Slot()
    def _do_abort_stream(self) -> None:
        self._abort_requested = True
        resp = self._stream_response
        if resp is not None:
            resp.close()

    @Slot(str, float)
    def _do_probe(self, base_url: str, timeout_s: float) -> None:
        url = base_url.rstrip("/") + "/api/tags"
        try:
            with httpx.Client(timeout=timeout_s) as client:
                response = client.get(url)
                ok = response.is_success
                err = "" if ok else response.text
                self.probe_finished.emit(ok, response.content, err)
        except Exception as exc:  # noqa: BLE001
            self.probe_finished.emit(False, b"", str(exc))

    @Slot(int, str, bytes, float)
    def _do_post(self, job_id: int, url: str, body: bytes, timeout_s: float) -> None:
        try:
            with httpx.Client(timeout=timeout_s) as client:
                response = client.post(
                    url,
                    content=body,
                    headers={"Content-Type": "application/json"},
                )
                ok = response.is_success
                err = "" if ok else response.text
                self.post_finished.emit(job_id, ok, response.status_code, response.content, err)
        except Exception as exc:  # noqa: BLE001
            self.post_finished.emit(job_id, False, 0, b"", str(exc))

    @Slot(int, str, bytes, float)
    def _do_stream(self, job_id: int, url: str, body: bytes, timeout_s: float) -> None:
        self._abort_requested = False
        self._stream_response = None
        timeout = httpx.Timeout(timeout_s, connect=30.0)
        try:
            with httpx.Client(timeout=timeout) as client:
                with client.stream(
                    "POST",
                    url,
                    content=body,
                    headers={"Content-Type": "application/json"},
                ) as response:
                    self._stream_response = response
                    status = response.status_code
                    if response.is_error:
                        data = response.read()
                        self.stream_http_finished.emit(
                            job_id,
                            False,
                            status,
                            data.decode(errors="replace") or response.reason_phrase,
                        )
                        return

                    buffer = b""
                    for chunk in response.iter_bytes():
                        if self._abort_requested:
                            self.stream_http_finished.emit(job_id, False, status, "canceled")
                            return
                        buffer += chunk
                        while b"\n" in buffer:
                            line, buffer = buffer.split(b"\n", 1)
                            stripped = line.strip()
                            if stripped:
                                self.stream_line.emit(job_id, stripped)

                    tail = buffer.strip()
                    if tail:
                        self.stream_line.emit(job_id, tail)
                    self.stream_http_finished.emit(job_id, True, status, "")
        except httpx.HTTPError as exc:
            if self._abort_requested:
                self.stream_http_finished.emit(job_id, False, 0, "canceled")
            else:
                self.stream_http_finished.emit(job_id, False, 0, str(exc))
        except Exception as exc:  # noqa: BLE001
            self.stream_http_finished.emit(job_id, False, 0, str(exc))
        finally:
            self._stream_response = None


class OllamaBackend(ILlmBackend):
    warm_up_finished = Signal(bool)
    startup_sequence_finished = Signal(bool)

    _JOB_PROBE = -1
    _JOB_STREAM = -2

    def __init__(self, config: AppConfigProtocol, parent: Optional[QObject] = None) -> None:
        super().__init__(parent)
        self._config = config

        self._load_options = OllamaLoadOptions(
            num_ctx=config.llm_num_ctx(),
            num_thread=config.llm_num_thread(),
            num_batch=config.llm_num_batch(),
        )

        self._worker = _OllamaHttpWorker()
        self._thread = QThread(self)
        self._worker.moveToThread(self._thread)
        self._thread.start()

        self._worker.probe_finished.connect(self._on_probe_finished)
        self._worker.post_finished.connect(self._on_post_finished)
        self._worker.stream_line.connect(self._on_stream_line)
        self._worker.stream_http_finished.connect(self._on_stream_http_finished)

        self._first_token_timer = QTimer(self)
        self._first_token_timer.setSingleShot(True)
        self._first_token_timer.setInterval(12000)
        self._first_token_timer.timeout.connect(self._on_first_token_timeout)

        self._overall_timer = QTimer(self)
        self._overall_timer.setSingleShot(True)
        self._overall_timer.setInterval(config.llm_request_timeout_ms())
        self._overall_timer.timeout.connect(self._on_overall_timeout)

        self._warm_schedule_timer = QTimer(self)
        self._warm_schedule_timer.setSingleShot(True)
        self._warm_schedule_timer.timeout.connect(self._on_warm_schedule_timeout)

        self._aux_queue: Deque[_AuxJob] = deque()
        self._active_aux_job: Optional[_AuxJob] = None
        self._aux_in_flight = False
        self._aux_job_serial = 0
        self._pending_aux_by_id: Dict[int, _AuxJob] = {}

        self._stream_in_flight = False
        self._active_request = LlmRequest()
        self._line_buffer = b""
        self._stream_text = ""
        self._stream_purpose = ""
        self._got_first_token = False
        self._logged_first_stream_byte = False
        self._turn_id = -1
        self._request_timer = QElapsedTimer()
        self._stream_post_timer = QElapsedTimer()

        self._installed_models: List[str] = []
        self._model_info: Dict[str, _ModelInfo] = {}
        self._pending_preload_model = ""
        self._aux_purpose = ""

        self._coalesced_warm_model = ""
        self._coalesced_warm_prompt = ""
        self._has_coalesced_warm = False

        self._stream_completed = False
        self._first_token_ms = -1

        self._in_startup_sequence = False
        self._startup_awaiting_warm = False
        self._startup_step = _StartupStep.Idle
        self._startup_primary_model = ""
        self._startup_fallback_model = ""
        self._startup_system_prompt = ""

        self._models_require_think_false: Set[str] = set()
        self._current_request_sent_think_false = False
        self._turn_thinking_retry_used = False
        self._pending_think_false_retry = False

    def __del__(self) -> None:
        try:
            self.abort()
            self._thread.quit()
            self._thread.wait(3000)
        except Exception:  # noqa: BLE001
            pass

    @property
    def load_options(self) -> OllamaLoadOptions:
        return self._load_options

    @property
    def installed_models(self) -> List[str]:
        return list(self._installed_models)

    def is_aux_in_flight(self) -> bool:
        return self._aux_in_flight

    def is_stream_in_flight(self) -> bool:
        return self._stream_in_flight

    def aux_concurrent_count(self) -> int:
        return 1 if self._aux_in_flight else 0

    def is_busy(self) -> bool:
        return self._stream_in_flight or self._aux_in_flight

    def abort_active_turn(self) -> None:
        self._safe_abort_stream()

    def abort(self) -> None:
        self._safe_abort_stream()
        self._safe_abort_aux()
        self._aux_queue.clear()
        self._has_coalesced_warm = False
        self._startup_awaiting_warm = False
        self._in_startup_sequence = False
        self._startup_step = _StartupStep.Idle

    def _safe_abort_stream(self) -> None:
        if not self._stream_in_flight:
            return
        self._worker.abort_stream.emit()
        self._stream_in_flight = False
        self._first_token_timer.stop()
        self._overall_timer.stop()
        self._line_buffer = b""
        self._stream_purpose = ""

    def _abort_stream_for_thinking_retry(self) -> None:
        if not self._stream_in_flight:
            return
        self._worker.abort_stream.emit()
        self._first_token_timer.stop()
        self._overall_timer.stop()

    def _safe_abort_aux(self) -> None:
        if not self._aux_in_flight:
            return
        self._aux_in_flight = False
        self._aux_purpose = ""
        self._active_aux_job = None

    def _model_has_thinking(self, model: str) -> bool:
        return "thinking" in self._model_info.get(model, _ModelInfo()).capabilities

    def _should_send_think_false(self, model: str) -> bool:
        if model in self._models_require_think_false:
            return True
        if self._model_has_thinking(model):
            return True
        return bool(_THINKING_NAME_RE.search(model))

    def _log_chat_request(self, purpose: str, body: dict) -> None:
        _log.info("POST /api/chat %s %s", purpose, json.dumps(body, separators=(",", ":")))

    def _log_stream_line(self, line: bytes, ms_since_post: int) -> None:
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            _log.info(
                "%s NDJSON parse error at %sms: %s",
                self._stream_purpose,
                ms_since_post,
                line[:200],
            )
            return
        if not isinstance(obj, dict):
            return
        message = obj.get("message") or {}
        content_len = len((message.get("content") or ""))
        thinking_len = len((message.get("thinking") or ""))
        _log.info(
            "%s line at %sms done=%s done_reason=%s content_chars=%s thinking_chars=%s",
            self._stream_purpose,
            ms_since_post,
            obj.get("done"),
            obj.get("done_reason"),
            content_len,
            thinking_len,
        )

    def _build_options(self, req: LlmRequest) -> dict:
        options: dict = {
            "num_ctx": self._load_options.num_ctx,
            "num_batch": self._load_options.num_batch,
            "num_predict": req.num_predict,
            "temperature": req.temperature,
            "top_p": req.top_p,
            "top_k": req.top_k,
            "repeat_penalty": req.repeat_penalty,
        }
        if self._load_options.num_thread > 0:
            options["num_thread"] = self._load_options.num_thread
        if req.stop:
            options["stop"] = req.stop
        return options

    def _build_chat_body(
        self,
        model: str,
        messages: List[LlmMessage],
        req: LlmRequest,
        for_warm_up: bool,
        stream: bool,
    ) -> dict:
        msg_array = [{"role": m.role, "content": m.content} for m in messages]
        body: dict = {
            "model": model,
            "messages": msg_array,
            "stream": stream,
            "keep_alive": self._config.llm_keep_alive(),
        }
        if self._should_send_think_false(model):
            body["think"] = False

        effective = LlmRequest(
            model=req.model,
            messages=req.messages,
            num_predict=req.num_predict,
            temperature=req.temperature,
            top_p=req.top_p,
            top_k=req.top_k,
            repeat_penalty=req.repeat_penalty,
            stop=list(req.stop),
            json_schema=dict(req.json_schema),
            purpose=req.purpose,
            turn_id=req.turn_id,
        )
        if for_warm_up:
            effective.num_predict = 2
            effective.temperature = 0.1
        body["options"] = self._build_options(effective)
        if req.json_schema:
            body["format"] = req.json_schema
        return body

    def run_startup_sequence(
        self,
        model: str,
        fallback_model: str,
        system_prompt: str,
    ) -> None:
        self._aux_queue.clear()
        self._has_coalesced_warm = False
        self._in_startup_sequence = True
        self._startup_awaiting_warm = True
        self._startup_primary_model = model
        self._startup_fallback_model = fallback_model
        self._startup_system_prompt = system_prompt
        self._startup_step = _StartupStep.ShowPrimary

        self._aux_queue.append(_AuxJob(_AuxKind.Show, model))
        if fallback_model and fallback_model != model:
            self._aux_queue.append(_AuxJob(_AuxKind.Show, fallback_model))
            self._startup_step = _StartupStep.ShowFallback
        self._aux_queue.append(_AuxJob(_AuxKind.Preload, model))
        self._startup_step = _StartupStep.Preload
        self._pump_aux_queue()

    def schedule_warm_up(self, model: str, system_prompt: str) -> None:
        self._coalesced_warm_model = model
        self._coalesced_warm_prompt = system_prompt
        self._has_coalesced_warm = True
        if self._in_startup_sequence or self._aux_in_flight:
            return
        self._warm_schedule_timer.start(0)

    def _enqueue_coalesced_warm_if_needed(self) -> None:
        if not self._has_coalesced_warm:
            return
        self._aux_queue.append(
            _AuxJob(_AuxKind.WarmUp, self._coalesced_warm_model, self._coalesced_warm_prompt)
        )
        self._has_coalesced_warm = False

    def _on_warm_schedule_timeout(self) -> None:
        if self._has_coalesced_warm and not self._aux_in_flight and not self._in_startup_sequence:
            self._enqueue_coalesced_warm_if_needed()
        self._pump_aux_queue()

    def _pump_aux_queue(self) -> None:
        if self._aux_in_flight:
            return
        if not self._aux_queue:
            if self._has_coalesced_warm and not self._in_startup_sequence:
                self._enqueue_coalesced_warm_if_needed()
            if not self._aux_queue:
                return
        job = self._aux_queue.popleft()
        self._start_aux_job(job)

    def _start_aux_job(self, job: _AuxJob) -> None:
        if self._aux_in_flight:
            _log.warning("start_aux_job while aux in flight")
            return

        self._active_aux_job = job
        self._aux_in_flight = True
        base = _base_url_trimmed(self._config)
        timeout_s = self._config.llm_request_timeout_ms() / 1000.0

        if job.kind == _AuxKind.Show:
            url = base + "/api/show"
            body = json.dumps({"model": job.model}).encode("utf-8")
            self._aux_purpose = f"show:{job.model}"
        elif job.kind == _AuxKind.Preload:
            self._pending_preload_model = job.model
            self._aux_purpose = "preload"
            url = base + "/api/chat"
            body = json.dumps(
                {
                    "model": job.model,
                    "messages": [],
                    "stream": False,
                    "keep_alive": self._config.llm_keep_alive(),
                }
            ).encode("utf-8")
        else:
            self._aux_purpose = "warmup"
            messages = [
                LlmMessage(role="system", content=job.system_prompt),
                LlmMessage(role="user", content="Reply with one word: Ready."),
            ]
            req = LlmRequest(
                model=job.model,
                messages=messages,
                purpose="warmup",
                num_predict=2,
                temperature=0.1,
            )
            chat_body = self._build_chat_body(job.model, messages, req, True, False)
            self._log_chat_request(self._aux_purpose, chat_body)
            url = base + "/api/chat"
            body = json.dumps(chat_body).encode("utf-8")

        if job.kind == _AuxKind.Preload:
            self._log_chat_request(self._aux_purpose, json.loads(body.decode("utf-8")))

        self._aux_job_serial += 1
        job_id = self._aux_job_serial
        self._pending_aux_by_id[job_id] = job
        self._worker.run_post.emit(job_id, url, body, timeout_s)

    def _complete_aux_job(self, job: _AuxJob, ok: bool, body: bytes) -> None:
        if job.kind == _AuxKind.Show:
            if ok:
                try:
                    obj = json.loads(body)
                except json.JSONDecodeError:
                    return
                model = str(obj.get("model", job.model))
                caps: List[str] = []
                for cap in obj.get("capabilities") or []:
                    caps.append(str(cap))
                if not caps:
                    details = obj.get("details") or {}
                    for cap in details.get("capabilities") or []:
                        caps.append(str(cap))
                if not caps and self._should_send_think_false(model):
                    caps.append("thinking")
                info = _ModelInfo(capabilities=caps, size_bytes=int(obj.get("size") or 0))
                self._model_info[model] = info
                self.model_shown.emit(model, caps, info.size_bytes)
            return

        if job.kind == _AuxKind.Preload:
            self.preloaded.emit(self._pending_preload_model, ok)
            self._pending_preload_model = ""
            if self._startup_awaiting_warm:
                self._startup_awaiting_warm = False
                if not self._has_coalesced_warm:
                    self._coalesced_warm_model = self._startup_primary_model
                    self._coalesced_warm_prompt = self._startup_system_prompt
                    self._has_coalesced_warm = True
                self._enqueue_coalesced_warm_if_needed()
            return

        self.warm_up_finished.emit(ok)
        if self._in_startup_sequence:
            self._in_startup_sequence = False
            self._startup_step = _StartupStep.Idle
            self.startup_sequence_finished.emit(ok)

    @Slot(int, bool, int, bytes, str)
    def _on_post_finished(
        self,
        job_id: int,
        ok: bool,
        status: int,
        body: bytes,
        error: str,
    ) -> None:
        job = self._pending_aux_by_id.pop(job_id, None)
        purpose = self._aux_purpose
        self._aux_purpose = ""
        self._aux_in_flight = False
        self._active_aux_job = None

        _log.info("%s HTTP status=%s ok=%s err=%s", purpose, status, ok, error)

        if job is not None:
            self._complete_aux_job(job, ok, body)

        if job is not None and job.kind == _AuxKind.WarmUp and self._has_coalesced_warm and not self._in_startup_sequence:
            self._warm_schedule_timer.start(0)
        else:
            self._pump_aux_queue()

    def start(self, req: LlmRequest) -> None:
        self._safe_abort_stream()
        if self._aux_in_flight:
            _log.warning("Turn start blocked: aux busy (%s)", self._aux_purpose)
            self.failed.emit(
                req.turn_id,
                "Eagle is still preparing the model — try again.",
                True,
            )
            return

        self._active_request = req
        self._turn_id = req.turn_id
        self._turn_thinking_retry_used = False
        self._pending_think_false_retry = False
        self._stream_purpose = f"turn:{req.purpose}"
        self._request_timer.restart()
        self._begin_turn_stream()

    def _begin_turn_stream(self) -> None:
        self._stream_text = ""
        self._line_buffer = b""
        self._got_first_token = False
        self._stream_completed = False
        self._first_token_ms = -1
        self._logged_first_stream_byte = False
        self._stream_post_timer.restart()

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
        self._log_chat_request(self._stream_purpose, body)
        url = _base_url_trimmed(self._config) + "/api/chat"
        payload = json.dumps(body).encode("utf-8")
        timeout_s = self._config.llm_request_timeout_ms() / 1000.0

        self._stream_in_flight = True
        self._worker.run_stream.emit(self._JOB_STREAM, url, payload, timeout_s)
        self._first_token_timer.start()
        self._overall_timer.start()

    def preload(self, model: str) -> None:
        self._aux_queue.append(_AuxJob(_AuxKind.Preload, model))
        self._pump_aux_queue()

    def show_model(self, model: str) -> None:
        self._aux_queue.append(_AuxJob(_AuxKind.Show, model))
        self._pump_aux_queue()

    def probe(self) -> None:
        timeout_s = self._config.llm_request_timeout_ms() / 1000.0
        self._worker.run_probe.emit(_base_url_trimmed(self._config), timeout_s)

    @Slot(bool, bytes, str)
    def _on_probe_finished(self, ok: bool, body: bytes, error: str) -> None:
        if not ok:
            _log.info("probe failed: %s", error)
            self.probed.emit(False, "", [])
            return
        try:
            doc = json.loads(body)
        except json.JSONDecodeError:
            self.probed.emit(False, "", [])
            return
        models: List[str] = []
        for entry in doc.get("models") or []:
            if isinstance(entry, dict):
                name = entry.get("name")
                if name:
                    models.append(str(name))
        self._installed_models = models
        self.probed.emit(True, "", models)

    def _parse_stream_line(self, line: bytes) -> None:
        if not line or not self._stream_in_flight:
            return
        self._log_stream_line(line, self._stream_post_timer.elapsed())

        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            return
        if not isinstance(obj, dict):
            return

        if "error" in obj:
            self._finish_stream_turn(False, str(obj["error"]))
            return

        message = obj.get("message") or {}
        thinking = message.get("thinking") or ""
        content = message.get("content") or ""

        if thinking and not content and not self._stream_text and self._stream_in_flight:
            if not self._current_request_sent_think_false and not self._turn_thinking_retry_used:
                self._models_require_think_false.add(self._active_request.model)
                self._turn_thinking_retry_used = True
                self._pending_think_false_retry = True
                _log.info(
                    "%s thinking-only chunk with no content; retrying once with think:false",
                    self._stream_purpose,
                )
                self._abort_stream_for_thinking_retry()
                return
            if self._current_request_sent_think_false:
                _log.warning(
                    "%s thinking field non-empty despite think:false; ignored",
                    self._stream_purpose,
                )

        if content:
            if not self._got_first_token:
                self._got_first_token = True
                self._first_token_ms = self._request_timer.elapsed()
                self._first_token_timer.stop()
                _log.info("%s first content token at %sms", self._stream_purpose, self._first_token_ms)
            self._stream_text += content
            self.token.emit(int(self._turn_id), content)

        if obj.get("done"):
            metrics = LlmMetrics(
                prompt_eval_count=int(obj.get("prompt_eval_count") or 0),
                prompt_eval_cached_count=int(obj.get("prompt_eval_cached_count", -1)),
                prompt_eval_ns=int(obj.get("prompt_eval_duration") or 0),
                eval_count=int(obj.get("eval_count") or 0),
                eval_ns=int(obj.get("eval_duration") or 0),
                load_ns=int(obj.get("load_duration") or 0),
                total_ns=int(obj.get("total_duration") or 0),
                ms_to_first_token=self._first_token_ms,
                done_reason=str(obj.get("done_reason") or ""),
            )
            self._overall_timer.stop()
            self._stream_completed = True

            if not self._stream_text:
                self._finish_stream_turn(
                    False,
                    "Ollama finished with no speakable content. If the model is Qwen 3.5 / "
                    "eagle-clm, ensure think:false is set (see eagle.ollama logs).",
                )
                return
            self.finished.emit(int(self._turn_id), self._stream_text, metrics)

    def _finish_stream_turn(self, success: bool, error: str = "") -> None:
        self._overall_timer.stop()
        self._first_token_timer.stop()
        if not success:
            self.failed.emit(int(self._turn_id), error, True)

    @Slot(int, bytes)
    def _on_stream_line(self, job_id: int, line: bytes) -> None:
        if job_id != self._JOB_STREAM or not self._stream_in_flight:
            return
        if not self._logged_first_stream_byte:
            self._logged_first_stream_byte = True
            _log.info(
                "%s first NDJSON bytes at %sms",
                self._stream_purpose,
                self._stream_post_timer.elapsed(),
            )
        self._parse_stream_line(line)

    @Slot(int, bool, int, str)
    def _on_stream_http_finished(self, job_id: int, ok: bool, status: int, error: str) -> None:
        if job_id != self._JOB_STREAM:
            return

        was_retry = self._pending_think_false_retry
        if not ok and error == "canceled" and was_retry:
            self._pending_think_false_retry = False
            self._line_buffer = b""
            self._stream_purpose = self._stream_purpose or f"turn:{self._active_request.purpose}"
            self._begin_turn_stream()
            return

        self._stream_in_flight = False
        _log.info(
            "%s HTTP finished status=%s ok=%s error=%s",
            self._stream_purpose,
            status,
            ok,
            error,
        )

        if not ok and not self._stream_completed:
            if was_retry:
                self._pending_think_false_retry = False
            self._handle_model_fallback(error)
            self._line_buffer = b""
            self._stream_purpose = ""
            self._first_token_timer.stop()
            self._overall_timer.stop()
            return

        if not self._stream_completed:
            self._finish_stream_turn(
                False,
                "Ollama stream closed without done:true (possible proxy or truncated response).",
            )

        self._line_buffer = b""
        self._stream_purpose = ""
        self._first_token_timer.stop()
        self._overall_timer.stop()

    def _handle_model_fallback(self, error_text: str) -> None:
        fallback = self._config.llm_fallback_model()
        if (
            self._active_request.model != fallback
            and fallback in self._installed_models
            and "not found" in error_text.lower()
        ):
            self._active_request.model = fallback
            self.start(self._active_request)
            return
        self.failed.emit(int(self._turn_id), error_text, True)

    def _on_first_token_timeout(self) -> None:
        if not self._got_first_token and self._stream_in_flight:
            self._finish_stream_turn(
                False,
                "Timed out waiting for the first speakable token from Ollama "
                "(content was empty; check think:false).",
            )
            self._safe_abort_stream()

    def _on_overall_timeout(self) -> None:
        if self._stream_in_flight:
            self._finish_stream_turn(False, "Ollama request timed out.")
            self._safe_abort_stream()
