# Python ↔ C++ (M4b) parity

Comparison of the running Python package `eaglegd/` against `cpp_archive/` at M4b. **No code was changed for this document.**

Canonical Python paths are the modules wired by `eaglegd/app/Application.py` / `LlmServices.py` / `ui/MainWindow.py`. Where a fuller snake_case twin exists but is not wired, that is noted under status.

| Feature | C++ file(s) | Python file(s) | Status | Test covering it |
| --- | --- | --- | --- | --- |
| System prompt: school core + unit card / none; LF templates; blank line before card; cached per pack\|topic (byte-stable across turns/modes/languages) | `cpp_archive/src/llm/PromptBuilder.cpp`, `TemplateEngine.cpp`; templates under `resources/prompts/` | `eaglegd/llm/prompt_builder.py` (re-export `PromptBuilder.py`), `template_engine.py` | **ported** | Python: `tests/python/test_prompt_builder.py` (stable across languages only). C++: `cpp_archive/tests_unit/tst_promptbuilder.cpp` (unit card, topic change, `\n\nUNIT CARD`, no `\r`) — Python suite thinner |
| User turn: doubt template + tail last; language/word limit only in user message | `PromptBuilder.cpp` | `prompt_builder.py` | **ported** | `tests/python/test_prompt_builder.py` (tail-ish); C++ `tst_promptbuilder` |
| `think: false` on chat/warm for thinking models; one thinking-only retry | `OllamaBackend.cpp` | Wired: `eaglegd/llm/OllamaBackend.py`. Fuller twin: `ollama_backend.py` (unused by `LlmServices`) | **partial** — wired backend sends `think:false` + retry; lacks C++/snake_case model-capability `show` parsing sophistication | C++ `tst_ollamabackend.cpp`. No Python Ollama backend test |
| `keep_alive: -1` (config default) on chat / preload / warm | `AppConfig.cpp`, `OllamaBackend.cpp` | `app/AppConfig.py` (`llmKeepAlive` → -1), `llm/OllamaBackend.py` | **ported** | None (config default only) |
| Serialized aux queue (show → preload → warm) | `OllamaBackend.cpp` | `OllamaBackend.py` (queue + pump) | **partial** — queue exists; startup does **not** enqueue post-preload warm with restored unit prompt the way C++ / `ollama_backend.py` do | C++ `tst_ollamabackend` |
| Coalesced warm-ups (`scheduleWarmUp` overwrites pending; single enqueue) | `OllamaBackend.cpp` (`m_hasCoalescedWarm`, timer) | Wired `OllamaBackend.py`: **missing** (each call appends a WarmUp job). Complete logic in unused `ollama_backend.py` | **partial** / **missing** in wired path | C++ `tst_ollamabackend` (rapid warm-ups → one POST) |
| Safe abort (stream/aux abort without re-entrant finished crash; abort active turn vs full abort) | `OllamaBackend.cpp` (`abort`, `abortActiveTurn`, `abortStreamForThinkingRetry`) | `OllamaBackend.py` (`_safe_abort_*`, `abortActiveTurn`) | **partial** — basic safe abort present; no coalesced-warm clear / aux-reorder behavior of M4b | C++ `tst_ollamabackend` (`abortDuringAux…`) |
| Soft word limit: finish current sentence; hard cap = limit × (1 + overflow); stop after budget | `SentenceStreamer.cpp`, `AppConfig` `discussion.word_limit_overflow` | `core/SentenceStreamer.py`, `AppConfig.discussionWordLimitOverflow` | **ported** | C++ `tst_sentencestreamer.cpp`. No Python sentence-streamer test |
| Assistant bubble + history = spoken text (not raw LLM stream) | `MainWindow.cpp` (`spokenAssistantText` → bubble / `appendTurn`) | `ui/MainWindow.py` (`spokenAssistantText` → `bubble.setText` / `append_turn`) | **ported** | None (integration); streamer unit tests in C++ only |
| whisper-server process start/stop; lang restart; `GET /health` | `ServiceSupervisor.cpp` | `app/ServiceSupervisor.py` | **ported** | None (manual / readiness UI) |
| VAD 300 ms calibrate; noise floor = 20th percentile of frame dB | `stt/Vad.cpp` | `stt/vad.py` | **ported** | C++ `tst_vad.cpp`. No Python VAD test |
| Mic muted only during audio playback (`mute_mic_while_speaking`) | `SttEngine.cpp`, `MainWindow` ↔ `audioPlaybackChanged` | `stt/stt_engine.py`, `MainWindow._sync_mic_playback_mute` | **ported** | None |
| Incremental VAD segments + whole-recording fallback if no text | `SttEngine.cpp`, `WhisperServerClient.cpp` | `stt/stt_engine.py`, `whisper_server_client.py` | **ported** | None |
| Unit topic `key_terms` + config bias → whisper `prompt` | `MainWindow.cpp` `applySttBiasFromUnit`, `SttEngine::buildPrompt` | `MainWindow._apply_stt_bias_from_unit`, `stt_engine._build_prompt` | **ported** | None |
| Hindi / Hinglish: script detect, hinglish tokens, Urdu→Hindi, hallucination filter | `LanguageService.cpp` + `hinglish.json` / `languages.json` | `core/LanguageService.py` (+ `language_service.py` aliases) | **ported** | C++ `tst_languageservice.cpp`. No Python language-service test |
| Persistent Piper workers (stdin queue, cold/warm synth timing) | `tts/PiperCliBackend.cpp`, `TtsEngine.cpp` | `tts/PiperCliBackend.py`, `TtsEngine.py` via `TurnSpeechController` | **ported** (legacy `piper_speaker.py` still used for device list / stop chrome, same dual path as C++ `piperspeaker`) | C++ `tst_speechqueueplayer` / audio tests. No Python Piper worker test |
| Sentence streaming LLM → TTS during generation | `SentenceStreamer` + `TurnSpeechController` | same under `eaglegd/core/` | **ported** | C++ `tst_sentencestreamer` |
| Fillers: load JSON, warm into cache, play after delay | `FillerCache.cpp`, `TurnSpeechController` | `tts/FillerCache.py`, `TurnSpeechController.py` | **ported** | None |
| Unit pack load / validate / schema `eagle.unitpack.v1` | `UnitPack.cpp` / `UnitPackValidator.cpp` | `curriculum/unit_pack.py` (+ validator shim) | **ported** | Python packs via `test_prompt_builder.test_unit_pack_loads`; C++ `tst_unitpack.cpp` richer |
| Library + approval filter (builtin always; else approved/reviewed) | `UnitLibrary.cpp` | `curriculum/unit_library.py` | **ported** | C++ `tst_unitpack` / library usage; no dedicated Python library test |
| Unit / topic / period pickers + context line | `ChatPanel.cpp`, `MainWindow.cpp`, `CurriculumController.cpp` | `ui/ChatPanel.py`, `MainWindow.py`, `curriculum_controller.py` | **ported** | None |
| Re-warm on unit/topic change; queue send while warming | `CurriculumController` + `MainWindow` | `curriculum_controller.py` + `MainWindow` (`_retry_send_after_warm`) | **ported** (depends on backend `scheduleWarmUp` quality — see coalescing gap) | C++ `tst_ollamabackend` + manual |
| Restore pack/topic/period on startup without competing warm | `CurriculumController::restoreSettings` (`requestWarmup=false`) | `curriculum_controller.restore_settings` (`request_warmup=False`) | **ported** | None |
| Retriever BM25 notes: k1=1.2, b=0.75, top 2, ≤160 words | `curriculum/Retriever.cpp` | `curriculum/retriever.py` | **ported** | C++ `tst_retriever.cpp`. No Python retriever test |
| TextPostProcessor: strip think/tool tags, URLs/MD, role labels, symbols, list markers, banned phrases, Hindi feminine, terminal punct | `TextPostProcessor.cpp`, `HindiFeminineFixer.cpp` | `core/TextPostProcessor.py`, `HindiFeminineFixer.py` | **ported** | Python `tests/python/test_hindi_fixer.py`. C++ `tst_textpostprocessor` + `tst_hindifixer` |
| Logging to daily file; STT logs metadata/char counts **not** transcript text | `app/Logging.cpp`; `SttEngine` logs `chars` only | `app/Logging.py`; `stt_engine` does not log segment text (discards reply metadata) | **ported** (Python STT even quieter than C++) | None |

