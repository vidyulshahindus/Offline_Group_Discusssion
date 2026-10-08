# Eagle GD Offline — architecture (living doc)

See `docs/EAGLE_BUILD_SPEC.md` §6 for the target diagram and module list.

## M1 state

- **LLM:** `src/llm/` — `OllamaBackend`, `PromptBuilder`, `TemplateEngine`, `HistoryManager`, `TokenEstimator`.
- **Core:** `Metrics` + F12 `MetricsOverlay` (`prompt_eval_count`, cache hint).
- **Turn shape:** stable `system` (A.1 + unit card), append-only `history`, per-turn user message (Doubt = A.4.1 + tail).
- **Startup:** probe → `show` (capabilities) → preload (`keep_alive: -1`) → warm-up (system + “Ready.”).
- **Next:** M2 sentence streaming TTS; M4 unit packs and `Retriever`.
