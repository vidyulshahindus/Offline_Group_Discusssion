from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from datetime import datetime, timezone

from PySide6.QtCore import QObject, QUrl, Signal
from PySide6.QtNetwork import (
    QHttpMultiPart,
    QHttpPart,
    QNetworkAccessManager,
    QNetworkReply,
    QNetworkRequest,
)

from eaglegd.stt.wav_util import pcm16k_mono_to_wav


@dataclass
class _Job:
    pcm: bytes
    prompt: str
    segment_start_ms: int = -1
    segment_end_ms: int = -1
    fallback_pass: bool = False
    sent_at_ms: int = 0


class WhisperServerClient(QObject):
    segment_text = Signal(str)
    inference_finished = Signal(int, int, int, int, str, bool)
    queue_idle = Signal()
    error_occurred = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._network = QNetworkAccessManager(self)
        self._base_url = QUrl()
        self._queue: deque[_Job] = deque()
        self._in_flight_job = _Job(b"", "")
        self._in_flight = False
        self._draining = False
        self._hallucinations: list[str] = []

    def set_base_url(self, base_url: QUrl) -> None:
        self._base_url = base_url

    def set_hallucination_filter(self, phrases: list[str]) -> None:
        self._hallucinations = list(phrases)

    def enqueue(
        self,
        pcm16k: bytes,
        prompt: str,
        segment_start_ms: int = -1,
        segment_end_ms: int = -1,
        fallback_pass: bool = False,
    ) -> None:
        if len(pcm16k) < 8000:
            return
        self._queue.append(
            _Job(
                pcm=pcm16k,
                prompt=prompt,
                segment_start_ms=segment_start_ms,
                segment_end_ms=segment_end_ms,
                fallback_pass=fallback_pass,
            )
        )
        self._pump()

    def drain(self) -> None:
        self._draining = True
        if not self._in_flight and not self._queue:
            self._draining = False
            self.queue_idle.emit()

    def is_busy(self) -> bool:
        return self._in_flight or bool(self._queue)

    def pending_count(self) -> int:
        return len(self._queue) + (1 if self._in_flight else 0)

    def _pump(self) -> None:
        if self._in_flight or not self._queue or not self._base_url.isValid():
            return
        self._in_flight_job = self._queue.popleft()
        self._in_flight_job.sent_at_ms = int(datetime.now(timezone.utc).timestamp() * 1000)
        self._in_flight = True

        multi = QHttpMultiPart(QHttpMultiPart.ContentType.FormDataType)

        file_part = QHttpPart()
        file_part.setHeader(
            QNetworkRequest.KnownHeaders.ContentTypeHeader, b"audio/wav"
        )
        file_part.setHeader(
            QNetworkRequest.KnownHeaders.ContentDispositionHeader,
            b'form-data; name="file"; filename="segment.wav"',
        )
        file_part.setBody(pcm16k_mono_to_wav(self._in_flight_job.pcm))

        temp_part = QHttpPart()
        temp_part.setHeader(
            QNetworkRequest.KnownHeaders.ContentDispositionHeader,
            b'form-data; name="temperature"',
        )
        temp_part.setBody(b"0.0")

        temp_inc_part = QHttpPart()
        temp_inc_part.setHeader(
            QNetworkRequest.KnownHeaders.ContentDispositionHeader,
            b'form-data; name="temperature_inc"',
        )
        temp_inc_part.setBody(b"0.2")

        format_part = QHttpPart()
        format_part.setHeader(
            QNetworkRequest.KnownHeaders.ContentDispositionHeader,
            b'form-data; name="response_format"',
        )
        format_part.setBody(b"json")

        prompt_part = QHttpPart()
        prompt_part.setHeader(
            QNetworkRequest.KnownHeaders.ContentDispositionHeader,
            b'form-data; name="prompt"',
        )
        prompt_part.setBody(self._in_flight_job.prompt.encode("utf-8"))

        for part in (file_part, temp_part, temp_inc_part, format_part, prompt_part):
            multi.append(part)

        url = self._base_url.resolved(QUrl("/inference"))
        reply = self._network.post(QNetworkRequest(url), multi)
        multi.setParent(reply)
        reply.finished.connect(self._on_finished)

    def _on_finished(self) -> None:
        reply = self.sender()
        if not isinstance(reply, QNetworkReply):
            return
        reply_ms = int(datetime.now(timezone.utc).timestamp() * 1000) - self._in_flight_job.sent_at_ms
        self._in_flight = False

        error = ""
        text = ""
        if reply.error() != QNetworkReply.NetworkError.NoError:
            error = reply.errorString()
        else:
            text, parse_err = self._parse_text(bytes(reply.readAll()))
            error = parse_err
            text = self._filter_text(text, self._hallucinations)

        self.inference_finished.emit(
            self._in_flight_job.segment_start_ms,
            self._in_flight_job.segment_end_ms,
            reply_ms,
            len(text),
            error,
            self._in_flight_job.fallback_pass,
        )
        if error:
            self.error_occurred.emit(error)
        elif text:
            self.segment_text.emit(text)

        reply.deleteLater()
        if self._queue:
            self._pump()
        elif self._draining and not self._in_flight:
            self._draining = False
            self.queue_idle.emit()

    @staticmethod
    def _parse_text(body: bytes) -> tuple[str, str]:
        import json

        try:
            doc = json.loads(body.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return "", "Invalid JSON from whisper-server"
        if not isinstance(doc, dict):
            return "", "Invalid JSON from whisper-server"
        return str(doc.get("text", "")).strip(), ""

    @staticmethod
    def _filter_text(text: str, hallucinations: list[str]) -> str:
        trimmed = text.strip()
        for h in hallucinations:
            if trimmed.casefold() == h.casefold():
                return ""
        if trimmed.casefold() == "you":
            return ""
        return trimmed
