from __future__ import annotations

from typing import TYPE_CHECKING

from PySide6.QtCore import QObject, Signal

from eaglegd.llm.PromptTypes import SessionLanguage
from eaglegd.tts.FillerCache import FillerCache
from eaglegd.tts.PiperCliBackend import PiperCliBackend

if TYPE_CHECKING:
    from eaglegd.app.AppConfig import AppConfig
    from eaglegd.app.Paths import Paths


class TtsEngine(QObject):
    wavReady = Signal(int, int, str, int)
    firstTtsDone = Signal(int, int)
    synthesisFailed = Signal(int, int, str)
    errorOccurred = Signal(str)

    def __init__(self, config: AppConfig, paths: Paths, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._config = config
        self._paths = paths
        self._en = PiperCliBackend(self)
        self._hi = PiperCliBackend(self)
        self._fillers = FillerCache()
        self._active_language = SessionLanguage.English
        for worker in (self._en, self._hi):
            worker.wavReady.connect(self._on_worker_wav)
            worker.synthesisFailed.connect(self._on_worker_failed)

    def _on_worker_wav(self, turn_id: int, sentence_index: int, wav_path: str, synth_ms: int) -> None:
        if turn_id >= 0:
            self.firstTtsDone.emit(turn_id, sentence_index)
        self.wavReady.emit(turn_id, sentence_index, wav_path, synth_ms)

    def _on_worker_failed(self, turn_id: int, sentence_index: int, error: str) -> None:
        self.synthesisFailed.emit(turn_id, sentence_index, error)
        if turn_id >= 0:
            self.errorOccurred.emit(error)

    def startSession(self) -> None:
        self._ensure_worker(SessionLanguage.English)
        self._ensure_worker(SessionLanguage.Hindi)
        self._en.startWorker()
        self._hi.startWorker()
        if self._config.ttsFillersEnabled():
            self._fillers.load(self._config, self._paths)
            self._fillers.warmUp(self._en, self._hi)

    def stop(self) -> None:
        self._en.stopWorker()
        self._hi.stopWorker()

    def speakSentence(
        self, turn_id: int, sentence_index: int, text: str, language: SessionLanguage
    ) -> None:
        self._active_language = language
        self._ensure_worker(language)
        worker = self._hi if language == SessionLanguage.Hindi else self._en
        worker.startWorker()
        worker.synthesize(turn_id, sentence_index, text)

    def speakTestPhrase(
        self, language: SessionLanguage, phrase_index: int, text: str
    ) -> None:
        self._ensure_worker(language)
        worker = self._hi if language == SessionLanguage.Hindi else self._en
        worker.startWorker()
        worker.synthesize(-1, phrase_index, text)

    def isBusy(self) -> bool:
        return self._en.hasPendingJobs() or self._hi.hasPendingJobs()

    def fillers(self) -> FillerCache:
        return self._fillers

    def lastColdSynthMs(self, language: SessionLanguage) -> int:
        worker = self._hi if language == SessionLanguage.Hindi else self._en
        return worker.lastColdSynthMs()

    def lastWarmSynthMs(self, language: SessionLanguage) -> int:
        worker = self._hi if language == SessionLanguage.Hindi else self._en
        return worker.lastWarmSynthMs()

    def _voice_for(self, language: SessionLanguage) -> str:
        return (
            self._config.ttsVoiceHi()
            if language == SessionLanguage.Hindi
            else self._config.ttsVoiceEn()
        )

    def _ensure_worker(self, language: SessionLanguage) -> None:
        worker = self._hi if language == SessionLanguage.Hindi else self._en
        if worker.isConfigured():
            return
        voice = self._voice_for(language)
        onnx = str(self._paths.piperVoiceOnnx(voice))
        scale = (
            self._config.ttsLengthScaleHi()
            if language == SessionLanguage.Hindi
            else self._config.ttsLengthScaleDefault()
        )
        worker.configure(str(self._paths.piperExecutable() or ""), onnx, scale)
