from __future__ import annotations

import re


def _should_apply(text: str) -> bool:
    markers = ("हूँ", "हूं", "ूँगा", "ूंगा", "ऊँगा", "ऊंगा")
    return any(m in text for m in markers)


class HindiFeminineFixer:
    @staticmethod
    def apply(sentence: str) -> str:
        if not _should_apply(sentence):
            return sentence
        out = sentence
        replacements = [
            (re.compile(r"(\S+)ता हूँ"), r"\1ती हूँ"),
            (re.compile(r"(\S+)ता हूं"), r"\1ती हूं"),
            (re.compile(r"रहा हूँ"), "रही हूँ"),
            (re.compile(r"रहा हूं"), "रही हूं"),
            (re.compile(r"गया हूँ"), "गई हूँ"),
            (re.compile(r"गया हूं"), "गई हूं"),
            (re.compile(r"(\S+)ूँगा$"), r"\1ूँगी"),
            (re.compile(r"(\S+)ूंगा$"), r"\1ूंगी"),
            (re.compile(r"(\S+)ऊँगा$"), r"\1ऊँगी"),
            (re.compile(r"(\S+)ऊंगा$"), r"\1ऊंगी"),
        ]
        for pattern, repl in replacements:
            out = pattern.sub(repl, out)
        return out
