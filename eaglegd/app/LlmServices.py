from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING

from eaglegd._repo import resource_path
from eaglegd.core.Metrics import Metrics
from eaglegd.llm.HistoryManager import HistoryManager
from eaglegd.llm.OllamaBackend import OllamaBackend
from eaglegd.llm.PromptBuilder import PromptBuilder
from eaglegd.llm.TokenEstimator import TokenEstimator

if TYPE_CHECKING:
    from eaglegd.app.AppConfig import AppConfig
    from eaglegd.app.Paths import Paths


@dataclass
class LlmServices:
    estimator: TokenEstimator | None = None
    prompts: PromptBuilder | None = None
    history: HistoryManager | None = None
    backend: OllamaBackend | None = None
    metrics: Metrics | None = None

    def initialize(
        self, config: AppConfig, paths: Paths, errors_out: list[str] | None = None
    ) -> bool:
        self.estimator = TokenEstimator(config)
        prompts_dir = paths.prompts_dir() if hasattr(paths, "prompts_dir") else resource_path("prompts")
        self.prompts = PromptBuilder(config, prompts_dir, self.estimator)
        if not self.prompts.load_templates():
            if errors_out is not None:
                errors_out.append("Failed to load prompt templates.")
            return False
        validation = self.prompts.validation_errors()
        if validation and errors_out is not None:
            errors_out.extend(validation)
        self.history = HistoryManager(config, self.estimator)
        self.backend = OllamaBackend(config)
        self.metrics = Metrics()
        return True
