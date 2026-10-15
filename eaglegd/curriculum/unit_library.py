from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from eaglegd.app.paths import Paths
from eaglegd.curriculum.unit_pack import UnitPack
from eaglegd.resources import resource_path

_BUILTIN_UNIT_FILES = (
    "dp-bio-g11-cells-form-function.json",
    "myp-g9-industrial-revolution.json",
    "myp-g7-delhi-sultanate.json",
)


@dataclass
class UnitLibraryEntry:
    pack: UnitPack
    display_label: str = ""
    from_builtin: bool = False


class UnitLibrary:
    def __init__(self, paths: Paths) -> None:
        self._paths = paths
        self._entries: list[UnitLibraryEntry] = []
        self.reload()

    def reload(self) -> None:
        self._entries.clear()
        self._load_builtin()
        self._load_school_dir()

    def entries(self) -> list[UnitLibraryEntry]:
        return list(self._entries)

    def _load_builtin(self) -> None:
        for file_name in _BUILTIN_UNIT_FILES:
            path = resource_path(f"units/{file_name}")
            err: list[str] = []
            pack = UnitPack.from_file(path, err)
            if pack.id():
                pack.set_builtin(True)
                pack.set_source_path(str(path))
                entry = UnitLibraryEntry(
                    pack=pack,
                    from_builtin=True,
                    display_label=f"{pack.title()} — {pack.programme()} (sample)",
                )
                if self._should_list(pack):
                    self._entries.append(entry)

    def _load_school_dir(self) -> None:
        units_dir = self._paths.units_dir
        if not units_dir.is_dir():
            return
        for path in sorted(units_dir.glob("*.json")):
            err: list[str] = []
            pack = UnitPack.from_file(path, err)
            if not pack.id() or pack.id() == "domain_science_museum":
                continue
            entry = UnitLibraryEntry(
                pack=pack,
                display_label=f"{pack.title()} — {pack.programme()}",
            )
            if self._should_list(pack):
                self._entries.append(entry)

    @staticmethod
    def _should_list(pack: UnitPack) -> bool:
        status = pack.approval_status()
        if pack.is_builtin:
            return True
        return status in ("approved", "reviewed")

    def filtered(
        self, programme: str, grade: int, subject: str
    ) -> list[UnitLibraryEntry]:
        out: list[UnitLibraryEntry] = []
        for entry in self._entries:
            if programme and entry.pack.programme() != programme:
                continue
            if grade > 0 and entry.pack.grade() != grade:
                continue
            if subject and entry.pack.subject().casefold() != subject.casefold():
                continue
            out.append(entry)
        return out

    def pack_by_id(self, pack_id: str) -> UnitPack:
        for entry in self._entries:
            if entry.pack.id() == pack_id:
                return entry.pack
        return UnitPack()
