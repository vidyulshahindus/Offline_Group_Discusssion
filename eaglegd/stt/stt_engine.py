from __future__ import annotations

from PySide6.QtCore import QObject, QElapsedTimer, Signal, QTimer
from PySide6.QtMultimedia import QAudioDevice, QAudioFormat

from eaglegd.app.app_config import AppConfig
from eaglegd.app.paths import Paths
from eaglegd.app.service_supervisor import ServiceSupervisor
from eaglegd.core.language_service import LanguageService
from eaglegd.prompt_types import SessionLanguage
from eaglegd.stt.audio_capture import AudioCapture
from eaglegd.stt.vad import Vad, VadConfig, VadSegment
from eaglegd.stt.whisper_server_client import WhisperServerClient

_MAX_SESSION_BYTES = 16000 * 2 * 120
_MIC_NOISE_FLOORS: dict[bytes, float] = {}


class SttEngine(QObject):
    listening_changed = Signal(bool)
    partial_text = Signal(str)
    final_text = Signal(object, object)
    status_changed = Signal(str)
    error_occurred = Signal(str)
    input_level = Signal(float)

    def __init__(
        self,
        config: AppConfig,
        paths: Paths,
        supervisor: ServiceSupervisor,
        parent: QObject | None = None,
    ) -> None:
        super().__init__(parent)
        self._config = config
        self._supervisor = supervisor
        self._language = LanguageService()
        self._capture = AudioCapture(self)
        self._vad = Vad(
            VadConfig(
                frame_ms=config.stt_vad_frame_ms(),
                start_threshold_db=config.stt_vad_start_threshold_db(),
                end_hangover_ms=config.stt_vad_end_hangover_ms(),
                min_segment_ms=config.stt_vad_min_segment_ms(),
                max_segment_ms=config.stt_vad_max_segment_ms(),
                pre_roll_ms=config.stt_vad_pre_roll_ms(),
            )
        )
        self._client = WhisperServerClient(self)
        self._stats_timer = QTimer(self)
        self._stats_timer.setInterval(1000)
        self._bias_terms: list[str] = []
        self._locked_reply = SessionLanguage.ENGLISH
        self._mic_device_id = b""
        self._confirmed = ""
        self._last_segment_tail = ""
        self._session_pcm = bytearray()
        self._stream_ms = 0
        self._session_audio_ms = 0
        self._bytes_this_second = 0
        self._max_frame_db = -70.0
        self._segments_sent = 0
        self._segments_returned = 0
        self._got_segment_text = False
        self._listening = False
        self._waiting_drain = False
        self._pending_fallback = False
        self._playback_active = False
        self._stop_timer = QElapsedTimer()

        self._language.load(config, paths)
        self._bias_terms = config.stt_bias_terms()
        self._client.set_base_url(supervisor.whisper_base_url())
        self._client.set_hallucination_filter(self._language.whisper_hallucinations())

        self._stats_timer.timeout.connect(self._on_stats_tick)
        self._capture.pcm16k_ready.connect(self._on_pcm)
        self._capture.error_occurred.connect(self.error_occurred)
        self._client.segment_text.connect(self._on_segment_text)
        self._client.inference_finished.connect(self._on_inference_finished)
        self._client.queue_idle.connect(self._on_queue_idle)
        self._client.error_occurred.connect(self.error_occurred)

    def microphones(self) -> list[QAudioDevice]:
        return self._capture.microphones()

    def set_microphone(self, device: QAudioDevice) -> None:
        self._capture.set_microphone(device)

    def set_locked_reply_language(self, language: SessionLanguage) -> None:
        self._locked_reply = language

    def set_bias_terms(self, terms: list[str]) -> None:
        self._bias_terms = list(terms)

    def set_playback_active(self, active: bool) -> None:
        if self._playback_active == active:
            return
        self._playback_active = active

    def is_listening(self) -> bool:
        return self._listening

    def is_busy(self) -> bool:
        return (
            self._listening
            or self._client.is_busy()
            or self._waiting_drain
            or self._pending_fallback
        )

    def toggle_listening(self) -> None:
        if self._listening:
            self.stop_listening()
        else:
            self.start_listening()

    def start_listening(self) -> None:
        if self._listening:
            return
        if self._playback_active and self._config.audio_mute_mic_while_speaking():
            self.error_occurred.emit("Stop Eagle's speech before listening.")
            return
        lang = self._whisper_language_arg()
        if not self._supervisor.ensure_whisper_server(lang):
            self.error_occurred.emit(
                "whisper-server is not available. Check Readiness."
            )
            return
        self._client.set_base_url(self._supervisor.whisper_base_url())
        self._confirmed = ""
        self._last_segment_tail = ""
        self._session_pcm = bytearray()
        self._stream_ms = 0
        self._session_audio_ms = 0
        self._bytes_this_second = 0
        self._max_frame_db = -70.0
        self._segments_sent = 0
        self._segments_returned = 0
        self._got_segment_text = False
        self._waiting_drain = False
        self._pending_fallback = False

        dev = self._capture.device
        self._mic_device_id = bytes(dev.id())
        has_prior = self._mic_device_id in _MIC_NOISE_FLOORS
        inherited = _MIC_NOISE_FLOORS.get(self._mic_device_id, -50.0)
        self._vad.reset_session(inherited, has_prior)

        self._capture.start()
        if not self._capture.is_capturing():
            return

        self._listening = True
        self._stats_timer.start()
        self.listening_changed.emit(True)
        self.status_changed.emit("Listening…")
        self.partial_text.emit(self._display_transcript())
        self.input_level.emit(0.0)

    def stop_listening(self) -> None:
        if not self._listening and not self._client.is_busy():
            return
        self._stats_timer.stop()
        self._capture.stop()
        self._listening = False
        self.listening_changed.emit(False)
        self.input_level.emit(0.0)

        _MIC_NOISE_FLOORS[self._mic_device_id] = self._vad.noise_floor_db

        for seg in self._vad.flush(self._stream_ms):
            self._enqueue_segment(seg, False)

        self._waiting_drain = True
        self._stop_timer.start()
        self.status_changed.emit("Transcribing…")
        self._client.drain()
        if not self._client.is_busy():
            self._on_queue_idle()

    def _on_pcm(self, pcm: bytes) -> None:
        muted = self._playback_active and self._config.audio_mute_mic_while_speaking()
        if not self._listening or muted:
            return
        self._bytes_this_second += len(pcm)
        self._append_session_pcm(pcm)
        for seg in self._vad.push_pcm16k(pcm, 0):
            if seg.pcm16k:
                self._enqueue_segment(seg, False)
        self._stream_ms += (len(pcm) // 2) * 1000 // 16000
        self._session_audio_ms = self._stream_ms
        self._max_frame_db = max(self._max_frame_db, self._vad.last_frame_db)
        self.partial_text.emit(self._display_transcript())
        self.input_level.emit(self._level_from_db(self._vad.last_frame_db))

    def _append_session_pcm(self, pcm: bytes) -> None:
        if len(self._session_pcm) >= _MAX_SESSION_BYTES:
            return
        room = _MAX_SESSION_BYTES - len(self._session_pcm)
        self._session_pcm.extend(pcm[:room])

    def _enqueue_segment(self, seg: VadSegment, fallback_pass: bool) -> None:
        if not seg.pcm16k and not fallback_pass:
            return
        pcm = bytes(self._session_pcm) if fallback_pass else seg.pcm16k
        if len(pcm) < 8000:
            return
        self._segments_sent += 1
        self._client.enqueue(
            pcm,
            self._build_prompt(),
            0 if fallback_pass else seg.start_ms,
            self._session_audio_ms if fallback_pass else seg.end_ms,
            fallback_pass,
        )

    def _on_stats_tick(self) -> None:
        if not self._listening:
            return
        self._bytes_this_second = 0

    def _on_segment_text(self, text: str) -> None:
        cleaned = self._language.filter_whisper_hallucination(text)
        if not cleaned:
            return
        self._got_segment_text = True
        if self._confirmed:
            self._confirmed += " "
        self._confirmed += cleaned
        self._last_segment_tail = cleaned
        self.partial_text.emit(self._display_transcript())

    def _on_inference_finished(
        self,
        segment_start_ms: int,
        segment_end_ms: int,
        reply_ms: int,
        char_count: int,
        error: str,
        fallback_pass: bool,
    ) -> None:
        del segment_start_ms, segment_end_ms, reply_ms, char_count, error, fallback_pass
        self._segments_returned += 1

    def _on_queue_idle(self) -> None:
        if not self._waiting_drain:
            return
        if (
            not self._got_segment_text
            and not self._pending_fallback
            and len(self._session_pcm) >= 8000
        ):
            self._pending_fallback = True
            whole = VadSegment(
                pcm16k=b"",
                start_ms=0,
                end_ms=self._session_audio_ms,
            )
            self._enqueue_segment(whole, True)
            self._client.drain()
            return

        self._waiting_drain = False
        self._pending_fallback = False
        self.status_changed.emit("Ready")
        note: list[str] = []
        reply = self._language.reply_language_for(
            self._confirmed, self._locked_reply, note
        )
        self.final_text.emit(self._confirmed, reply)

    def _build_prompt(self) -> str:
        parts: list[str] = []
        if self._bias_terms:
            terms = ", ".join(self._bias_terms)
            if len(terms) > 150:
                terms = terms[:147] + "..."
            parts.append(terms)
        if self._last_segment_tail:
            tail = self._last_segment_tail
            if len(tail) > 120:
                tail = tail[-120:]
            parts.append(tail)
        return ", ".join(parts)

    def _display_transcript(self) -> str:
        if not self._confirmed:
            return "…" if self._listening else ""
        return f"{self._confirmed} …" if self._listening else self._confirmed

    def _whisper_language_arg(self) -> str:
        mode = self._config.stt_language_mode()
        if mode == "auto":
            return "auto"
        if mode == "hi":
            return "hi"
        if mode == "en":
            return "en"
        return "hi" if self._locked_reply == SessionLanguage.HINDI else "en"

    @staticmethod
    def _level_from_db(db: float) -> float:
        norm = (db + 60.0) / 60.0
        return float(max(0.0, min(1.0, norm)))

    @staticmethod
    def sample_format_name(fmt: QAudioFormat.SampleFormat) -> str:
        names = {
            QAudioFormat.SampleFormat.UInt8: "UInt8",
            QAudioFormat.SampleFormat.Int16: "Int16",
            QAudioFormat.SampleFormat.Int32: "Int32",
            QAudioFormat.SampleFormat.Float: "Float",
        }
        return names.get(fmt, "Unknown")
