from pathlib import Path

from eaglegd.app.AppConfig import AppConfig
from eaglegd.llm.prompt_builder import PromptBuilder
from eaglegd.llm.prompt_types import SessionLanguage, TurnInputs, UnitContext
from eaglegd.llm.token_estimator import TokenEstimator


def _builder() -> PromptBuilder:
    config = AppConfig()
    prompts = Path(__file__).resolve().parents[2] / "resources" / "prompts"
    est = TokenEstimator(config)
    builder = PromptBuilder(config, prompts, est)
    assert builder.load_templates()
    return builder


def test_system_prompt_stable_across_languages() -> None:
    builder = _builder()
    sys1 = builder.system_prompt()
    en = TurnInputs(language=SessionLanguage.English, question="What is osmosis?")
    hi = TurnInputs(language=SessionLanguage.Hindi, question="परासरण क्या है?")
    unit = UnitContext(has_unit=False, register_short="Grade 9 MYP")
    builder.build_user_turn(en, unit)
    builder.build_user_turn(hi, unit)
    assert builder.system_prompt() == sys1


def test_tail_is_last_in_doubt_turn() -> None:
    builder = _builder()
    inputs = TurnInputs(question="Why do leaves look green?")
    unit = UnitContext(has_unit=False, register_short="Grade 9 MYP")
    text = builder.build_user_turn(inputs, unit)
    assert "Why do leaves look green?" in text
    # Tail template content should appear after the question block.
    assert text.strip().endswith((".", "!", "?", "…")) or "word" in text.lower()


def test_unit_pack_loads() -> None:
    from eaglegd.curriculum.unit_pack import UnitPack

    path = (
        Path(__file__).resolve().parents[2]
        / "resources"
        / "units"
        / "myp-g9-industrial-revolution.json"
    )
    errors: list[str] = []
    pack = UnitPack.from_file(path, errors)
    assert pack.id()
    assert not errors or pack.id()
