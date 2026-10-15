from __future__ import annotations

from typing import Any

from eaglegd.curriculum.ib_reference import IbReference
from eaglegd.curriculum.unit_pack import UnitPack
from eaglegd.prompt_types import UnitContext


def _register_short(programme: str, grade: int) -> str:
    if programme == "DP":
        return f"Grade {grade} DP"
    if programme == "MYP":
        return f"Grade {grade} MYP"
    return f"Grade {grade}"


def _build_ib_links(ib_root: dict[str, Any], ib: IbReference) -> str:
    parts: list[str] = []
    myp = ib_root.get("myp")
    if isinstance(myp, dict) and myp:
        key_concept = str(myp.get("key_concept", ""))
        if key_concept:
            blurb = ib.myp_key_concept_blurb(key_concept)
            suffix = f" — {blurb}" if blurb else ""
            parts.append(f"MYP key concept: {key_concept}{suffix}")
        gc = str(myp.get("global_context", ""))
        if gc:
            focus = ib.global_context_summary(gc)
            suffix = f" — {focus}" if focus else ""
            parts.append(f"Global context: {gc}{suffix}")
        related = myp.get("related_concepts")
        if isinstance(related, list) and related:
            names = [str(v) for v in related]
            parts.append(f"Related concepts: {', '.join(names)}")
    dp = ib_root.get("dp")
    if isinstance(dp, dict) and dp:
        theme = str(dp.get("theme_or_organiser", ""))
        if theme:
            parts.append(f"DP theme: {theme}")
    lp = ib_root.get("learner_profile")
    if isinstance(lp, list) and lp:
        attrs = [str(v) for v in lp]
        parts.append(f"Learner profile: {', '.join(attrs)}")
    return "; ".join(parts)


class UnitContextBuilder:
    @staticmethod
    def none() -> UnitContext:
        return UnitContext(
            has_unit=False,
            register_short="Grade 9 MYP",
            concept="",
        )

    @staticmethod
    def from_pack(pack: UnitPack, topic_index: int, ib: IbReference) -> UnitContext:
        unit = UnitContext(
            has_unit=True,
            pack_id=pack.id(),
            programme=pack.programme(),
            grade=str(pack.grade()),
            subject=pack.subject(),
            register_short=_register_short(pack.programme(), pack.grade()),
            unit_title=pack.title(),
        )
        clm = pack.document.get("clm")
        if not isinstance(clm, dict):
            return unit
        unit.concept = str(clm.get("concept", ""))
        unit.concept_meaning = str(clm.get("concept_meaning", ""))
        unit.relevance = str(clm.get("relevance", ""))
        unit.wisdom_question = str(clm.get("wisdom_question", ""))

        topics = clm.get("topics")
        if not isinstance(topics, list) or not topics:
            return unit
        if topic_index < 0 or topic_index >= len(topics):
            topic_index = 0
        topic = topics[topic_index]
        if not isinstance(topic, dict):
            return unit
        unit.topic_id = str(topic.get("id", ""))
        unit.topic_title = str(topic.get("title", ""))
        unit.key_facts = str(topic.get("key_facts", ""))

        ib_root = pack.document.get("ib")
        if isinstance(ib_root, dict):
            unit.ib_links = _build_ib_links(ib_root, ib)
        return unit
