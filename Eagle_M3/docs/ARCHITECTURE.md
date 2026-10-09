# Eagle GD Offline — architecture (living doc)

See `docs/EAGLE_BUILD_SPEC.md` §6 for the target diagram and module list.

## M1 state

- **LLM:** `src/llm/` — `OllamaBackend`, `PromptBuilder`, `TemplateEngine`, `HistoryManager`, `TokenEstimator`.
- **Core:** `Metrics` + F12 `MetricsOverlay` (`prompt_eval_count`, cache hint).
- **Turn shape:** stable `system` (A.1 + unit card), append-only `history`, per-turn user message (Doubt = A.4.1 + tail).
- **Startup:** probe → `show` (capabilities) → preload (`keep_alive: -1`) → warm-up (system + “Ready.”).
- **Next:** M2 sentence streaming TTS; M4 unit packs and `Retriever`.

## M3 state (speech input)

```
Mic ─ QAudioSource ─ PcmTo16kMono ─ Vad ──segment──▶ WhisperServerClient ──POST /inference──▶ whisper-server
          (GUI thread, 500 ms buffer)      (30 ms frames)     (1 in flight, FIFO)                (child process)
                                                                   │
                         TranscriptAssembler ◀── text ─────────────┘
                                │
          ListenController ◀────┘  live text in the input box → Enter / auto-submit → MainWindow::onSendClicked
                                                                                          │
                                                       LanguageService (Auto) ◀── reply language for the turn
```

- **src/stt/**: `Vad`, `PcmTo16kMono`, `WavUtil`, `TranscriptAssembler`, `WhisperServerClient`, `SttEngine`.
- **src/app/**: `WhisperServerProcess` (child process + health + restart), `ChildProcessGuard`, `SttConfig` (config → STT settings).
- **src/core/**: `LanguageService`.
- **src/ui/**: `ListenController`.
- `TurnSpeechController::outputActiveChanged` mutes the microphone while Eagle's audio plays.
- **Next:** M4 unit packs (unit key terms become the whisper bias prompt), `Retriever`.

