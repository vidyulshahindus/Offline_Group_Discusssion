"""LLM prompt building and Ollama backend."""

from eaglegd.llm.config import AppConfig, AppConfigProtocol
from eaglegd.llm.history_manager import HistoryManager
from eaglegd.llm.il_llm_backend import ILlmBackend, LlmMessage, LlmMetrics, LlmRequest
from eaglegd.llm.ollama_backend import OllamaBackend, OllamaLoadOptions
from eaglegd.llm.prompt_builder import PromptBuilder
from eaglegd.llm.prompt_types import EagleMode, SessionLanguage, TurnInputs, UnitContext
from eaglegd.llm.template_engine import TemplateEngine
from eaglegd.llm.token_estimator import TokenEstimator

__all__ = [
    "AppConfig",
    "AppConfigProtocol",
    "EagleMode",
    "HistoryManager",
    "ILlmBackend",
    "LlmMessage",
    "LlmMetrics",
    "LlmRequest",
    "OllamaBackend",
    "OllamaLoadOptions",
    "PromptBuilder",
    "SessionLanguage",
    "TemplateEngine",
    "TokenEstimator",
    "TurnInputs",
    "UnitContext",
]
