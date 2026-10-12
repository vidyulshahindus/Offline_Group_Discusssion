# Changelog

## M4 — Curriculum layer, part A (2026-10-12)

### M4 bugfix — Ollama aux crash on unit change / restore

- Serialized Ollama aux (`show` → `preload` → warm-up) with coalesced `scheduleWarmUp`; safe reply abort without re-entrant `finished` corruption.
- QSettings unit restore no longer fires a competing warm-up; startup sequence uses the restored unit system prompt once.
- Unit change during a turn aborts stream/speech only (`abortActiveTurn`); re-warm queues after aux steps finish.
- System prompt: blank line before `UNIT CARD`; templates normalized to LF.
- Tests: `tst_ollamabackend` (preload + unit change, rapid warm-ups, abort aux); `tst_promptbuilder` (`\n\nUNIT CARD`, no `\r`).

- Appendix B/C reference JSON and three sample unit packs (D.2–D.4) in resources; `UnitPack` validation, `UnitLibrary`, `ClmReference`, `IbReference`, `UnitContextBuilder`, `CurriculumController`.
- Unit/topic/period pickers; context status line; QSettings persistence; unit card in system prompt; re-warm on unit/topic change with queued send during warm-up.
- Doubt turn links to unit concept when a pack is loaded; STT bias terms extended from topic `key_terms` (config API only).
- Unit tests: `tst_unitpack`; extended `tst_promptbuilder`.

## M3 — Speech input (2026-10-12)

- **Word limit:** Soft spoken limit — complete sentences only; optional overflow cap (`discussion.word_limit_overflow`, default 0.5); assistant bubble and history match spoken text; no TTS for token-truncated fragments.
- `AudioCapture`, `PcmTo16kMono`, `Vad`, `WhisperServerClient`, `SttEngine`; `LanguageService` with `languages.json` and `hinglish.json`.
- `ServiceSupervisor` starts/stops `whisper-server` on `stt.host`/`stt.port`, restarts on language change, health via `GET /health`.
- `Paths::whisperServerExe()` + `resolvedWhisperModel()`; Readiness checks executable and model file.
- Replaced prototype `SpeechInput` / `whisper-cli` with incremental segment transcription; editable live transcript; bias terms in config.
- Barge-in on Listen; mic muted while Eagle speaks (`SpeechQueuePlayer` unchanged).
- Unit tests: `tst_vad`, `tst_languageservice`.

## M2 — Speech output (2026-10-09)

- `SentenceStreamer`, `TextPostProcessor`, `HindiFeminineFixer`; unit tests `tst_sentencestreamer`, `tst_textpostprocessor`, `tst_hindifixer`.
- `TtsEngine` + `PiperCliBackend` + `SpeechQueuePlayer` (`QMediaPlayer` WAV queue); `TurnSpeechController` streams sentences to TTS during LLM generation.
- Per-turn timestamps (T0…T1) on `TurnMetrics` and F12 overlay; rolling T0→T1 p50/p95.
- Esc stops LLM + speech; word-budget abort via `SentenceStreamer`.
- `Paths::whisperModel` fallback model; `resources/data/lang/fillers.json`.
- Persistent Piper worker per voice (stdin JSON lines, stdout paths); filler PCM cache + `filler_after_ms` playback.
- Legacy `PiperSpeaker` retained for device UI only.

## M1 — LLM core (2026-10-08)

### M1 bugfix — Doubt chat stuck on "…"

- Serial startup (`runStartupSequence`): show → preload → warm-up on one aux connection; warm-up uses `stream: false`.
- `isBusy()` blocks user turns while aux or stream is active; `think: false` from `/api/show`, name heuristic, or per-model memory after a thinking-only stream.
- On thinking-only NDJSON (non-empty `message.thinking`, empty `content`), abort once and retry with `think: false`; remember model.
- F12 cache hint uses `prompt_eval_count − prompt_eval_cached_count` (Ollama reports total + cached separately); doubt turn omits concept link when no unit is loaded.
- Stream parser: ignore `message.thinking` for UI; fail clearly on `done` with empty content; log full `/api/chat` bodies under `eagle.ollama`.
- Manual Ollama curl fixtures: `tests/fixtures/ollama/`; regression `tst_ollamabackend` (mock HTTP NDJSON server).

- Added `OllamaBackend` (`/api/show`, capabilities, `think:false` for thinking models, `keep_alive`, load options, metrics, preload, warm-up, streaming, timeouts, fallback model).
- Added `PromptBuilder` + `TemplateEngine` with Appendix A.1, A.2, A.4.1 and `tail.txt` (resources + data-dir overrides).
- Added `HistoryManager`, `TokenEstimator`, `Metrics`, and F12 **Metrics overlay** (`prompt_eval_count` vs estimated new tokens).
- Wired **Doubt** mode end-to-end (typed chat): stable system prompt, byte-identical history replay, English/Hindi reply language.
- Replaced prototype `OllamaClient`; unit tests `tst_promptbuilder`, `tst_historymanager`.
- **Paths:** added `<appDir>/../../tools` for dev builds from `build/mingw-release/`.

## M0 — Foundation (2026-10-08)

- Renamed the CMake target from `OllamaChat` to **EagleGD**; added `CMakePresets.json`.
- Added `AppConfig`, `Paths`, file logging, `ServiceSupervisor` (Ollama probe + tool path checks), and startup **Readiness** dialog.
- Split the monolithic `MainWindow` into `MainWindow` + `ChatPanel`; removed hard-coded user tool paths.
- Prototype chat (Ollama streaming, whisper-cli STT, Piper TTS) retained.
