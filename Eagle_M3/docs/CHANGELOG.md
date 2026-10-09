# Changelog

## M3 — Speech input (2026-10-09)

- `whisper-server` supervised by `ServiceSupervisor` via `WhisperServerProcess`: start on 127.0.0.1:8178, wait for `/health`, restart on crash with backoff, kill on exit, adopt an orphan already on the port; log in `logs/whisper-server.log`.
- `SttEngine`: `QAudioSource` → `PcmTo16kMono` → `Vad` → `WhisperServerClient` (multipart `/inference`, one request in flight, FIFO, retry while the model loads, timeout). Each VAD segment is transcribed while the speaker continues; Stop waits only for the last one.
- `TranscriptAssembler`: whitespace/annotation cleanup, whisper hallucination filter, bias prompt (terms + previous text).
- `LanguageService`: script detection, Hinglish heuristic, Urdu-script-as-Hindi, no-voice fallback; Auto language in the UI.
- `ListenController` (UI): Listen / Stop listening / Transcribing… button, mic level meter, live editable transcript, mic hot-plug, Space / PageDown / PageUp keys, barge-in, mic muted while Eagle speaks; status line shows "text ready N s after Stop".
- `ChildProcessGuard`: whisper-server and Piper workers die with Eagle (Windows job object; Linux parent-death signal).
- Paths: single source for whisper-server and model with fallback (fixes "whisper.cpp was not found"); prototype `SpeechInput` removed.
- Config: new `stt` keys; missing keys are migrated into saved configs.
- Fix: fillers resource path (fillers were never loaded in M2).
- Header toggle renamed "Voice on/off" and wired to speech output.
- Tests: `tst_vad`, `tst_languageservice`, `tst_pcmto16kmono`, `tst_transcriptassembler`, `tst_whisperclient` (mock server), `tst_sttengine` (end-to-end with mock server), `tst_whisperserverprocess` (fake server; Unix only).

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
