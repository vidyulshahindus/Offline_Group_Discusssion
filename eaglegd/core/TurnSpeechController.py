from __future__ import annotations

import logging
from typing import TYPE_CHECKING

from PySide6.QtCore import QElapsedTimer, QTimer, Signal, QObject
from PySide6.QtMultimedia import QAudioDevice

from eaglegd.core.SentenceStreamer import SentenceStreamer
from eaglegd.core.TurnTimestamps import TurnTimestamps
from eaglegd.llm.PromptTypes import EagleMode, SessionLanguage
from eaglegd.tts.SpeechQueuePlayer import SpeechQueuePlayer
from eaglegd.tts.TtsEngine import TtsEngine

if TYPE_CHECKING:
    from eaglegd.app.AppConfig import AppConfig
    from eaglegd.app.Paths import Paths

_log = logging.getLogger("eagle.turn")


class TurnSpeechController(QObject):
    wordBudgetReached = Signal(int)
    firstAudio = Signal(int, int)
    speechDone = Signal(int)
    turnIdle = Signal(int)
    ttsError = Signal(str)
    readinessVoiceTestFinished = Signal(bool, str)
    testToneFinished = Signal(bool, str)
    audioPlaybackChanged = Signal()

    def __init__(
        self, config: AppConfig, paths: Paths, parent: QObject | None = None
    ) -> None:
        super().__init__(parent)
        self._config = config
        self._tts = TtsEngine(config, paths, self)
        self._tts.startSession()
        backend = config.audioPlaybackBackend()
        if backend == "sink":
            _log.warning(
                "audio.backend=sink is no longer used; QAudioSink WASAPI path had format/speed "
                "bugs. Using mediaplayer (QMediaPlayer + QAudioOutput)."
            )
        self._audio = SpeechQueuePlayer(self)
        self._audio.setVolume(float(config.ttsVolume()))
        self._audio.setSentenceSilenceMs(int(config.ttsSentenceSilenceS() * 1000.0))
        self._streamer = SentenceStreamer(self)
        self._timestamps = TurnTimestamps()
        self._turn_clock = QElapsedTimer()
        self._filler_timer = QTimer(self)
        self._filler_timer.setSingleShot(True)
        self._filler_timer.timeout.connect(self._on_filler_timeout)
        self._idle_watch = QTimer(self)
        self._idle_watch.setInterval(400)
        self._idle_watch.timeout.connect(self._on_idle_watch)
        self._turn_id = -1
        self._mode = EagleMode.Doubt
        self._language = SessionLanguage.English
        self._speech_enabled = True
        self._turn_active = False
        self._first_tts_marked = False
        self._pending_tts = 0
        self._filler_played_this_turn = False
        self._answer_audio_started = False
        self._last_filler_index = -1
        self._turn_first_synth_ms = -1
        self._turn_warm_synth_ms = -1
        self._readiness_test_pending = 0

        self._streamer.sentenceReady.connect(self._on_sentence)
        self._streamer.wordBudgetReached.connect(self.wordBudgetReached)
        self._tts.wavReady.connect(self._on_tts_wav)
        self._tts.firstTtsDone.connect(self._on_first_tts_done)
        self._tts.errorOccurred.connect(self.ttsError)
        self._tts.synthesisFailed.connect(self._on_synthesis_failed)
        self._audio.firstAudioDelivered.connect(self._on_first_audio_delivered)
        self._audio.speechStarted.connect(lambda _: self.audioPlaybackChanged.emit())
        self._audio.speechFinished.connect(self._on_speech_finished)
        self._audio.playbackError.connect(self._on_playback_error)

    def _on_tts_wav(self, turn_id: int, idx: int, wav_path: str, synth_ms: int) -> None:
        if turn_id == -1:
            self._on_wav(-1, idx, wav_path, synth_ms, False)
            if self._readiness_test_pending > 0:
                self._readiness_test_pending -= 1
                if self._readiness_test_pending == 0:
                    self.readinessVoiceTestFinished.emit(True, "")
            return
        if turn_id < 0:
            return
        self._on_wav(turn_id, idx, wav_path, synth_ms, True)

    def _on_first_tts_done(self, turn_id: int, _idx: int) -> None:
        if not self._first_tts_marked and turn_id == self._turn_id:
            self._first_tts_marked = True
            self._timestamps.firstTtsDoneMs = self._turn_clock.elapsed()

    def _on_first_audio_delivered(self, turn_id: int) -> None:
        if self._timestamps.firstAudioMs < 0 and turn_id == self._turn_id:
            self._timestamps.firstAudioMs = self._turn_clock.elapsed()
            self._filler_timer.stop()
            self.firstAudio.emit(turn_id, self._timestamps.firstAudioMs)

    def _on_speech_finished(self, turn_id: int, cancelled: bool) -> None:
        self.audioPlaybackChanged.emit()
        if turn_id == SpeechQueuePlayer.kTestToneTurnId:
            self.testToneFinished.emit(
                not cancelled, "cancelled" if cancelled else ""
            )
            return
        if not cancelled and turn_id == self._turn_id:
            self._timestamps.speechDoneMs = self._turn_clock.elapsed()
        if self._turn_active and self._timestamps.llmDoneMs >= 0:
            # Audio queue drained — don't stay locked on orphan TTS counters.
            if not self._audio.isPlaybackBusy() and not self._tts.isBusy():
                self._pending_tts = 0
            self._try_complete_turn()

    def _on_playback_error(self, message: str) -> None:
        self.ttsError.emit(message)
        if not self._turn_active:
            self.testToneFinished.emit(False, message)
        if self._turn_active and self._pending_tts > 0:
            self._pending_tts = 0
        self._try_complete_turn()

    def _on_idle_watch(self) -> None:
        if not self._turn_active:
            self._idle_watch.stop()
            return
        if self._timestamps.llmDoneMs < 0:
            return
        if self._tts.isBusy():
            return
        if self._audio.isPlaybackBusy():
            return
        # LLM done, nothing synthesizing or playing — clear sticky counters and unlock UI.
        self._pending_tts = 0
        self._try_complete_turn()
        if not self._turn_active:
            self._idle_watch.stop()

    def setOutputDevice(self, device: QAudioDevice) -> None:
        self._audio.setOutputDevice(device)

    def isSpeechBusy(self) -> bool:
        return self._pending_tts > 0 or self._tts.isBusy() or self._audio.isPlaybackBusy()

    def isAudioPlaybackBusy(self) -> bool:
        return self._audio.isPlaybackBusy()

    def beginTurn(
        self, turn_id: int, mode: EagleMode, language: SessionLanguage, word_limit: int
    ) -> None:
        self._filler_timer.stop()
        self._idle_watch.stop()
        self._audio.stop()
        self._streamer.reset(-1, EagleMode.Doubt, SessionLanguage.English, 75)
        self._pending_tts = 0
        self._turn_active = False

        self._turn_id = turn_id
        self._mode = mode
        self._language = language
        self._turn_active = True
        self._first_tts_marked = False
        self._pending_tts = 0
        self._filler_played_this_turn = False
        self._answer_audio_started = False
        self._turn_first_synth_ms = -1
        self._turn_warm_synth_ms = -1
        self._timestamps = TurnTimestamps()
        self._turn_clock.start()
        self._timestamps.t0Ms = 0
        self._streamer.reset(
            turn_id, mode, language, word_limit, self._config.discussionWordLimitOverflow()
        )
        self._filler_timer.stop()
        if (
            self._config.ttsFillersEnabled()
            and mode != EagleMode.Practice
            and self._speech_enabled
        ):
            self._filler_timer.start(self._config.ttsFillerAfterMs())

    def markPromptBuilt(self) -> None:
        self._timestamps.promptBuiltMs = self._turn_clock.elapsed()

    def markRequestSent(self) -> None:
        self._timestamps.requestSentMs = self._turn_clock.elapsed()

    def onLlmToken(self, token: str) -> None:
        if self._timestamps.firstTokenMs < 0:
            self._timestamps.firstTokenMs = self._turn_clock.elapsed()
        self._streamer.setClockMs(self._turn_clock.elapsed())
        self._streamer.pushToken(token)

    def onLlmFinished(self, hit_token_limit: bool = False) -> None:
        self._timestamps.llmDoneMs = self._turn_clock.elapsed()
        self._streamer.finishStream(hit_token_limit)
        self._try_complete_turn()
        if self._turn_active and not self._idle_watch.isActive():
            self._idle_watch.start()

    def spokenAssistantText(self) -> str:
        return self._streamer.spokenText()

    def stop(self) -> None:
        self._filler_timer.stop()
        self._idle_watch.stop()
        ended_turn = self._turn_id
        self._audio.stop()
        self._streamer.reset(-1, EagleMode.Doubt, SessionLanguage.English, 75)
        self._pending_tts = 0
        if self._turn_active:
            self._turn_active = False
            self.turnIdle.emit(ended_turn)

    def shutdown(self) -> None:
        """Stop playback and terminate Piper workers before QObject teardown."""
        self.stop()
        self._tts.stop()

    def timestamps(self) -> TurnTimestamps:
        return self._timestamps

    def turnFirstSynthMs(self) -> int:
        return self._turn_first_synth_ms

    def turnWarmSynthMs(self) -> int:
        return self._turn_warm_synth_ms

    def workerColdSynthMs(self) -> int:
        return self._tts.lastColdSynthMs(self._language)

    def workerWarmSynthMs(self) -> int:
        return self._tts.lastWarmSynthMs(self._language)

    def isTurnActive(self) -> bool:
        return self._turn_active

    def _on_sentence(self, turn_id: int, idx: int, text: str) -> None:
        if turn_id != self._turn_id:
            return
        if self._timestamps.firstSentenceMs < 0:
            self._timestamps.firstSentenceMs = self._turn_clock.elapsed()
        if not self._speech_enabled:
            self._try_complete_turn()
            return
        self._pending_tts += 1
        self._tts.speakSentence(turn_id, idx, text, self._language)

    def _on_wav(
        self,
        turn_id: int,
        idx: int,
        wav_path: str,
        synth_ms: int,
        counts_for_t1: bool,
    ) -> None:
        if counts_for_t1 and turn_id != self._turn_id:
            return
        if counts_for_t1:
            if self._turn_first_synth_ms < 0:
                self._turn_first_synth_ms = synth_ms
            else:
                self._turn_warm_synth_ms = synth_ms
            self._answer_audio_started = True
            self._filler_timer.stop()
        delete_after = counts_for_t1 or (turn_id < 0 and idx >= 0)
        enqueue_turn = turn_id if turn_id >= 0 else self._turn_id
        self._audio.enqueueWav(
            enqueue_turn, idx, wav_path, counts_for_t1, delete_after
        )
        if counts_for_t1:
            if self._pending_tts > 0:
                self._pending_tts -= 1
            self._try_complete_turn()

    def _on_synthesis_failed(self, turn_id: int, sentence_index: int, error: str) -> None:
        del sentence_index
        if turn_id == -1:
            if self._readiness_test_pending > 0:
                self._readiness_test_pending -= 1
            self.readinessVoiceTestFinished.emit(False, error)
            return
        if turn_id != self._turn_id:
            return
        if self._pending_tts > 0:
            self._pending_tts -= 1
        self.ttsError.emit(error)
        self._try_complete_turn()

    def _try_complete_turn(self) -> None:
        if not self._turn_active or self._turn_id < 0:
            return
        if self._timestamps.llmDoneMs < 0:
            return
        if self._pending_tts > 0:
            return
        self._audio.markInputComplete()
        if self._audio.isPlaybackBusy():
            return
        self._finish_turn()

    def _finish_turn(self) -> None:
        if not self._turn_active:
            return
        self._idle_watch.stop()
        self._turn_active = False
        self.speechDone.emit(self._turn_id)
        self.turnIdle.emit(self._turn_id)

    def _on_filler_timeout(self) -> None:
        self._maybe_play_filler()

    def _maybe_play_filler(self) -> None:
        if (
            self._mode == EagleMode.Practice
            or self._filler_played_this_turn
            or self._answer_audio_started
            or self._timestamps.firstAudioMs >= 0
            or not self._speech_enabled
            or not self._tts.fillers().isReady()
        ):
            return
        idx = self._tts.fillers().pickIndex(self._language, self._last_filler_index)
        if idx < 0:
            return
        wav = self._tts.fillers().wavPathFor(self._language, idx)
        if not wav:
            return
        self._filler_played_this_turn = True
        self._last_filler_index = idx
        self._on_wav(-1, -1, wav, 0, False)

    def playReadinessVoiceTest(self) -> None:
        if self._readiness_test_pending > 0 or self._turn_active:
            self.readinessVoiceTestFinished.emit(
                False, "Finish the current turn before testing voice."
            )
            return
        self._readiness_test_pending = 2
        self._tts.speakTestPhrase(
            SessionLanguage.English, 0, "Hello. I am Eagle, your study assistant."
        )
        self._tts.speakTestPhrase(
            SessionLanguage.Hindi,
            1,
            "नमस्ते। मैं ईगल हूँ, आपकी अध्ययन सहायक।",
        )

    def playTestTone(self) -> None:
        if self._turn_active or self._readiness_test_pending > 0:
            self.testToneFinished.emit(False, "Finish the current turn first.")
            return
        self._audio.playTestTone(1000)
