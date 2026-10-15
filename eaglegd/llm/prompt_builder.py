"""Prompt assembly from template files (ported from PromptBuilder)."""

from __future__ import annotations

from dataclasses import replace
from pathlib import Path
from typing import Dict, List, Optional

from eaglegd.llm.config import AppConfigProtocol
from eaglegd.llm.il_llm_backend import LlmMessage
from eaglegd.llm.prompt_types import EagleMode, SessionLanguage, TurnInputs, UnitContext
from eaglegd.llm.template_engine import TemplateEngine
from eaglegd.llm.token_estimator import TokenEstimator


def _register_for_grade(programme: str, grade: str) -> str:
    try:
        g = int(grade)
    except ValueError:
        g = 9
    if programme == "DP" or g >= 11:
        return "precise subject vocabulary and an assessment-aware tone"
    if g <= 8:
        return "simple words, short sentences and everyday examples"
    return "clear academic language that stays accessible"


class PromptBuilder:
    def __init__(
        self,
        config: AppConfigProtocol,
        resources_dir: Path,
        estimator: TokenEstimator,
        *,
        data_prompts_dir: Optional[Path] = None,
    ) -> None:
        self._config = config
        self._resources_dir = Path(resources_dir)
        self._data_prompts_dir = Path(data_prompts_dir) if data_prompts_dir else None
        self._estimator = estimator

        self._system_school = ""
        self._unit_card = ""
        self._unit_card_none = ""
        self._doubt = ""
        self._tail = ""

        self._cached_system = ""
        self._cached_unit_key = ""
        self._cached_unit_context = UnitContext()

    def load_templates(self) -> bool:
        self._system_school = self._load_template_file("system_school.txt")
        self._unit_card = self._load_template_file("unit_card.txt")
        self._unit_card_none = self._load_template_file("unit_card_none.txt")
        self._doubt = self._load_template_file("doubt.txt")
        self._tail = self._load_template_file("tail.txt")
        self._cached_system = ""
        return bool(self._system_school and self._doubt and self._tail)

    def _load_template_file(self, name: str) -> str:
        def normalize(text: str) -> str:
            return text.replace("\r\n", "\n").replace("\r", "\n")

        if self._data_prompts_dir is not None:
            data_override = self._data_prompts_dir / name
            if data_override.is_file():
                return normalize(data_override.read_text(encoding="utf-8"))

        resource_path = self._resources_dir / name
        if resource_path.is_file():
            return normalize(resource_path.read_text(encoding="utf-8"))
        return ""

    def validation_errors(self) -> List[str]:
        errors: List[str] = []
        required_keys = {
            "avoid_meta",
            "language_instruction",
            "word_limit",
            "first_sentence_hint",
            "register_short",
            "concept",
            "question",
            "tail",
        }

        sample = TurnInputs(word_limit=75, question="sample")
        unit = UnitContext(register_short="Grade 9 MYP")
        vars = self._base_vars(sample, unit)
        vars["tail"] = self._render_tail(sample)

        def check_template(name: str, text: str) -> None:
            for key in TemplateEngine.placeholders_in(text):
                if key == "tail":
                    continue
                if key not in vars and key not in required_keys:
                    errors.append(f"{name}: missing placeholder {key}")

        check_template("system_school.txt", self._system_school)
        check_template("unit_card_none.txt", self._unit_card_none)
        check_template("doubt.txt", self._doubt)
        check_template("tail.txt", self._tail)
        return errors

    def _language_instruction(self, language: SessionLanguage) -> str:
        if language == SessionLanguage.Hindi:
            return "Hindi in Devanagari script. Mixing a few common English words is fine"
        return "English"

    def _base_vars(self, inputs: TurnInputs, unit: UnitContext) -> Dict[str, str]:
        programme = unit.programme or "MYP"
        grade = unit.grade or "9"
        return {
            "avoid_meta": "1" if self._config.school_avoid_meta_school_words() else "",
            "programme": programme,
            "subject": unit.subject,
            "grade": grade,
            "register": unit.register_text or _register_for_grade(programme, grade),
            "register_short": unit.register_short,
            "has_unit": "1" if unit.has_unit else "",
            "unit_title": unit.unit_title,
            "topic_title": unit.topic_title,
            "concept": unit.concept,
            "concept_meaning": unit.concept_meaning,
            "relevance": unit.relevance,
            "wisdom_question": unit.wisdom_question,
            "ib_links": unit.ib_links,
            "key_facts": unit.key_facts,
            "language_instruction": self._language_instruction(inputs.language),
            "word_limit": str(inputs.word_limit),
            "first_sentence_hint": "1" if self._config.llm_first_sentence_hint() else "",
            "question": inputs.question,
            "notes": inputs.notes,
            "misconception": inputs.misconception,
        }

    def _render_tail(self, inputs: TurnInputs) -> str:
        unit = UnitContext(register_short="Grade 9 MYP")
        vars = self._base_vars(inputs, unit)
        return TemplateEngine.render(self._tail, vars)

    def _unit_cache_key(self, unit: UnitContext) -> str:
        if not unit.has_unit:
            return "none"
        return f"{unit.pack_id}|{unit.topic_id}"

    def system_prompt(self, unit: UnitContext | None = None) -> str:
        ctx = unit if unit is not None else UnitContext()
        if not ctx.has_unit:
            ctx.register_short = "Grade 9 MYP"
        key = self._unit_cache_key(ctx)
        if not self._cached_system or self._cached_unit_key != key:
            dummy = TurnInputs()
            vars = self._base_vars(dummy, ctx)
            core = TemplateEngine.render(self._system_school, vars).strip()
            card_tpl = self._unit_card if ctx.has_unit else self._unit_card_none
            card = TemplateEngine.render(card_tpl, vars).strip()
            self._cached_system = core if not card else core + "\n\n" + card
            self._cached_unit_key = key
            self._cached_unit_context = ctx
        return self._cached_system

    def build_user_turn(self, inputs: TurnInputs, unit: UnitContext) -> str:
        vars = self._base_vars(inputs, unit)
        vars["tail"] = self._render_tail(inputs)
        if inputs.mode == EagleMode.Doubt:
            return TemplateEngine.render(self._doubt, vars)
        return inputs.question

    def build_messages(
        self,
        inputs: TurnInputs,
        unit: UnitContext,
        history: List[LlmMessage],
    ) -> List[LlmMessage]:
        messages: List[LlmMessage] = [
            LlmMessage(role="system", content=self.system_prompt(unit)),
        ]
        messages.extend(history)
        messages.append(LlmMessage(role="user", content=self.build_user_turn(inputs, unit)))
        return messages

    def estimate_new_turn_tokens(
        self,
        inputs: TurnInputs,
        unit: UnitContext,
        history: Optional[List[LlmMessage]] = None,
    ) -> int:
        u = replace(unit, register_short="Grade 9 MYP") if not unit.has_unit else unit
        tokens = self._estimator.estimate_tokens(self.build_user_turn(inputs, u), inputs.language)
        hist = history or []
        if hist and hist[-1].role == "assistant":
            tokens += self._estimator.estimate_tokens(hist[-1].content, inputs.language)
        return tokens
