# Architecture decisions

## M0

| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-10-08 | Keep prototype LLM/STT/TTS classes in `src/` until M1–M3 reorganise them under `llm/`, `stt/`, `tts/`. | Smaller M0 diff; behaviour unchanged. |
| 2026-10-08 | `ServiceSupervisor` only probes Ollama over HTTP in M0; whisper-server and robot bridge supervision deferred. | Matches milestone scope; Readiness still reports resolved whisper/Piper paths via `Paths`. |
| 2026-10-08 | Default LLM names in config: `eagle-clm` with fallback `qwen3.5:2b-q4_K_M` (replacing `ornith:9b` in the UI). | Spec §4.2: 9B models do not fit 8 GB RAM. |
| 2026-10-08 | Built only on **Windows (MinGW)** in this environment; Ubuntu build not verified in M0 commit. | Per §0 rule 14: recorded here until a Linux kit is available. |
