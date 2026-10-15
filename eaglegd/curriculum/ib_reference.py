from __future__ import annotations

from dataclasses import dataclass

from eaglegd.curriculum.json_resource import load_document


@dataclass
class CommandTermInfo:
    term: str = ""
    tier: int = 0
    definition: str = ""
    shape: str = ""
    spoken: bool = True


class IbReference:
    def __init__(self) -> None:
        self._terms: dict[str, CommandTermInfo] = {}
        self._global_contexts: dict[str, str] = {}
        self._myp_key_concepts: dict[str, str] = {}

    def load_from_resources(self) -> bool:
        cmd_doc = load_document(":/data/ib/command_terms.json")
        if not cmd_doc:
            return False
        self._terms.clear()
        terms = cmd_doc.get("terms")
        if isinstance(terms, list):
            for value in terms:
                if not isinstance(value, dict):
                    continue
                info = CommandTermInfo(
                    term=str(value.get("term", "")),
                    tier=int(value.get("tier", 0)),
                    definition=str(value.get("definition", "")),
                    shape=str(value.get("shape", "")),
                    spoken=bool(value.get("spoken", True)),
                )
                self._terms[info.term.casefold()] = info

        gc_doc = load_document(":/data/ib/global_contexts.json")
        self._global_contexts.clear()
        if isinstance(gc_doc, dict):
            contexts = gc_doc.get("global_contexts")
            if isinstance(contexts, list):
                for value in contexts:
                    if not isinstance(value, dict):
                        continue
                    name = str(value.get("name", ""))
                    self._global_contexts[name] = str(value.get("focus", ""))

        myp_doc = load_document(":/data/ib/myp_key_concepts.json")
        self._myp_key_concepts.clear()
        if isinstance(myp_doc, dict):
            defs = myp_doc.get("is_definitions")
            if isinstance(defs, dict):
                for key, val in defs.items():
                    self._myp_key_concepts[str(key)] = str(val)
        return bool(self._terms)

    def command_term(self, term: str) -> CommandTermInfo:
        return self._terms.get(term.strip().casefold(), CommandTermInfo())

    def global_context_summary(self, name: str) -> str:
        return self._global_contexts.get(name, "")

    def myp_key_concept_blurb(self, name: str) -> str:
        return self._myp_key_concepts.get(name, "")
