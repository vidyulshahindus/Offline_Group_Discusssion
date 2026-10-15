from __future__ import annotations

from typing import Protocol

from PySide6.QtCore import QObject, QSettings, Signal

from eaglegd.app.paths import Paths
from eaglegd.curriculum.clm_reference import ClmReference
from eaglegd.curriculum.ib_reference import IbReference
from eaglegd.curriculum.unit_context_builder import UnitContextBuilder
from eaglegd.curriculum.unit_library import UnitLibrary
from eaglegd.prompt_types import UnitContext

_SETTINGS_ORG = "EagleGD"
_SETTINGS_APP = "EagleGD"


class PromptBuilder(Protocol):
    def system_prompt(self, context: UnitContext) -> str: ...


class CurriculumController(QObject):
    context_changed = Signal(object, bool)
    warmup_requested = Signal(str, str)
    warming_changed = Signal(bool)
    context_line_changed = Signal(str)

    def __init__(
        self,
        paths: Paths,
        prompts: PromptBuilder | None = None,
        parent: QObject | None = None,
    ) -> None:
        super().__init__(parent)
        self._library = UnitLibrary(paths)
        self._clm = ClmReference()
        self._ib = IbReference()
        self._prompts = prompts
        self._context = UnitContextBuilder.none()
        self._selected_pack_id = ""
        self._topic_index = 0
        self._period = 4
        self._warming = False
        self._clm.load_from_resources()
        self._ib.load_from_resources()

    @property
    def library(self) -> UnitLibrary:
        return self._library

    @property
    def clm(self) -> ClmReference:
        return self._clm

    @property
    def ib(self) -> IbReference:
        return self._ib

    @property
    def current_context(self) -> UnitContext:
        return self._context

    @property
    def period(self) -> int:
        return self._period

    @property
    def selected_pack_id(self) -> str:
        return self._selected_pack_id

    @property
    def selected_topic_index(self) -> int:
        return self._topic_index

    def has_unit_selected(self) -> bool:
        return bool(self._selected_pack_id)

    def is_warming(self) -> bool:
        return self._warming

    def set_warming(self, warming: bool) -> None:
        if self._warming == warming:
            return
        self._warming = warming
        self.warming_changed.emit(warming)

    def restore_settings(self) -> None:
        settings = QSettings(_SETTINGS_ORG, _SETTINGS_APP)
        self._period = int(settings.value("curriculum/period", 4))
        pack_id = str(settings.value("curriculum/packId", "") or "")
        self._topic_index = int(settings.value("curriculum/topicIndex", 0))
        if not pack_id:
            self._apply_selection("", 0, False)
            return
        self._apply_selection(pack_id, self._topic_index, False)

    def save_settings(self) -> None:
        settings = QSettings(_SETTINGS_ORG, _SETTINGS_APP)
        settings.setValue("curriculum/period", self._period)
        settings.setValue("curriculum/packId", self._selected_pack_id)
        settings.setValue("curriculum/topicIndex", self._topic_index)

    def select_no_unit(self) -> None:
        self._apply_selection("", 0, True)

    def select_pack(self, pack_id: str, topic_index: int) -> None:
        self._apply_selection(pack_id, topic_index, True)

    def set_period(self, period: int) -> None:
        self._period = max(1, min(15, period))

    def period_label(self) -> str:
        return self._clm.period_label(self._period)

    def context_line(
        self, mode_label: str, language_label: str, word_limit_label: str
    ) -> str:
        return self._context_status_line(mode_label, language_label, word_limit_label)

    def _apply_selection(self, pack_id: str, topic_index: int, request_warmup: bool) -> None:
        had_unit = bool(self._selected_pack_id)
        if not pack_id:
            changed = had_unit
            self._selected_pack_id = ""
            self._topic_index = 0
            self._rebuild_context(changed, request_warmup)
            return
        pack = self._library.pack_by_id(pack_id)
        if not pack.id():
            self._apply_selection("", 0, request_warmup)
            return
        changed = pack_id != self._selected_pack_id or topic_index != self._topic_index
        self._selected_pack_id = pack_id
        max_index = max(0, pack.topic_count() - 1)
        self._topic_index = max(0, min(topic_index, max_index))
        self._rebuild_context(changed, request_warmup)

    def _rebuild_context(self, unit_or_topic_changed: bool, request_warmup: bool) -> None:
        if not self._selected_pack_id:
            self._context = UnitContextBuilder.none()
        else:
            pack = self._library.pack_by_id(self._selected_pack_id)
            self._context = UnitContextBuilder.from_pack(
                pack, self._topic_index, self._ib
            )
        self.context_changed.emit(self._context, unit_or_topic_changed)
        if unit_or_topic_changed and request_warmup and self._prompts:
            system = self._prompts.system_prompt(self._context)
            title = self._context.unit_title if self._context.has_unit else "General"
            self.warmup_requested.emit(system, title)

    def _context_status_line(
        self, mode_label: str, language_label: str, word_limit_label: str
    ) -> str:
        unit_part = self._context.unit_title if self._context.has_unit else "No unit"
        topic_part = self._context.topic_title if self._context.has_unit else "—"
        period_part = f"P{self._period}"
        mode_part = mode_label or "Doubt"
        lang_part = language_label or "English"
        limit_part = word_limit_label or "75"
        return f"{unit_part} › {topic_part} › {period_part} › {mode_part} › {lang_part} › {limit_part} words"
