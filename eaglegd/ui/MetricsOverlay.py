from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QShowEvent
from PySide6.QtWidgets import QDialog, QLabel, QVBoxLayout, QWidget

from eaglegd.core.Metrics import Metrics


class MetricsOverlay(QDialog):
    def __init__(self, metrics: Metrics, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._metrics = metrics
        self.setWindowTitle("Eagle GD — Metrics (F12)")
        self.resize(480, 320)
        layout = QVBoxLayout(self)
        self._body = QLabel(self)
        self._body.setWordWrap(True)
        self._body.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        layout.addWidget(self._body)
        self.refresh()

    def showEvent(self, event: QShowEvent) -> None:
        self.refresh()
        super().showEvent(event)

    def refresh(self) -> None:
        t = self._metrics.lastTurn()
        cache = "n/a"
        prompt_new = t.llm.prompt_eval_new_count()
        if prompt_new > 0 and t.estimatedNewPromptTokens > 0:
            ratio = float(prompt_new) / float(t.estimatedNewPromptTokens)
            cache = "likely HIT" if ratio <= 1.8 else "likely MISS"
        if t.llm.prompt_eval_cached_count >= 0:
            prompt_line = (
                f"prompt_eval: {prompt_new} new + {t.llm.prompt_eval_cached_count} "
                f"cached = {t.llm.prompt_eval_count} total "
                f"(est. new ~{t.estimatedNewPromptTokens}) → {cache}"
            )
        else:
            prompt_line = (
                f"prompt_eval_count: {t.llm.prompt_eval_count} "
                f"(est. new ~{t.estimatedNewPromptTokens}) → {cache}"
            )
        ts = t.timestamps
        model_label = t.model if t.model else "—"
        self._body.setText(
            f"Model: {model_label}\n"
            f"Last turn #{t.turnId}\n"
            f"T0→T1 (first audio): {t.t0ToT1Ms} ms | "
            f"rolling p50 {self._metrics.rollingP50T0ToT1():.0f} / "
            f"p95 {self._metrics.rollingP95T0ToT1():.0f}\n"
            f"Timestamps (ms from T0): prompt {ts.promptBuiltMs} | sent {ts.requestSentMs} | "
            f"1st token {ts.firstTokenMs} | 1st sentence {ts.firstSentenceMs} | "
            f"1st TTS {ts.firstTtsDoneMs} | LLM done {ts.llmDoneMs} | "
            f"speech done {ts.speechDoneMs}\n"
            f"First token (LLM): {t.firstTokenMs} ms\n"
            f"TTS synth (turn): first {t.ttsFirstSynthMs} ms | warm {t.ttsWarmSynthMs} ms\n"
            f"TTS worker: cold {t.ttsWorkerColdSynthMs} ms | "
            f"warm {t.ttsWorkerWarmSynthMs} ms (persistent piper)\n"
            f"{prompt_line}\n"
            f"eval_count: {t.llm.eval_count} | load: {t.llm.load_ns // 1_000_000} ms"
        )
