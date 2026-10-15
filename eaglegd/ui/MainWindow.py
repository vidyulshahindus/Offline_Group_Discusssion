from __future__ import annotations

from typing import TYPE_CHECKING

from PySide6.QtCore import QElapsedTimer, Qt
from PySide6.QtGui import QAction, QKeySequence, QShortcut
from PySide6.QtWidgets import QMainWindow, QWidget

from eaglegd.core.Metrics import TurnMetrics
from eaglegd.core.TurnSpeechController import TurnSpeechController
from eaglegd.curriculum.curriculum_controller import CurriculumController
from eaglegd.curriculum.retriever import Retriever
from eaglegd.curriculum.unit_context_builder import UnitContextBuilder
from eaglegd.llm.ILlmBackend import LlmMessage, LlmMetrics, LlmRequest
from eaglegd.llm.PromptTypes import EagleMode, SessionLanguage, TurnInputs, UnitContext
from eaglegd.stt.stt_engine import SttEngine
from eaglegd.tts.piper_speaker import PiperSpeaker
from eaglegd.ui.ChatPanel import ChatPanel
from eaglegd.ui.MetricsOverlay import MetricsOverlay
from eaglegd.ui.ReadinessPanel import ReadinessPanel
from eaglegd.ui.chat_bubble import ChatRole

if TYPE_CHECKING:
    from eaglegd.app.AppConfig import AppConfig
    from eaglegd.app.LlmServices import LlmServices
    from eaglegd.app.Paths import Paths
    from eaglegd.app.ServiceSupervisor import ServiceSupervisor


def _metric_int(metrics: LlmMetrics, *names: str, default: int = -1) -> int:
    for name in names:
        if hasattr(metrics, name):
            value = getattr(metrics, name)
            if value is not None:
                return int(value)
    return default


def _metric_str(metrics: LlmMetrics, *names: str, default: str = "") -> str:
    for name in names:
        if hasattr(metrics, name):
            value = getattr(metrics, name)
            if value is not None:
                return str(value)
    return default


