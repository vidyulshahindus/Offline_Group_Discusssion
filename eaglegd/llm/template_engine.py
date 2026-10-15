"""Mustache-like template rendering (ported from TemplateEngine)."""

from __future__ import annotations

import re
from typing import Dict, List

_BLOCK_RE = re.compile(
    r"\{\{#if\s+([a-zA-Z0-9_]+)\s*\}\}(.*?)\{\{/if\}\}",
    re.DOTALL,
)
_PLACEHOLDER_RE = re.compile(r"\{\{([a-zA-Z0-9_]+)\}\}")
_IF_KEY_RE = re.compile(r"\{\{#if\s+([a-zA-Z0-9_]+)\s*\}\}")


def _is_truthy(value: str) -> bool:
    if not value:
        return False
    lower = value.strip().lower()
    return lower not in ("0", "false", "no")


def _expand_conditionals(text: str, vars: Dict[str, str]) -> str:
    result = text
    offset = 0
    while True:
        match = _BLOCK_RE.search(result, offset)
        if not match:
            break
        key = match.group(1)
        body = match.group(2)
        replacement = body if _is_truthy(vars.get(key, "")) else ""
        start, end = match.span()
        result = result[:start] + replacement + result[end:]
        offset = start + len(replacement)
    return result


class TemplateEngine:
    @staticmethod
    def render(template_text: str, vars: Dict[str, str]) -> str:
        result = _expand_conditionals(template_text, vars)
        offset = 0
        while True:
            match = _PLACEHOLDER_RE.search(result, offset)
            if not match:
                break
            key = match.group(1)
            value = vars.get(key, "")
            start, end = match.span()
            result = result[:start] + value + result[end:]
            offset = start + len(value)
        return result.strip()

    @staticmethod
    def placeholders_in(template_text: str) -> List[str]:
        keys: List[str] = []
        keys.extend(m.group(1) for m in _PLACEHOLDER_RE.finditer(template_text))
        keys.extend(m.group(1) for m in _IF_KEY_RE.finditer(template_text))
        seen: set[str] = set()
        unique: List[str] = []
        for key in keys:
            if key not in seen:
                seen.add(key)
                unique.append(key)
        return unique
