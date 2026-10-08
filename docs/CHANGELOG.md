# Changelog

## M0 — Foundation (2026-10-08)

- Renamed the CMake target from `OllamaChat` to **EagleGD**; added `CMakePresets.json`.
- Added `AppConfig`, `Paths`, file logging, `ServiceSupervisor` (Ollama probe + tool path checks), and startup **Readiness** dialog.
- Split the monolithic `MainWindow` into `MainWindow` + `ChatPanel`; removed hard-coded user tool paths.
- Prototype chat (Ollama streaming, whisper-cli STT, Piper TTS) retained.
