from __future__ import annotations

from dataclasses import dataclass

from eaglegd.curriculum.json_resource import load_document


@dataclass
class ClmPeriodInfo:
    period: int = 0
    phase: str = ""
    name: str = ""
    mode: str = ""
    eagle_role: str = ""
    topic: int = 0


class ClmReference:
    def __init__(self) -> None:
        self._periods: list[ClmPeriodInfo] = []

    def load_from_resources(self) -> bool:
        doc = load_document(":/data/clm/clm_phases.json")
        if not doc:
            return False
        self._periods.clear()
        phases = doc.get("phases")
        if not isinstance(phases, list):
            return False
        for value in phases:
            if not isinstance(value, dict):
                continue
            info = ClmPeriodInfo(
                period=int(value.get("period", 0)),
                phase=str(value.get("phase", "")),
                name=str(value.get("name", "")),
                mode=str(value.get("mode", "")),
                eagle_role=str(value.get("eagle_role", "")),
            )
            if value.get("topic") is not None:
                info.topic = int(value.get("topic", 0))
            self._periods.append(info)
        return bool(self._periods)

    def periods(self) -> list[ClmPeriodInfo]:
        return list(self._periods)

    def period_label(self, period: int) -> str:
        for info in self._periods:
            if info.period == period:
                return f"P{period} {info.name}"
        return f"P{period}"

    def period_name(self, period: int) -> str:
        for info in self._periods:
            if info.period == period:
                return info.name
        return ""

    def period_mode(self, period: int) -> str:
        for info in self._periods:
            if info.period == period:
                return info.mode
        return ""
