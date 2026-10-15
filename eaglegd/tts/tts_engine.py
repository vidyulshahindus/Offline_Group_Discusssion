from __future__ import annotations

from PySide6.QtCore import QObject, Signal

from eaglegd.app.app_config import AppConfig
from eaglegd.app.paths import Paths
from eaglegd.prompt_types import SessionLanguage
from eaglegd.tts.filler_cache import FillerCache
from eaglegd.tts.piper_cli_backend import PiperCliBackend


class TtsEngine(QObject):
    wav_ready = Signal(int, int, str, int)
    first_tts_done = Signal(int, int)
    synthesis_failed = Signal(int, int, str)
    error_occurred = Signal(str)

    def __init__(
        self,
        config: AppConfig,
        paths: Paths,
        parent: QObject | None = None,
    ) -> None:
        super().__init__(parent)
        self._config = config
        self._paths = paths
        self._en = PiperCliBackend(self)
        self._hi = PiperCliBackend(self)
        self._fillers = FillerCache()
        self._active_language = SessionLanguage.ENGLISH
        self._connect_worker(self._en)
        self._connect_worker(self._hi)

    def _connect_worker(self, worker: PiperCliBackend) -> None:
        worker.wav_ready.connect(self._on_wav_ready)
        worker.synthesis_failed.connect(self._on_synthesis_failed)

    def _on_wav_ready(self, turn_id: int, sentence_index: int, wav_path: str, synth_ms: int) -> None:
        if turn_id >= 0:
            self.first_tts_done.emit(turn_id, sentence_index)
        self.wav_ready.emit(turn_id, sentence_index, wav_path, synth_ms)

    def _on_synthesis_failed(self, turn_id: int, sentence_index: int, error: str) -> None:
        self.synthesis_failed.emit(turn_id, sentence_index, error)
        if turn_id >= 0:
            self.error_occurred.emit(error)

    @property
    def fillers(self) -> FillerCache:
        return self._fillers

    def start_session(self) -> None:
        self._ensure_worker(SessionLanguage.ENGLISH)
        self._ensure_worker(SessionLanguage.HINDI)
        self._en.start_worker()
        self._hi.start_worker()
        if self._config.tts_fillers_enabled():
            self._fillers.load(self._config, self._paths)
            self._fillers.warm_up(self._en, self._hi)

    def stop(self) -> None:
        self._en.stop_worker()
        self._hi.stop_worker()

    def _voice_for(self, language: SessionLanguage) -> str:
        return (
            self._config.tts_voice_hi()
            if language == SessionLanguage.HINDI
            else self._config.tts_voice_en()
        )

    def _ensure_worker(self, language: SessionLanguage) -> None:
        worker = self._hi if language == SessionLanguage.HINDI else self._en
        if worker.is_configured():
            return
        voice = self._voice_for(language)
        exe = self._paths.piper_executable()
        onnx = self._paths.piper_voice_onnx(voice)
        scale = (
            self._config.tts_length_scale_hi()
            if language == SessionLanguage.HINDI
            else self._config.tts_length_scale_default()
        )
        if exe:
            worker.configure(str(exe), str(onnx), scale)

    def speak_sentence(
        self, turn_id: int, sentence_index: int, text: str, language: SessionLanguage
    ) -> None:
        self._active_language = language
        self._ensure_worker(language)
        worker = self._hi if language == SessionLanguage.HINDI else self._en
        worker.start_worker()
        worker.synthesize(turn_id, sentence_index, text)

    def speak_test_phrase(
        self, language: SessionLanguage, phrase_index: int, text: str
    ) -> None:
        self._ensure_worker(language)
        worker = self._hi if language == SessionLanguage.HINDI else self._en
        worker.start_worker()
        worker.synthesize(-1, phrase_index, text)

    def is_busy(self) -> bool:
        return self._en.has_pending_jobs() or self._hi.has_pending_jobs()

    def last_cold_synth_ms(self, language: SessionLanguage) -> int:
        worker = self._hi if language == SessionLanguage.HINDI else self._en
        return worker.last_cold_synth_ms

    def last_warm_synth_ms(self, language: SessionLanguage) -> int:
        worker = self._hi if language == SessionLanguage.HINDI else self._en
        return worker.last_warm_synth_ms
