"""Curriculum layer (lazy exports — import submodules if PySide6 is unavailable)."""

from typing import TYPE_CHECKING

__all__ = [
    "ClmPeriodInfo",
    "ClmReference",
    "CommandTermInfo",
    "CurriculumController",
    "IbReference",
    "RetrievalHit",
    "Retriever",
    "UnitContextBuilder",
    "UnitLibrary",
    "UnitLibraryEntry",
    "UnitPack",
    "UnitPackValidation",
    "load_document",
]

_LAZY = {
    "ClmPeriodInfo": ("eaglegd.curriculum.clm_reference", "ClmPeriodInfo"),
    "ClmReference": ("eaglegd.curriculum.clm_reference", "ClmReference"),
    "CommandTermInfo": ("eaglegd.curriculum.ib_reference", "CommandTermInfo"),
    "CurriculumController": ("eaglegd.curriculum.curriculum_controller", "CurriculumController"),
    "IbReference": ("eaglegd.curriculum.ib_reference", "IbReference"),
    "RetrievalHit": ("eaglegd.curriculum.retriever", "RetrievalHit"),
    "Retriever": ("eaglegd.curriculum.retriever", "Retriever"),
    "UnitContextBuilder": ("eaglegd.curriculum.unit_context_builder", "UnitContextBuilder"),
    "UnitLibrary": ("eaglegd.curriculum.unit_library", "UnitLibrary"),
    "UnitLibraryEntry": ("eaglegd.curriculum.unit_library", "UnitLibraryEntry"),
    "UnitPack": ("eaglegd.curriculum.unit_pack", "UnitPack"),
    "UnitPackValidation": ("eaglegd.curriculum.unit_pack", "UnitPackValidation"),
    "load_document": ("eaglegd.curriculum.json_resource", "load_document"),
}


def __getattr__(name: str):
    if name in _LAZY:
        module_name, attr = _LAZY[name]
        import importlib

        module = importlib.import_module(module_name)
        return getattr(module, attr)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


if TYPE_CHECKING:
    from eaglegd.curriculum.clm_reference import ClmPeriodInfo, ClmReference
    from eaglegd.curriculum.curriculum_controller import CurriculumController
    from eaglegd.curriculum.ib_reference import CommandTermInfo, IbReference
    from eaglegd.curriculum.json_resource import load_document
    from eaglegd.curriculum.retriever import RetrievalHit, Retriever
    from eaglegd.curriculum.unit_context_builder import UnitContextBuilder
    from eaglegd.curriculum.unit_library import UnitLibrary, UnitLibraryEntry
    from eaglegd.curriculum.unit_pack import UnitPack, UnitPackValidation
