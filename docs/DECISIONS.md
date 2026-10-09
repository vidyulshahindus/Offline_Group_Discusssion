# Architecture decisions

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
