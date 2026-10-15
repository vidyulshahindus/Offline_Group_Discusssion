from __future__ import annotations

import re
from enum import Enum, auto

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QFrame, QHBoxLayout, QLabel, QSizePolicy, QWidget

from eaglegd.core.TextPostProcessor import TextPostProcessor


class ChatRole(Enum):
    User = auto()
    Assistant = auto()


def _to_bubble_html(plain: str) -> str:
    html = plain.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    html = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", html)
    html = html.replace("\n", "<br>")
    return html


class ChatBubble(QFrame):
    def __init__(
        self, role: ChatRole, text: str, parent: QWidget | None = None
    ) -> None:
        super().__init__(parent)
        self._role = role
        self._raw_text = text
        self.setFrameShape(QFrame.Shape.NoFrame)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Minimum)

        outer = QHBoxLayout(self)
        outer.setContentsMargins(16, 6, 16, 6)

        self._label = QLabel(self)
        self._label.setWordWrap(True)
        self._label.setTextFormat(Qt.TextFormat.RichText)
        self._label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
            | Qt.TextInteractionFlag.TextBrowserInteraction
        )
        self._label.setSizePolicy(
            QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Minimum
        )
        self._label.setMaximumWidth(720)

        if role == ChatRole.User:
            outer.addStretch(1)
            outer.addWidget(self._label, 0, Qt.AlignmentFlag.AlignRight)
        else:
            outer.addWidget(self._label, 0, Qt.AlignmentFlag.AlignLeft)
            outer.addStretch(1)

        self._apply_role_style()
        self.setText(text)

    @staticmethod
    def spokenText(raw: str) -> str:
        return TextPostProcessor.stripForSpeech(raw)

    def role(self) -> ChatRole:
        return self._role

    def setText(self, text: str) -> None:
        self._raw_text = text
        if self._role == ChatRole.Assistant:
            visible = TextPostProcessor.stripForDisplay(self._raw_text)
            if not visible and self._raw_text:
                self._label.setText("Thinking…")
            elif not visible:
                self._label.setText("…")
            else:
                self._label.setText(_to_bubble_html(visible))
        else:
            self._label.setText(_to_bubble_html(self._raw_text))

    def text(self) -> str:
        return self._raw_text

    def appendText(self, chunk: str) -> None:
        self._raw_text += chunk
        self.setText(self._raw_text)

    def _apply_role_style(self) -> None:
        if self._role == ChatRole.User:
            self.setStyleSheet(
                "ChatBubble { background: transparent; }"
                "QLabel {"
                "  background-color: #2f6fed;"
                "  color: #ffffff;"
                "  padding: 12px 14px;"
                "  border-radius: 16px;"
                "  font-size: 14px;"
                "  line-height: 1.45;"
                "}"
            )
        else:
            self.setStyleSheet(
                "ChatBubble { background: transparent; }"
                "QLabel {"
                "  background-color: #2f2f2f;"
                "  color: #ececec;"
                "  padding: 12px 14px;"
                "  border-radius: 16px;"
                "  font-size: 14px;"
                "  line-height: 1.45;"
                "}"
            )