class MainWindow(QMainWindow):
    def __init__(
        self,
        config: AppConfig,
        paths: Paths,
        supervisor: ServiceSupervisor,
        llm: LlmServices,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._config = config
        self._paths = paths
        self._supervisor = supervisor
        self._llm = llm

        self._chat: ChatPanel | None = None
        self._metrics_overlay: MetricsOverlay | None = None
        self._stt: SttEngine | None = None
        self._piper: PiperSpeaker | None = None
        self._turn_speech: TurnSpeechController | None = None
        self._curriculum: CurriculumController | None = None

        self._speech_prefix = ""
        self._last_user_turn_message = ""
        self._turn_counter = 0
        self._turn_timer = QElapsedTimer()
        self._unit_context = UnitContext()
        self._pending_llm_metrics = LlmMetrics()
        self._pending_turn_open = False
        self._retry_send_after_warm = False
        self._retriever = Retriever()

        self._turn_speech = TurnSpeechController(config, paths, self)
        self._turn_speech.firstAudio.connect(self._on_turn_first_audio)
        self._turn_speech.wordBudgetReached.connect(self._on_word_budget_reached)
        self._turn_speech.ttsError.connect(self._on_turn_speech_error)
        self._turn_speech.turnIdle.connect(self._on_turn_idle)
        self._turn_speech.audioPlaybackChanged.connect(self._on_audio_playback_changed)
        self._turn_speech.testToneFinished.connect(self._on_audio_playback_changed)
        self._turn_speech.speechDone.connect(lambda _tid: self._on_turn_idle(_tid))

        self._stt = SttEngine(config, paths, supervisor, self)
        self._piper = PiperSpeaker(self)
        self._wire_paths(paths)

        self._chat = ChatPanel(self)
        self.setCentralWidget(self._chat)
        self.setWindowTitle("Eagle GD")
        self.resize(960, 720)
        self.setMinimumSize(640, 480)

        self._populate_models([config.llmModel(), config.llmFallbackModel()])

        readiness_action = QAction("Readiness…", self)
        readiness_action.setShortcut(QKeySequence("Ctrl+Shift+R"))
        readiness_action.triggered.connect(self._show_readiness_panel)
        self.menuBar().addAction(readiness_action)

        metrics_action = QAction("Metrics", self)
        metrics_action.setShortcut(QKeySequence(Qt.Key.Key_F12))
        metrics_action.triggered.connect(self._show_metrics_overlay)
        self.menuBar().addAction(metrics_action)

        stop_shortcut = QShortcut(QKeySequence(Qt.Key.Key_Escape), self)
        stop_shortcut.activated.connect(self._on_stop_clicked)

        chat = self._chat
        chat.sendRequested.connect(self._on_send_clicked)
        chat.newChatRequested.connect(self._on_new_chat_clicked)
        chat.stopRequested.connect(self._on_stop_clicked)
        chat.micIndexChanged.connect(self._on_mic_selection_changed)
        chat.speakerIndexChanged.connect(self._on_speaker_selection_changed)
        chat.refreshDevicesRequested.connect(self._on_refresh_mics_clicked)
        chat.recordToggled.connect(self._on_record_clicked)
        chat.listenToggleRequested.connect(self._on_listen_toggle)
        chat.unitSelectionChanged.connect(self._on_unit_combo_changed)
        chat.topicSelectionChanged.connect(self._on_topic_combo_changed)
        chat.periodSelectionChanged.connect(self._on_period_combo_changed)
        chat.languageCombo().currentIndexChanged.connect(
            lambda _index: self._refresh_context_line()
        )
        chat.modeCombo().currentIndexChanged.connect(
            lambda _index: self._refresh_context_line()
        )
        chat.wordLimitCombo().currentTextChanged.connect(
            lambda _text: self._refresh_context_line()
        )

        if llm.prompts:
            self._curriculum = CurriculumController(paths, llm.prompts, self)
            self._curriculum.context_changed.connect(
                self._on_curriculum_context_changed
            )
            self._curriculum.warmup_requested.connect(
                self._on_curriculum_warmup_requested
            )
            self._populate_curriculum_ui()
            self._curriculum.restore_settings()
            self._populate_curriculum_ui()
            self._unit_context = self._curriculum.current_context
            self._rebuild_retriever_index()
            self._refresh_context_line()
        else:
            self._unit_context = UnitContextBuilder.none()

        backend = llm.backend
        if backend:
            backend.token.connect(self._on_llm_token)
            backend.finished.connect(self._on_llm_finished)
            backend.failed.connect(self._on_llm_failed)
            backend.probed.connect(self._on_models_probed)
            if hasattr(backend, "warmUpFinished"):
                backend.warmUpFinished.connect(self._on_warm_up_finished)

        self._stt.listening_changed.connect(self._on_recording_changed)
        self._stt.input_level.connect(chat.setMicInputLevel)
        self._stt.partial_text.connect(self._on_partial_transcription)
        self._stt.final_text.connect(self._on_final_transcription)
        self._stt.error_occurred.connect(self._on_speech_error)
        self._stt.status_changed.connect(self._on_speech_status)

        self._piper.speakingChanged.connect(self._on_piper_speaking_changed)
        self._piper.statusChanged.connect(self._on_piper_status)
        self._piper.errorOccurred.connect(self._on_piper_error)

        self._refresh_microphones()
        self._refresh_speakers()

        chat.addMessageBubble(
            ChatRole.Assistant,
            "Hi! I am Eagle. Choose Doubt mode, pick a model, and ask a question. "
            "Make sure Ollama is running on this machine.",
        )

    def __del__(self) -> None:
        if self._curriculum is not None:
            try:
                self._curriculum.save_settings()
            except Exception:
                pass

    def closeEvent(self, event) -> None:  # noqa: N802 (Qt API)
        self.shutdown()
        super().closeEvent(event)

    def shutdown(self) -> None:
        """Orderly teardown so Piper/whisper QProcess objects are not destroyed live."""
        if getattr(self, "_shutting_down", False):
            return
        self._shutting_down = True
        try:
            if self._curriculum is not None:
                self._curriculum.save_settings()
        except Exception:
            pass
        backend = self._llm.backend
        if backend is not None:
            try:
                backend.abort()
            except Exception:
                pass
        if self._stt is not None:
            try:
                self._stt.stop_listening()
            except Exception:
                pass
        if self._turn_speech is not None:
            try:
                self._turn_speech.shutdown()
            except Exception:
                pass
        if self._piper is not None:
            try:
                self._piper.stop()
            except Exception:
                pass
        try:
            self._supervisor.stopWhisperServer()
        except Exception:
            pass

    def turnSpeech(self) -> TurnSpeechController:
        assert self._turn_speech is not None
        return self._turn_speech

    def systemPromptForStartup(self) -> str:
        if not self._llm.prompts:
            return ""
        if self._curriculum is not None:
            return self._llm.prompts.system_prompt(self._curriculum.current_context)
        return self._llm.prompts.system_prompt(self._unit_context)

    def _current_language(self) -> SessionLanguage:
        chat = self._chat
        assert chat is not None
        code = chat.languageCombo().currentData()
        return (
            SessionLanguage.HINDI
            if code == "hi"
            else SessionLanguage.ENGLISH
        )

    @staticmethod
    def _current_mode() -> EagleMode:
        return EagleMode.DOUBT

    def _wire_paths(self, paths: Paths) -> None:
        assert self._piper is not None
        exe = paths.piperExecutable()
        voice = paths.piperVoiceOnnx(self._config.ttsVoiceEn())
        self._piper.setPiperPaths(
            str(exe) if exe else "",
            str(voice),
        )

    def _populate_models(self, models: list[str]) -> None:
        chat = self._chat
        assert chat is not None
        combo = chat.modelCombo()
        combo.clear()
        seen: list[str] = []
        for model in models:
            if not model or model in seen:
                continue
            seen.append(model)
            combo.addItem(model)
        if combo.count() == 0:
            combo.addItem(self._config.llmModel())
        combo.setCurrentIndex(0)

    def _on_models_probed(self, ok: bool, version: str, models: list) -> None:
        if not ok:
            return
        merged = [self._config.llmModel(), *models, self._config.llmFallbackModel()]
        self._populate_models(merged)
        if version and self._chat is not None:
            self._chat.statusLabel().setText(f"Ollama {version}")

    def _show_readiness_panel(self) -> None:
        panel = ReadinessPanel(self._supervisor, self._turn_speech, self)
        panel.setAttribute(Qt.WidgetAttribute.WA_DeleteOnClose)
        panel.open()

    def _show_metrics_overlay(self) -> None:
        if self._llm.metrics is None:
            return
        if self._metrics_overlay is None:
            self._metrics_overlay = MetricsOverlay(self._llm.metrics, self)
        self._metrics_overlay.refresh()
        self._metrics_overlay.show()
        self._metrics_overlay.raise_()
        self._metrics_overlay.activateWindow()

    def _on_send_clicked(self) -> None:
        if not self._llm.backend:
            return
        if self._curriculum and self._curriculum.is_warming():
            self._retry_send_after_warm = True
            assert self._chat is not None
            self._chat.statusLabel().setText(
                "Waiting — Eagle is preparing this unit…"
            )
            return
        self._send_turn_if_ready()

    def _send_turn_if_ready(self) -> bool:
        backend = self._llm.backend
        chat = self._chat
        prompts = self._llm.prompts
        assert backend is not None and chat is not None and prompts is not None
        assert self._turn_speech is not None
        assert self._llm.estimator is not None and self._llm.history is not None

        if backend.isBusy():
            chat.statusLabel().setText(
                "Eagle is still loading the model — try again in a moment."
            )
            return False

        text = chat.input().toPlainText().strip()
        if not text:
            return False

        chat.input().clear()
        chat.addMessageBubble(ChatRole.User, text)

        inputs = TurnInputs(
            mode=self._current_mode(),
            language=self._current_language(),
            word_limit=chat.currentWordLimit(),
            question=text,
            notes=self._retriever.notes_for_turn(text),
        )

        self._last_user_turn_message = prompts.build_user_turn(
            inputs, self._unit_context
        )
        messages = prompts.build_messages(
            inputs, self._unit_context, self._llm.history.messages
        )

        turn_id = self._turn_counter + 1
        self._turn_counter = turn_id
        self._turn_speech.beginTurn(
            turn_id,
            self._current_mode(),
            self._current_language(),
            inputs.word_limit,
        )
        self._turn_speech.markPromptBuilt()
        self._pending_turn_open = True

        req = LlmRequest(
            model=chat.modelCombo().currentText(),
            messages=messages,
            turn_id=turn_id,
            purpose="turn",
            temperature=self._config.llmTemperatureFor("doubt"),
            top_p=self._config.llmTopP(),
            top_k=self._config.llmTopK(),
            repeat_penalty=self._config.llmRepeatPenalty(),
            num_predict=self._llm.estimator.num_predict_for_word_limit(
                inputs.word_limit, inputs.language
            ),
        )

        chat.setActiveAssistantBubble(
            chat.addMessageBubble(ChatRole.Assistant, "")
        )
        chat.clearStreamBuffer()
        chat.statusLabel().setText("Generating…")
        self._refresh_turn_chrome()
        self._turn_timer.start()
        self._turn_speech.markRequestSent()
        backend.start(req)
        return True

    def _on_new_chat_clicked(self) -> None:
        backend = self._llm.backend
        chat = self._chat
        assert chat is not None and self._turn_speech is not None
        assert self._piper is not None and self._llm.history is not None
        if backend and backend.isBusy():
            backend.abort()
        self._turn_speech.stop()
        self._piper.stop()
        self._llm.history.reset()
        chat.clearConversation()
        chat.addMessageBubble(
            ChatRole.Assistant, "New chat started. What would you like to ask?"
        )
        chat.statusLabel().setText("Ready")

    def _on_stop_clicked(self) -> None:
        backend = self._llm.backend
        chat = self._chat
        assert chat is not None and self._turn_speech is not None
        assert self._piper is not None
        if backend:
            backend.abort()
        self._turn_speech.stop()
        self._piper.stop()
        self._refresh_turn_chrome()
        chat.statusLabel().setText("Stopped")

    def _on_llm_token(self, turn_id: int, text: str) -> None:
        del turn_id
        assert self._chat is not None and self._turn_speech is not None
        self._chat.appendAssistantStream(text)
        self._turn_speech.onLlmToken(text)

    def _on_llm_finished(
        self, turn_id: int, full_text: str, metrics: LlmMetrics
    ) -> None:
        del full_text
        assert self._chat is not None and self._turn_speech is not None
        assert self._llm.history is not None

        done_reason = _metric_str(metrics, "done_reason", "doneReason")
        hit_token_limit = done_reason == "length"
        self._turn_speech.onLlmFinished(hit_token_limit)
        spoken = self._turn_speech.spokenAssistantText()
        bubble = self._chat.activeAssistantBubble()
        if bubble is not None:
            bubble.setText(spoken)

        history_before = list(self._llm.history.messages)
        self._llm.history.append_turn(
            self._last_user_turn_message, spoken, self._current_language()
        )
        self._pending_llm_metrics = metrics
        self._finalize_turn_metrics(turn_id, metrics, history_before)

        self._chat.setActiveAssistantBubble(None)
        self._chat.clearStreamBuffer()
        self._refresh_turn_chrome()
        if self._turn_speech.isTurnActive() or self._turn_speech.isSpeechBusy():
            self._chat.statusLabel().setText("Speaking…")
        else:
            self._chat.statusLabel().setText("Ready")
        self._chat.scrollToBottom()

    def _on_turn_idle(self, turn_id: int) -> None:
        del turn_id
        assert self._chat is not None and self._stt is not None
        self._refresh_turn_chrome()
        if not self._stt.is_busy():
            self._chat.statusLabel().setText("Ready")

    def _on_turn_first_audio(self, turn_id: int, t1_ms: int) -> None:
        del turn_id
        assert self._chat is not None
        if t1_ms > 10000:
            self._chat.statusLabel().setStyleSheet("color: #c9a227;")
        if self._metrics_overlay is not None and self._metrics_overlay.isVisible():
            self._metrics_overlay.refresh()

    def _on_word_budget_reached(self, turn_id: int) -> None:
        del turn_id
        backend = self._llm.backend
        if backend and backend.isBusy():
            backend.abort()

    def _on_turn_speech_error(self, message: str) -> None:
        assert self._chat is not None
        self._chat.statusLabel().setText(f"Speech: {message[:120]}")
        self._refresh_turn_chrome()

    def _finalize_turn_metrics(
        self,
        turn_id: int,
        metrics: LlmMetrics,
        history_before_append: list[LlmMessage],
    ) -> None:
        assert self._chat is not None and self._llm.estimator is not None
        assert self._llm.metrics is not None and self._turn_speech is not None

        estimated_new = self._llm.estimator.estimate_tokens(
            self._last_user_turn_message, self._current_language()
        )
        if (
            history_before_append
            and history_before_append[-1].role == "assistant"
        ):
            estimated_new += self._llm.estimator.estimate_tokens(
                history_before_append[-1].content, self._current_language()
            )

        prompt_new = metrics.prompt_eval_new_count()
        tm = TurnMetrics()
        tm.turnId = turn_id
        tm.model = self._chat.modelCombo().currentText()
        tm.firstTokenMs = _metric_int(metrics, "ms_to_first_token", "msToFirstToken")
        tm.llmDoneMs = self._turn_timer.elapsed()
        tm.llm = metrics
        tm.estimatedNewPromptTokens = estimated_new
        tm.cacheLikelyHit = (
            prompt_new > 0
            and estimated_new > 0
            and prompt_new <= estimated_new * 2
        )
        tm.timestamps = self._turn_speech.timestamps()
        if tm.timestamps.firstTokenMs < 0:
            tm.timestamps.firstTokenMs = tm.firstTokenMs
        tm.t0ToT1Ms = tm.timestamps.firstAudioMs
        tm.ttsFirstSynthMs = self._turn_speech.turnFirstSynthMs()
        tm.ttsWarmSynthMs = self._turn_speech.turnWarmSynthMs()
        tm.ttsWorkerColdSynthMs = self._turn_speech.workerColdSynthMs()
        tm.ttsWorkerWarmSynthMs = self._turn_speech.workerWarmSynthMs()
        self._llm.metrics.recordTurn(tm)
        self._pending_turn_open = False

        if self._metrics_overlay is not None and self._metrics_overlay.isVisible():
            self._metrics_overlay.refresh()

    def _on_llm_failed(self, turn_id: int, error: str, recoverable: bool) -> None:
        del turn_id, recoverable
        assert self._turn_speech is not None
        self._turn_speech.stop()
        self._on_client_error(error)

    def _on_client_error(self, message: str) -> None:
        assert self._chat is not None
        bubble = self._chat.activeAssistantBubble()
        if bubble is not None:
            bubble.appendText(f"\n\n[Error: {message}]")
        else:
            self._chat.addMessageBubble(
                ChatRole.Assistant,
                f"Could not reach Ollama. Is it running?\n\n{message}",
            )
        self._chat.setActiveAssistantBubble(None)
        self._chat.clearStreamBuffer()
        self._refresh_turn_chrome()
        self._chat.statusLabel().setText("Error")

    def _sync_mic_playback_mute(self) -> None:
        assert self._stt is not None and self._piper is not None
        playback_busy = (
            self._turn_speech is not None
            and self._turn_speech.isAudioPlaybackBusy()
        ) or self._piper.isBusy()
        self._stt.set_playback_active(
            playback_busy and self._config.audioMuteMicWhileSpeaking()
        )

    def _on_audio_playback_changed(self, *_args) -> None:
        self._sync_mic_playback_mute()
        self._refresh_turn_chrome()
        if (
            self._turn_speech is not None
            and not self._turn_speech.isTurnActive()
            and not self._turn_speech.isSpeechBusy()
            and self._chat is not None
            and not (self._stt and self._stt.is_busy())
        ):
            self._chat.statusLabel().setText("Ready")

    def _refresh_turn_chrome(self) -> None:
        chat = self._chat
        backend = self._llm.backend
        assert chat is not None and self._stt is not None and self._piper is not None

        llm_busy = backend.isBusy() if backend else False
        turn_active = (
            self._turn_speech.isTurnActive() if self._turn_speech else False
        )
        speech_busy = (
            self._turn_speech.isSpeechBusy() if self._turn_speech else False
        )
        piper_legacy_busy = self._piper.isBusy()
        locked = llm_busy or turn_active
        self._sync_mic_playback_mute()

        chat.sendButton().setDisabled(locked)
        chat.modelCombo().setDisabled(locked)
        chat.modeCombo().setDisabled(locked)
        chat.languageCombo().setDisabled(locked)
        chat.wordLimitCombo().setDisabled(locked)
        chat.recordButton().setDisabled(locked or self._stt.is_busy())
        chat.micCombo().setDisabled(locked or self._stt.is_listening())
        chat.speakerCombo().setDisabled(locked or piper_legacy_busy)
        chat.refreshMicsButton().setDisabled(
            locked or self._stt.is_listening()
        )
        chat.stopButton().setText("Stop generating")
        chat.stopButton().setVisible(
            llm_busy or turn_active or speech_busy or piper_legacy_busy
        )
        if not self._stt.is_busy() and not turn_active and not speech_busy:
            if llm_busy:
                chat.statusLabel().setText("Generating…")
        elif turn_active and not llm_busy and not self._stt.is_busy():
            chat.statusLabel().setText("Speaking…")

    def _refresh_microphones(self) -> None:
        chat = self._chat
        assert chat is not None and self._stt is not None
        backend = self._llm.backend

        previous_id = chat.micCombo().currentData()
        combo = chat.micCombo()
        combo.blockSignals(True)
        combo.clear()

        devices = self._stt.microphones()
        for device in devices:
            combo.addItem(device.description(), device.id())

        index = combo.findData(previous_id)
        if index < 0:
            index = 0
        llm_busy = backend.isBusy() if backend else False
        if combo.count() == 0:
            combo.addItem("No microphone found")
            chat.recordButton().setEnabled(False)
        else:
            combo.setCurrentIndex(index)
            chat.recordButton().setEnabled(not llm_busy)
            self._on_mic_selection_changed(index)
        combo.blockSignals(False)

    def _refresh_speakers(self) -> None:
        chat = self._chat
        assert chat is not None and self._piper is not None

        previous_id = chat.speakerCombo().currentData()
        combo = chat.speakerCombo()
        combo.blockSignals(True)
        combo.clear()

        for device in self._piper.speakers():
            combo.addItem(device.description(), device.id())

        index = combo.findData(previous_id)
        if index < 0:
            index = 0
        if combo.count() == 0:
            combo.addItem("No speaker found")
        else:
            combo.setCurrentIndex(index)
            self._on_speaker_selection_changed(index)
        combo.blockSignals(False)

    def _on_mic_selection_changed(self, index: int) -> None:
        if index < 0:
            return
        chat = self._chat
        assert chat is not None and self._stt is not None
        device_id = chat.micCombo().itemData(index)
        for device in self._stt.microphones():
            if device.id() == device_id:
                self._stt.set_microphone(device)
                return

    def _on_speaker_selection_changed(self, index: int) -> None:
        if index < 0:
            return
        chat = self._chat
        assert chat is not None and self._piper is not None
        device_id = chat.speakerCombo().itemData(index)
        for device in self._piper.speakers():
            if device.id() == device_id:
                self._piper.setSpeaker(device)
                if self._turn_speech is not None:
                    self._turn_speech.setOutputDevice(device)
                return

    def _on_refresh_mics_clicked(self) -> None:
        self._refresh_microphones()
        self._refresh_speakers()
        assert self._chat is not None
        self._chat.statusLabel().setText("Microphones and speakers refreshed")

    def _on_record_clicked(self) -> None:
        self._on_listen_toggle()

    def _on_listen_toggle(self) -> None:
        assert self._stt is not None and self._chat is not None
        backend = self._llm.backend

        if self._stt.is_listening():
            self._stt.stop_listening()
            return

        llm_busy = backend.isBusy() if backend else False
        eagle_speaking = self._turn_speech is not None and (
            self._turn_speech.isTurnActive() or self._turn_speech.isSpeechBusy()
        )
        if llm_busy or eagle_speaking:
            self._on_stop_clicked()

        self._on_mic_selection_changed(self._chat.micCombo().currentIndex())
        self._stt.set_locked_reply_language(self._current_language())
        self._stt.set_bias_terms(self._config.sttBiasTerms())
        self._speech_prefix = self._chat.input().toPlainText().strip()
        self._stt.start_listening()

    def _show_speech_text(self, text: str) -> None:
        assert self._chat is not None
        combined = (
            text
            if not self._speech_prefix
            else f"{self._speech_prefix} {text}"
        )
        editor = self._chat.input()
        editor.setPlainText(combined)
        editor.moveCursor(editor.textCursor().MoveOperation.End)

    def _on_partial_transcription(self, text: str) -> None:
        self._show_speech_text(text)

    def _on_final_transcription(self, text: str, lang: SessionLanguage) -> None:
        self._show_speech_text(text)
        assert self._chat is not None
        self._chat.input().setFocus()
        self._chat.statusLabel().setText("Ready")
        code = "hi" if lang == SessionLanguage.HINDI else "en"
        idx = self._chat.languageCombo().findData(code)
        if idx >= 0:
            self._chat.languageCombo().setCurrentIndex(idx)

        backend = self._llm.backend
        llm_busy = backend.isBusy() if backend else False
        self._chat.recordButton().setEnabled(not llm_busy)
        self._chat.sendButton().setEnabled(not llm_busy)

    def _on_recording_changed(self, recording: bool) -> None:
        assert self._chat is not None and self._stt is not None
        backend = self._llm.backend

        self._chat.setMicLevelVisible(recording)
        self._chat.recordButton().setText(
            "Stop listening" if recording else "Mic"
        )
        self._chat.recordButton().setProperty("recording", recording)
        style = self._chat.recordButton().style()
        if style is not None:
            style.unpolish(self._chat.recordButton())
            style.polish(self._chat.recordButton())

        llm_busy = backend.isBusy() if backend else False
        self._chat.micCombo().setDisabled(recording or llm_busy)
        self._chat.refreshMicsButton().setDisabled(recording or llm_busy)
        self._chat.sendButton().setDisabled(recording or llm_busy)

    def _on_speech_error(self, message: str) -> None:
        assert self._chat is not None and self._stt is not None
        backend = self._llm.backend
        self._chat.statusLabel().setText("Speech error")
        self._chat.addMessageBubble(ChatRole.Assistant, message)
        llm_busy = backend.isBusy() if backend else False
        self._chat.recordButton().setEnabled(not llm_busy)
        self._chat.sendButton().setEnabled(
            not llm_busy and not self._stt.is_listening()
        )

    def _on_piper_speaking_changed(self, speaking: bool) -> None:
        assert self._chat is not None
        self._chat.stopButton().setText(
            "Stop speaking" if speaking else "Stop generating"
        )
        self._refresh_turn_chrome()

    def _on_piper_status(self, status: str) -> None:
        assert self._chat is not None and self._stt is not None
        backend = self._llm.backend
        llm_busy = backend.isBusy() if backend else False
        if not llm_busy and not self._stt.is_busy():
            self._chat.statusLabel().setText(status)

    def _on_piper_error(self, message: str) -> None:
        assert self._chat is not None
        self._chat.statusLabel().setText("Speech error")
        self._chat.addMessageBubble(ChatRole.Assistant, message)

    def _on_speech_status(self, status: str) -> None:
        assert self._chat is not None and self._stt is not None
        backend = self._llm.backend
        self._chat.statusLabel().setText(status)
        llm_busy = backend.isBusy() if backend else False
        locked = (
            status == "Transcribing…"
            or self._stt.is_listening()
            or llm_busy
        )
        self._chat.recordButton().setDisabled(
            llm_busy or status == "Transcribing…"
        )
        self._chat.sendButton().setDisabled(locked)

    def _populate_curriculum_ui(self) -> None:
        if not self._curriculum or not self._chat:
            return
        unit_combo = self._chat.unitCombo()
        topic_combo = self._chat.topicCombo()
        period_combo = self._chat.periodCombo()

        unit_combo.blockSignals(True)
        topic_combo.blockSignals(True)
        period_combo.blockSignals(True)

        unit_combo.clear()
        unit_combo.addItem("No unit", "")
        for entry in self._curriculum.library.entries():
            unit_combo.addItem(entry.display_label, entry.pack.id())

        period_combo.clear()
        for period in self._curriculum.clm.periods():
            period_combo.addItem(
                f"P{period.period} {period.name}", period.period
            )
        period_idx = period_combo.findData(self._curriculum.period)
        if period_idx >= 0:
            period_combo.setCurrentIndex(period_idx)

        if self._curriculum.has_unit_selected():
            unit_idx = unit_combo.findData(self._curriculum.selected_pack_id)
            if unit_idx >= 0:
                unit_combo.setCurrentIndex(unit_idx)
                self._refresh_topic_combo_for_unit(
                    self._curriculum.selected_pack_id
                )
                topic_idx = topic_combo.findData(
                    self._curriculum.selected_topic_index
                )
                if topic_idx >= 0:
                    topic_combo.setCurrentIndex(topic_idx)
        else:
            unit_combo.setCurrentIndex(0)
            topic_combo.clear()
            topic_combo.setEnabled(False)

        unit_combo.blockSignals(False)
        topic_combo.blockSignals(False)
        period_combo.blockSignals(False)

    def _refresh_topic_combo_for_unit(self, pack_id: str) -> None:
        assert self._curriculum is not None and self._chat is not None
        topic_combo = self._chat.topicCombo()
        topic_combo.blockSignals(True)
        topic_combo.clear()
        pack = self._curriculum.library.pack_by_id(pack_id)
        for i in range(pack.topic_count()):
            topic_combo.addItem(pack.topic_title(i), i)
        topic_combo.setEnabled(bool(pack.id()))
        topic_combo.blockSignals(False)

    def _refresh_context_line(self) -> None:
        if not self._curriculum or not self._chat:
            return
        mode = self._chat.modeCombo().currentText()
        lang = self._chat.languageCombo().currentText()
        limit = self._chat.wordLimitCombo().currentText()
        self._chat.contextLabel().setText(
            self._curriculum.context_line(mode, lang, limit)
        )

    def _reset_chat_for_new_unit(self, unit_title: str) -> None:
        assert self._chat is not None and self._turn_speech is not None
        assert self._piper is not None and self._llm.history is not None
        backend = self._llm.backend

        self._turn_speech.stop()
        self._piper.stop()
        if backend and backend.isStreamInFlight():
            backend.abortActiveTurn()
        self._llm.history.reset()
        self._chat.clearConversation()
        self._chat.addMessageBubble(
            ChatRole.Assistant, f"New unit: {unit_title}"
        )
        self._chat.statusLabel().setText("Ready")

    def _rebuild_retriever_index(self) -> None:
        if not self._curriculum or not self._unit_context.has_unit:
            self._retriever = Retriever()
            return
        pack = self._curriculum.library.pack_by_id(self._unit_context.pack_id)
        if not pack.id():
            self._retriever = Retriever()
            return
        topic_index = 0
        for i in range(pack.topic_count()):
            if pack.topic_id(i) == self._unit_context.topic_id:
                topic_index = i
                break
        self._retriever.build_from_unit(pack, topic_index)

    def _apply_stt_bias_from_unit(self) -> None:
        assert self._stt is not None
        terms = list(self._config.sttBiasTerms())
        if self._unit_context.has_unit and self._curriculum:
            pack = self._curriculum.library.pack_by_id(self._unit_context.pack_id)
            clm = pack.document.get("clm")
            topics = []
            if isinstance(clm, dict):
                raw = clm.get("topics")
                if isinstance(raw, list):
                    topics = raw
            for topic in topics:
                if not isinstance(topic, dict):
                    continue
                if topic.get("id") != self._unit_context.topic_id:
                    continue
                key_terms = topic.get("key_terms")
                if isinstance(key_terms, list):
                    for term in key_terms:
                        terms.append(str(term))
                break
        self._stt.set_bias_terms(terms)

    def _on_unit_combo_changed(self, index: int) -> None:
        if not self._curriculum or index < 0 or not self._chat:
            return
        pack_id = self._chat.unitCombo().itemData(index)
        if not pack_id:
            self._curriculum.select_no_unit()
            self._chat.topicCombo().clear()
            self._chat.topicCombo().setEnabled(False)
            return
        self._refresh_topic_combo_for_unit(str(pack_id))
        self._curriculum.select_pack(str(pack_id), 0)

    def _on_topic_combo_changed(self, index: int) -> None:
        if (
            not self._curriculum
            or index < 0
            or not self._curriculum.has_unit_selected()
            or not self._chat
        ):
            return
        topic_index = int(self._chat.topicCombo().itemData(index))
        self._curriculum.select_pack(
            self._curriculum.selected_pack_id, topic_index
        )

    def _on_period_combo_changed(self, index: int) -> None:
        if not self._curriculum or index < 0 or not self._chat:
            return
        period = int(self._chat.periodCombo().itemData(index))
        self._curriculum.set_period(period)
        self._refresh_context_line()

    def _on_curriculum_context_changed(
        self, context: UnitContext, unit_or_topic_changed: bool
    ) -> None:
        self._unit_context = context
        self._apply_stt_bias_from_unit()
        self._rebuild_retriever_index()
        self._refresh_context_line()
        if unit_or_topic_changed:
            title = context.unit_title if context.has_unit else "General"
            self._reset_chat_for_new_unit(title)

    def _on_curriculum_warmup_requested(
        self, system_prompt: str, unit_title: str
    ) -> None:
        backend = self._llm.backend
        chat = self._chat
        assert chat is not None and self._turn_speech is not None
        assert self._piper is not None
        if not backend:
            return
        if backend.isStreamInFlight() or self._pending_turn_open:
            backend.abortActiveTurn()
            self._turn_speech.stop()
            self._piper.stop()
            self._pending_turn_open = False
            self._refresh_turn_chrome()
        assert self._curriculum is not None
        self._curriculum.set_warming(True)
        chat.statusLabel().setText(f"Preparing Eagle for {unit_title}…")
        backend.scheduleWarmUp(chat.modelCombo().currentText(), system_prompt)

    def _on_warm_up_finished(self, ok: bool) -> None:
        if self._curriculum:
            self._curriculum.set_warming(False)
        assert self._chat is not None
        if not ok:
            self._chat.statusLabel().setText("Warm-up failed — try again")
            return
        if self._retry_send_after_warm:
            self._retry_send_after_warm = False
            self._send_turn_if_ready()
            return
        backend = self._llm.backend
        if not backend or not backend.isBusy():
            self._chat.statusLabel().setText("Ready")
