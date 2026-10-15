from __future__ import annotations

from typing import TYPE_CHECKING

from PySide6.QtCore import Qt, QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (
    QDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from eaglegd.app.Logging import EagleLog
from eaglegd.app.ServiceSupervisor import ServiceHealth, ServiceSupervisor

if TYPE_CHECKING:
    from eaglegd.core.TurnSpeechController import TurnSpeechController


def _health_text(health: ServiceHealth) -> str:
    mapping = {
        ServiceHealth.Ok: "OK",
        ServiceHealth.Warning: "Warning",
        ServiceHealth.Error: "Error",
        ServiceHealth.Checking: "Checking…",
        ServiceHealth.Unknown: "Unknown",
    }
    return mapping.get(health, "Unknown")


class ReadinessPanel(QDialog):
    def __init__(
        self,
        supervisor: ServiceSupervisor,
        turn_speech: TurnSpeechController | None,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._supervisor = supervisor
        self._turn_speech = turn_speech

        self.setWindowTitle("Eagle GD — Readiness")
        self.resize(580, 480)

        root = QVBoxLayout(self)

        self._title = QLabel("System readiness", self)
        self._title.setObjectName("readinessTitle")
        title_font = self._title.font()
        title_font.setPointSize(title_font.pointSize() + 2)
        title_font.setBold(True)
        self._title.setFont(title_font)

        self._log_label = QLabel(self)
        self._log_label.setObjectName("readinessDetail")
        self._log_label.setWordWrap(True)
        self._log_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        self._refresh_log_label()

        scroll = QScrollArea(self)
        scroll.setWidgetResizable(True)
        scroll_host = QWidget(scroll)
        self._rows_layout = QVBoxLayout(scroll_host)
        self._rows_layout.setSpacing(8)
        scroll.setWidget(scroll_host)

        self._test_tone_button = QPushButton("Test tone (440 Hz, 1 s)", self)
        self._test_tone_button.setEnabled(self._turn_speech is not None)
        self._test_tone_button.clicked.connect(self._on_test_tone_clicked)

        self._test_voice_button = QPushButton("Test voice (English + Hindi)", self)
        self._test_voice_button.setEnabled(self._turn_speech is not None)
        self._test_voice_button.clicked.connect(self._on_test_voice_clicked)

        if self._turn_speech is not None:
            self._turn_speech.readinessVoiceTestFinished.connect(
                self._on_voice_test_finished
            )
            self._turn_speech.testToneFinished.connect(self._on_test_tone_finished)

        self._open_log_button = QPushButton("Open log folder", self)
        self._open_log_button.clicked.connect(self._on_open_log_folder_clicked)

        refresh = QPushButton("Refresh checks", self)
        refresh.clicked.connect(self._on_refresh_clicked)

        close = QPushButton("Continue to Eagle GD", self)
        close.clicked.connect(self.accept)

        root.addWidget(self._title)
        root.addWidget(self._log_label)
        root.addWidget(scroll, 1)
        root.addWidget(self._test_tone_button)
        root.addWidget(self._test_voice_button)
        root.addWidget(self._open_log_button)
        root.addWidget(refresh)
        root.addWidget(close)

        self._supervisor.statusesChanged.connect(self._on_statuses_changed)
        self._rebuild_rows()

    def _refresh_log_label(self) -> None:
        path = EagleLog.filePath()
        if not path:
            self._log_label.setText("Log file: (not created)")
        else:
            from PySide6.QtCore import QDir

            self._log_label.setText(
                f"Log file: {QDir.toNativeSeparators(path)}"
            )

    def _on_test_tone_clicked(self) -> None:
        if self._turn_speech is None:
            return
        self._test_tone_button.setEnabled(False)
        self._test_tone_button.setText("Playing test tone…")
        self._turn_speech.playTestTone()

    def _on_test_tone_finished(self, ok: bool, detail: str) -> None:
        self._test_tone_button.setEnabled(self._turn_speech is not None)
        if ok:
            self._test_tone_button.setText("Test tone (440 Hz, 1 s) — played")
        else:
            self._test_tone_button.setText(
                f"Test tone failed — {detail[:80]}"
            )

    def _on_open_log_folder_clicked(self) -> None:
        from pathlib import Path

        self._refresh_log_label()
        directory = EagleLog.directory()
        if not directory and EagleLog.filePath():
            directory = str(Path(EagleLog.filePath()).parent)
        if not directory:
            self._log_label.setText("Log file: (not created)")
            return
        Path(directory).mkdir(parents=True, exist_ok=True)
        QDesktopServices.openUrl(QUrl.fromLocalFile(directory))

    def _on_test_voice_clicked(self) -> None:
        if self._turn_speech is None:
            return
        self._test_voice_button.setEnabled(False)
        self._test_voice_button.setText("Testing voice…")
        self._turn_speech.playReadinessVoiceTest()

    def _on_voice_test_finished(self, ok: bool, detail: str) -> None:
        self._test_voice_button.setEnabled(self._turn_speech is not None)
        if ok:
            self._test_voice_button.setText(
                "Test voice (English + Hindi) — OK"
            )
        else:
            self._test_voice_button.setText(
                f"Test voice failed — {detail[:80]}"
            )

    def _on_refresh_clicked(self) -> None:
        self._supervisor.refreshAll()
        self._refresh_log_label()

    def _on_statuses_changed(self) -> None:
        self._rebuild_rows()

    def _rebuild_rows(self) -> None:
        while self._rows_layout.count() > 0:
            item = self._rows_layout.takeAt(0)
            widget = item.widget()
            if widget is not None:
                widget.deleteLater()

        for status in self._supervisor.statuses():
            row = QFrame(self)
            row.setFrameShape(QFrame.Shape.StyledPanel)
            layout = QVBoxLayout(row)

            header = QHBoxLayout()
            name = QLabel(status.label, row)
            name.setObjectName("readinessRowName")
            badge = QLabel(_health_text(status.health), row)
            badge.setObjectName("readinessBadge")
            header.addWidget(name, 1)
            header.addWidget(badge)

            detail = QLabel(status.detail, row)
            detail.setWordWrap(True)
            detail.setObjectName("readinessDetail")

            layout.addLayout(header)
            layout.addWidget(detail)

            if status.fixHint and status.health != ServiceHealth.Ok:
                hint = QLabel(f"Fix: {status.fixHint}", row)
                hint.setWordWrap(True)
                hint.setObjectName("readinessHint")
                layout.addWidget(hint)

            self._rows_layout.addWidget(row)
        self._rows_layout.addStretch(1)
