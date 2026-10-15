from eaglegd.llm.template_engine import TemplateEngine


def test_render_simple() -> None:
    out = TemplateEngine.render("Hello {{name}}!", {"name": "Eagle"})
    assert out == "Hello Eagle!"


def test_placeholders_in() -> None:
    keys = TemplateEngine.placeholders_in("{{a}} and {{b}}")
    assert "a" in keys and "b" in keys
