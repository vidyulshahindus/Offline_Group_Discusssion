# Eagle GD Offline — architecture (living doc)

See `docs/EAGLE_BUILD_SPEC.md` §6 for the target diagram and module list.

## M0 state

- **Process:** single Qt app `EagleGD`; helpers (Ollama, whisper, Piper, robot bridge) are external.
- **App layer:** `Application` loads `eagle_config.json`, `Paths`, logging, and `ServiceSupervisor`.
- **UI:** `MainWindow` orchestrates services; `ChatPanel` holds the prototype chat chrome; `ReadinessPanel` shows health checks at startup.
- **Next:** M1 introduces `OllamaBackend`, `PromptBuilder`, and `HistoryManager` under `src/llm/`.