---

## Missing / partial (priority order)

1. **Wire complete Ollama warm-up / abort behavior (M4b)** — Highest. `LlmServices` uses `eaglegd/llm/OllamaBackend.py`, which lacks coalesced `scheduleWarmUp`, does not enqueue the startup warm with the restored unit system prompt after preload, and does not match M4b aux abort/coalesce tests. A closer port already exists in `eaglegd/llm/ollama_backend.py` but is unused by the app.
2. **Python tests for speech / LLM backend / VAD / streamer / TPP / retriever** — C++ has `tst_vad`, `tst_sentencestreamer`, `tst_textpostprocessor`, `tst_retriever`, `tst_ollamabackend`, `tst_languageservice`, `tst_historymanager`, etc. Python only covers prompt builder (thin), template engine, and Hindi fixer.
3. **Prompt-builder regression depth** — Port unit-card blank-line, topic-change invalidates cache, and LF-only template checks from `tst_promptbuilder` into `tests/python/`.
4. **Dual module trees (PascalCase vs snake_case)** — Several areas have two implementations (notably Ollama). Risk of fixing the unused file while the wired one stays partial; consolidate or re-export one source of truth.
5. **Behavioral drift: first-token timeout** — C++ uses 12 s; wired Python `OllamaBackend` uses 60 s (intentional cold-load workaround). Documented divergence, not a missing feature, but not M4b-identical.
6. **STT operational logging** — C++ logs segment timing / char counts / fallback; Python skips those logs. Privacy parity is fine; ops/debug parity is weaker.
7. **Legacy `PiperSpeaker` still in MainWindow chrome** — Same pattern as C++; not blocking TurnSpeech path, but duplicate speaker/stop paths can confuse future fixes.

---

## Notes

- Baseline for “done” on curriculum/LLM warm-up is **M4b** (CHANGELOG: coalesced warm-ups, safe aux abort, restore without competing warm, `\n\n` before unit card).
- AppImage / packaging and Linux tool paths are out of scope for this table.
- Do not treat unused snake_case modules as shipped behavior unless `Application` / `LlmServices` / `MainWindow` import them.
