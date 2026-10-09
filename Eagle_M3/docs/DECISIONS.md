# Architecture decisions

## M3

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-10-09 | **STT = `whisper-server` child process** (not libwhisper linked in). Flags used: `-m --host --port -t -l` only. Checked against whisper.cpp `examples/server/server.cpp` at v1.9.5-dev (commit d1be6fd, 2026-10-06): every request starts from the server defaults and may override `language`, `prompt`, `response_format`, `temperature`, `temperature_inc`, `no_timestamps`, `suppress_non_speech`. | No whisper.cpp in our CMake; a crash in STT cannot take the app down; the server keeps the model loaded. |
| 2026-10-09 | **Language per request**, so a language change needs no restart (no 1–3 s gap). `stt.restart_on_language_change` (default false) restarts with the new `-l` for an old build that ignores the field. | §10.1 allows either; per-request is faster. |
| 2026-10-09 | `suppress_non_speech` is sent **per request**, not as the `-sns` flag: an unknown request field is ignored by older servers, an unknown flag makes them exit. `--vad` (Silero) not used: needs an extra model; our VAD already trims. | Robust across whisper.cpp versions. |
| 2026-10-09 | Health = `GET /health` (200 ready, 503 loading). 404 counts as ready (older builds have no /health but only listen after the model loads). If something already answers on the port at start, Eagle **adopts** it (Readiness says so) instead of failing to bind. | Survives an orphan from a crash. |
| 2026-10-09 | **No orphan helpers:** whisper-server and Piper workers join a Windows Job Object with `KILL_ON_JOB_CLOSE` (die even if Eagle crashes). Linux: `PR_SET_PDEATHSIG(SIGKILL)` + `nice 5` for whisper-server. Crash restart with backoff 1→30 s, max 5 per 5 min; "unknown argument", "failed to load model" and "couldn't bind" are not retried. Server output goes to `logs/whisper-server.log` (never a pipe that can fill up). | §22.5. Windows job-object code syntax-checked against mingw-w64 headers; Linux nice verified; PDEATHSIG could not be verified in the cloud sandbox (it ignores it even for a plain C test). |
| 2026-10-09 | **Capture and VAD run on the GUI thread** (deviation from §6.4 "AudioWorker thread"). 30 ms frames cost microseconds; VAD timing is sample-based, so GUI stalls do not change segmentation; `QAudioSource` buffer is 500 ms. A `QAudioSource` on a worker thread risks COM problems on Windows; GUI-thread capture is what the prototype proved on this laptop. | Simpler and proven; revisit only if the NUC shows dropouts. |
| 2026-10-09 | **VAD (§10.2) plus:** 300 ms calibration (20th percentile) when no floor is known; the floor is carried to the next Listen on the same mic; the 500 ms minimum is measured on the voiced span (not pre-roll/tail); 200 ms of quiet tail kept; forced cut after the quietest frame in the last 1 s. All in `stt.vad`. | Detects speech that starts right after Listen; drops clicks. |
| 2026-10-09 | **Resampling:** any mic format/channels → 16 kHz mono Int16. Box-filter decimation (cheap anti-alias), linear interpolation when upsampling. | 48 kHz stereo float is the usual WASAPI mix format. |
| 2026-10-09 | **Bias prompt** is built when a request is *sent*: `stt.bias_terms` (≤150 chars) + tail of the transcript so far (≤120). Unit-pack terms replace `bias_terms` in M4. | Includes earlier segments even if this one was queued before they came back. |
| 2026-10-09 | **Auto language:** the first segment uses whisper auto-detect (`verbose_json`); its language sticks for the rest of that Listen (queued segments are switched too). An Urdu-script result with `urdu_script_as_hindi` is **redone as Hindi** before later segments (order kept). The reply language then comes from `LanguageService` on the final text (typed questions too). | Hindi speech often comes back in Urdu script; teachers need Devanagari. |
| 2026-10-09 | **Hinglish:** `min_hits` 2 (Appendix E.3 said 3), `min_tokens` 3, ratio 0.25; lexicon adds ka/ki/ke/ko/se/mein/hota. | "photosynthesis kya hai" has only two lexicon words. |
| 2026-10-09 | Language combo gains **Auto**; it starts at `stt.language_mode` (default auto). Only English and Hindi can be reply languages until the prompt layer supports more; other scripts reply in English with a note. | §10.5; M1 prompt layer is EN/HI. |
| 2026-10-09 | **UI:** Listen / Stop listening / Transcribing… button with a level meter; the input box is read-only while listening and editable after; Enter while listening = stop, then send the final text; Space toggles Listen when the box is empty or not focused; PageDown = Listen, PageUp = Ask (clicker). Listen during a turn is a **barge-in** (stops generation and speech first). Header toggle "Listen" (was unwired) renamed **Voice on/off** and wired to speech output. | §10.3, §19.2. |
| 2026-10-09 | **Mic mute while speaking:** `TurnSpeechController::outputActiveChanged` (any audio queued or playing, including fillers and test sounds, and the gaps between sentences). While active, captured audio is dropped and an open segment is abandoned. | No self-transcription. |
| 2026-10-09 | **Root cause of "whisper.cpp was not found":** `Paths::whisperModel(name)` only fell back to `stt.fallback_model` when no name was passed, and MainWindow passed `stt.model` (`ggml-small-q5_1.bin`, not installed). Paths is now the single source for whisper-server and the model, always with fallback. Prototype `speechinput.*` removed. | M3 bug. |
| 2026-10-09 | **Config migration:** missing default keys are added at every depth on load and saved; existing values are never changed. | Pending item since M2 (`tts.volume` etc.). |
| 2026-10-09 | **Fillers fix:** the resource was at `:/data/lang/data/lang/fillers.json`, so `:/data/lang/fillers.json` never loaded and fillers were silently off. Fixed with a qrc alias (same for `hinglish.json`). | Found while adding hinglish.json. |
| 2026-10-09 | Built and unit-tested on **Linux only** (Ubuntu 24.04, GCC 13, Qt 6.9.1 built from source, no audio backend). Not yet built on Windows. | §0 rule 14. |

