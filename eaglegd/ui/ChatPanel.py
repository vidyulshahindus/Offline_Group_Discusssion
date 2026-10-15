from __future__ import annotations

from PySide6.QtCore import QEvent, QObject, Qt, Signal
from PySide6.QtGui import QKeyEvent
from PySide6.QtWidgets import (
    QComboBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QProgressBar,
    QPushButton,
    QScrollArea,
    QTextEdit,
    QVBoxLayout,
    QWidget,
)

from eaglegd.ui.chat_bubble import ChatBubble, ChatRole


class ChatPanel(QWidget):
    sendRequested = Signal()
    newChatRequested = Signal()
    stopRequested = Signal()
    micIndexChanged = Signal(int)
    speakerIndexChanged = Signal(int)
    refreshDevicesRequested = Signal()
    recordToggled = Signal()
    listenToggleRequested = Signal()
    unitSelectionChanged = Signal(int)
    topicSelectionChanged = Signal(int)
    periodSelectionChanged = Signal(int)

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._active_assistant_bubble: ChatBubble | None = None
        self._stream_buffer = ""

        root_layout = QVBoxLayout(self)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(0)

        header = QFrame(self)
        header.setObjectName("headerBar")
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(20, 14, 20, 14)
        header_layout.setSpacing(12)

        title = QLabel("Eagle GD", header)
        title.setObjectName("appTitle")

        self._model_combo = QComboBox(header)
        self._model_combo.setObjectName("modelCombo")
        self._model_combo.setToolTip("Model used for the next reply")

        self._mode_combo = QComboBox(header)
        self._mode_combo.setObjectName("modeCombo")
        self._mode_combo.addItem("Doubt", "doubt")
        self._mode_combo.setToolTip("CLM mode for the next turn")

        self._language_combo = QComboBox(header)
        self._language_combo.setObjectName("languageCombo")
        self._language_combo.addItem("English", "en")
        self._language_combo.addItem("Hindi", "hi")
        self._language_combo.setToolTip("Reply language")

        word_limit_label = QLabel("Word limit:", header)
        word_limit_label.setObjectName("wordLimitLabel")

        self._word_limit_combo = QComboBox(header)
        self._word_limit_combo.setObjectName("wordLimitCombo")
        self._word_limit_combo.addItems(
            ["50", "75", "100", "125", "150", "200"]
        )
        self._word_limit_combo.setCurrentText("75")
        self._word_limit_combo.setToolTip(
            "Maximum words the assistant should use in each reply"
        )

        self._unit_combo = QComboBox(header)
        self._unit_combo.setObjectName("unitCombo")
        self._unit_combo.setMinimumWidth(180)
        self._unit_combo.setToolTip("CLM unit pack")
        self._unit_combo.currentIndexChanged.connect(self.unitSelectionChanged)

        self._topic_combo = QComboBox(header)
        self._topic_combo.setObjectName("topicCombo")
        self._topic_combo.setMinimumWidth(160)
        self._topic_combo.setToolTip("Current topic within the unit")
        self._topic_combo.currentIndexChanged.connect(self.topicSelectionChanged)

        self._period_combo = QComboBox(header)
        self._period_combo.setObjectName("periodCombo")
        self._period_combo.setMinimumWidth(120)
        self._period_combo.setToolTip("CLM period (1–15)")
        self._period_combo.currentIndexChanged.connect(self.periodSelectionChanged)

        self._listen_button = QPushButton("Listen", header)
        self._listen_button.setObjectName("listenButton")
        self._listen_button.setCheckable(True)
        self._listen_button.setChecked(True)
        self._listen_button.setToolTip("Speak each assistant reply with Piper")

        new_chat_button = QPushButton("New chat", header)
        new_chat_button.setObjectName("newChatButton")
        new_chat_button.clicked.connect(self.newChatRequested)

        self._status_label = QLabel("Ready", header)
        self._status_label.setObjectName("statusLabel")

        header_layout.addWidget(title)
        header_layout.addSpacing(8)
        header_layout.addWidget(self._model_combo)
        header_layout.addWidget(self._mode_combo)
        header_layout.addWidget(self._language_combo)
        header_layout.addWidget(word_limit_label)
        header_layout.addWidget(self._word_limit_combo)
        header_layout.addWidget(self._unit_combo)
        header_layout.addWidget(self._topic_combo)
        header_layout.addWidget(self._period_combo)
        header_layout.addStretch(1)
        header_layout.addWidget(self._status_label)
        header_layout.addWidget(self._listen_button)
        header_layout.addWidget(new_chat_button)

        self._chat_scroll = QScrollArea(self)
        self._chat_scroll.setObjectName("chatScroll")
        self._chat_scroll.setWidgetResizable(True)
        self._chat_scroll.setHorizontalScrollBarPolicy(
            Qt.ScrollBarPolicy.ScrollBarAlwaysOff
        )

        self._message_container = QWidget(self._chat_scroll)
        self._message_container.setObjectName("messageContainer")
        self._message_layout = QVBoxLayout(self._message_container)
        self._message_layout.setContentsMargins(0, 16, 0, 16)
        self._message_layout.setSpacing(4)
        self._message_layout.addStretch(1)
        self._chat_scroll.setWidget(self._message_container)

        input_bar = QFrame(self)
        input_bar.setObjectName("inputBar")
        input_outer = QVBoxLayout(input_bar)
        input_outer.setContentsMargins(20, 12, 20, 20)
        input_outer.setSpacing(10)

        input_row = QHBoxLayout()
        input_row.setSpacing(10)

        self._mic_combo = QComboBox(input_bar)
        self._mic_combo.setObjectName("micCombo")
        self._mic_combo.setToolTip("Microphone used for speech to text")
        self._mic_combo.currentIndexChanged.connect(self.micIndexChanged)

        self._refresh_mics_button = QPushButton("↻", input_bar)
        self._refresh_mics_button.setObjectName("refreshMicsButton")
        self._refresh_mics_button.setToolTip("Refresh microphones and speakers")
        self._refresh_mics_button.clicked.connect(self.refreshDevicesRequested)

        self._speaker_combo = QComboBox(input_bar)
        self._speaker_combo.setObjectName("speakerCombo")
        self._speaker_combo.setToolTip("Speaker used for Piper playback")
        self._speaker_combo.currentIndexChanged.connect(self.speakerIndexChanged)

        self._record_button = QPushButton("Mic", input_bar)
        self._record_button.setObjectName("recordButton")
        self._record_button.setToolTip("Start or stop voice input")
        self._record_button.clicked.connect(self.recordToggled)

        self._mic_level_bar = QProgressBar(input_bar)
        self._mic_level_bar.setObjectName("micLevelBar")
        self._mic_level_bar.setFixedSize(56, 12)
        self._mic_level_bar.setRange(0, 100)
        self._mic_level_bar.setTextVisible(False)
        self._mic_level_bar.setVisible(False)
        self._mic_level_bar.setToolTip("Microphone input level")

        self._input = QTextEdit(input_bar)
        self._input.setObjectName("messageInput")
        self._input.setPlaceholderText(
            "Ask Eagle…  (Enter to send, Shift+Enter for new line, Mic for voice)"
        )
        self._input.setFixedHeight(52)
        self._input.setTabChangesFocus(False)
        self._input.installEventFilter(self)
        self.installEventFilter(self)

        self._send_button = QPushButton("↑", input_bar)
        self._send_button.setObjectName("sendButton")
        self._send_button.setToolTip("Send message")
        self._send_button.clicked.connect(self.sendRequested)

        input_row.addWidget(self._mic_combo, 0, Qt.AlignmentFlag.AlignBottom)
        input_row.addWidget(self._speaker_combo, 0, Qt.AlignmentFlag.AlignBottom)
        input_row.addWidget(self._refresh_mics_button, 0, Qt.AlignmentFlag.AlignBottom)
        input_row.addWidget(self._record_button, 0, Qt.AlignmentFlag.AlignBottom)
        input_row.addWidget(self._mic_level_bar, 0, Qt.AlignmentFlag.AlignBottom)
        input_row.addWidget(self._input, 1)
        input_row.addWidget(self._send_button, 0, Qt.AlignmentFlag.AlignBottom)

        self._stop_button = QPushButton("Stop generating", input_bar)
        self._stop_button.setObjectName("stopButton")
        self._stop_button.setVisible(False)
        self._stop_button.clicked.connect(self.stopRequested)

        input_outer.addLayout(input_row)
        input_outer.addWidget(
            self._stop_button, 0, Qt.AlignmentFlag.AlignHCenter
        )

        self._context_label = QLabel(self)
        self._context_label.setObjectName("contextLabel")
        self._context_label.setContentsMargins(20, 0, 20, 6)

        root_layout.addWidget(header)
        root_layout.addWidget(self._context_label)
        root_layout.addWidget(self._chat_scroll, 1)
        root_layout.addWidget(input_bar)

    def modelCombo(self) -> QComboBox:
        return self._model_combo

    def modeCombo(self) -> QComboBox:
        return self._mode_combo

    def languageCombo(self) -> QComboBox:
        return self._language_combo

    def wordLimitCombo(self) -> QComboBox:
        return self._word_limit_combo

    def unitCombo(self) -> QComboBox:
        return self._unit_combo

    def topicCombo(self) -> QComboBox:
        return self._topic_combo

    def periodCombo(self) -> QComboBox:
        return self._period_combo

    def contextLabel(self) -> QLabel:
        return self._context_label

    def micCombo(self) -> QComboBox:
        return self._mic_combo

    def speakerCombo(self) -> QComboBox:
        return self._speaker_combo

    def statusLabel(self) -> QLabel:
        return self._status_label

    def listenButton(self) -> QPushButton:
        return self._listen_button

    def sendButton(self) -> QPushButton:
        return self._send_button

    def stopButton(self) -> QPushButton:
        return self._stop_button

    def recordButton(self) -> QPushButton:
        return self._record_button

    def refreshMicsButton(self) -> QPushButton:
        return self._refresh_mics_button

    def input(self) -> QTextEdit:
        return self._input

    def activeAssistantBubble(self) -> ChatBubble | None:
        return self._active_assistant_bubble

    def streamBuffer(self) -> str:
        return self._stream_buffer

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:  # type: ignore[name-defined]
        if event.type() == QEvent.Type.KeyPress:
            key_event = event
            if isinstance(key_event, QKeyEvent):
                if (
                    key_event.key() == Qt.Key.Key_Space
                    and watched != self._input
                ):
                    self.listenToggleRequested.emit()
                    return True
        if watched == self._input and event.type() == QEvent.Type.KeyPress:
            key_event = event
            if isinstance(key_event, QKeyEvent):
                if key_event.key() in (
                    Qt.Key.Key_Return,
                    Qt.Key.Key_Enter,
                ):
                    if not (key_event.modifiers() & Qt.KeyboardModifier.ShiftModifier):
                        self.sendRequested.emit()
                        return True
        return super().eventFilter(watched, event)

    def scrollToBottom(self) -> None:
        bar = self._chat_scroll.verticalScrollBar()
        if bar is not None:
            bar.setValue(bar.maximum())

    def addMessageBubble(self, role: ChatRole, text: str) -> ChatBubble:
        bubble = ChatBubble(role, text, self._message_container)
        insert_index = max(0, self._message_layout.count() - 1)
        self._message_layout.insertWidget(insert_index, bubble)
        self.scrollToBottom()
        return bubble

    def clearConversation(self) -> None:
        self._active_assistant_bubble = None
        self._stream_buffer = ""
        while self._message_layout.count() > 1:
            item = self._message_layout.takeAt(0)
            widget = item.widget()
            if widget is not None:
                widget.deleteLater()

    def currentWordLimit(self) -> int:
        text = self._word_limit_combo.currentText()
        try:
            return int(text)
        except ValueError:
            return 75

    def appendAssistantStream(self, text: str) -> None:
        self._stream_buffer += text
        if self._active_assistant_bubble is not None:
            self._active_assistant_bubble.appendText(text)
            self.scrollToBottom()

    def setActiveAssistantBubble(self, bubble: ChatBubble | None) -> None:
        self._active_assistant_bubble = bubble

    def clearStreamBuffer(self) -> None:
        self._stream_buffer = ""

    def setMicLevelVisible(self, visible: bool) -> None:
        self._mic_level_bar.setVisible(visible)
        if not visible:
            self._mic_level_bar.setValue(0)

    def setMicInputLevel(self, normalized: float) -> None:
        if self._mic_level_bar.isVisible():
            level = int(max(0.0, min(1.0, normalized)) * 100.0)
            self._mic_level_bar.setValue(level)