## M2

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-10-09 | **TTS:** One **persistent** `piper.exe` per voice (EN/HI); each sentence is one `--json-input` line on stdin; output WAV path read from **stdout** (line-delimited, flushed per job). Worker restarts on crash or `length_scale` change. F12 shows turn first/warm synth ms and worker cold/warm ms. `PiperHttpBackend` reserved if stdout piping fails on NUC (not needed on dev laptop). | §11.1. |
| 2026-10-09 | **Fillers:** `fillers.json` pre-synthesised to `<data>/cache/fillers/<voice>/<ls>/<sha1>.wav` at startup; play at `filler_after_ms` if no answer audio yet; excluded from T1; no repeat back-to-back; skipped in `Practice` mode. | §11.4. |
| 2026-10-09 | **Hindi voice:** `hi_IN-priyamvada-medium` synthesizes successfully with the legacy C++ Piper binary (`--json-input`, `length_scale` 1.15). | Verified on dev laptop 2026-10-09. |
| 2026-10-09 | **STT paths:** `Paths::whisperModel` tries primary + `stt.fallback_model` under `tools/whisper/models/` (fixes “whisper not found” when config asks for `ggml-small` but only `ggml-base.bin` is installed). | M2 path fix; M3 owns STT pipeline. |
| 2026-10-09 | **Qwen vs llama cache (bench-cache-turn):** `eagle-clm` ~**152** new prompt tokens/turn vs `llama3.2:3b` ~**23** — Qwen re-evaluates prior assistant text each turn. Revisit at M10 benchmark: Doubt without assistant history, or `/api/generate` + owned template. | §9.3 NUC latency budget. |
| 2026-10-09 | Per-turn timestamps §18.3 in memory + F12; SQLite logging deferred to M7. | M2. |
| 2026-10-09 | **Playback:** Default `audio.backend` is **`mediaplayer`**: `SpeechQueuePlayer` plays Piper WAV files with two `QMediaPlayer`s sharing one `QAudioOutput` (prototype `piperspeaker.cpp` approach). Pause between sentences is `QTimer` (`tts.sentence_silence_s`, default 0.2 s), not PCM silence. Volume is `QAudioOutput::setVolume(tts.volume)`. The `QAudioSink` + `PcmConverter` path is unwired (`audio.backend=sink` logs a warning and still uses mediaplayer). Reason: WASAPI format, speed, clipping, and idle-gap bugs; a full answer still played in ~2 s. | M2 TTS. |

## M1

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-10-08 | `think:false` when `/api/show` lists `thinking`, model name matches `eagle-clm` / `qwen3`, or the model was remembered after a thinking-only stream retry. If the first stream chunk has `thinking` but no `content`, abort and retry once with `think:false`. UI uses `message.content` only. Startup aux serial; warm-up non-streaming. | Fixes Doubt “…” hang; spec §9.1. |
| 2026-10-09 | **Prefix cache metrics:** Ollama’s `prompt_eval_count` is **total** prompt tokens; subtract `prompt_eval_cached_count` for **new** eval (matches server.log “/ N tokens” on cache hits). F12 HIT/MISS uses that delta vs estimated new (prior assistant + current doubt turn). Eagle `messages[]` history is byte-stable turn-to-turn (unit test). **No unit:** omit doubt “link to concept” line; do not inject placeholder concept text. Qwen 3.5 may log “context checkpoint” lines; KV reuse still shows high `cached n_tokens` in server.log when the app prefix is stable. | §9.3; user GPU/NUC latency. |
| 2026-10-08 | Default models: `eagle-clm` (from `qwen3.5:4b-q4_K_M` Modelfile) and fallback `qwen3.5:2b-q4_K_M`. | User-installed set; fits 8 GB RAM. |
| 2026-10-08 | M1 doubt flow uses **no unit pack** (`unit_card_none.txt`) until M4; concept placeholder `Learning and growth`. | Curriculum layer is M4; doubt still uses A.4.1 shape. |
| 2026-10-08 | `ServiceSupervisor` does not yet auto-launch Ollama on connection refused (errors surface in UI only). | M1 scope; §9.1 start logic deferred to M2/M11 packaging. |
| 2026-10-08 | `CacheWarmer`, `OpenAiCompatBackend`, and SQLite turn logging not in M1. | Per §23 M1 scope; warm-up runs at startup via `OllamaBackend::warmUp`. |
| 2026-10-08 | Legacy Python reference path is `legacy_reference/` (not `New-discussion-eleven/`). | User repo layout. |
| 2026-10-08 | Prompt QRC aliases use `:/prompts/<name>.txt` (no double `prompts/` prefix). | Fix template load from resources. |

## M0

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-10-08 | Keep prototype STT/TTS in `src/` until M2–M3 reorganise under `stt/`, `tts/`. | Smaller M0 diff. |
| 2026-10-08 | `ServiceSupervisor` only probes Ollama over HTTP in M0; whisper-server deferred. | Milestone scope. |
| 2026-10-08 | Built on **Windows (MinGW)**; Ubuntu build not verified in M0/M1 commits. | §0 rule 14. |
