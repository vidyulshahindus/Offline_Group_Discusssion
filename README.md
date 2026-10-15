# EagleGD

**Eagle Group Discussion** — a Python / PySide6 desktop app for classroom use with local [Ollama](https://ollama.com) LLMs, curriculum-aligned unit packs (IB / CLM), speech input (whisper.cpp server), and speech output (Piper).

Primary target: **Linux** (Ubuntu/Debian). The previous Qt 6 / C++ tree is preserved under `cpp_archive/` for reference.

## What works today (milestones)

| Area | Status |
|------|--------|
| **M0–M1** | App shell, Ollama streaming, prompts, history, warm-up, metrics overlay |
| **M2** | Sentence streaming TTS, fillers, soft word limit |
| **M3** | Listen / VAD / whisper-server STT, barge-in, mic level, language lock |
| **M4a** | Unit packs, unit/topic/period UI, system prompt unit card |
| **M4b** | BM25 `Retriever` — optional `Notes:` in doubt turns from unit knowledge |

## Repository layout

```text
eaglegd/             Python package (app, ui, llm, core, stt, tts, curriculum)
resources/           Config defaults, prompts, curriculum JSON, styles
model/               Ollama Modelfile for eagle-clm
docs/                CHANGELOG, DECISIONS, build spec
tests/python/        pytest suite
scripts/             Linux run / build helpers
cpp_archive/         Former C++ / CMake sources (reference only)
legacy_reference/    Older Python prototype (reference only)
pyproject.toml
requirements.txt
```

## Prerequisites (Linux)

1. **Python 3.10+** and `python3-venv`
2. **Ollama** — https://ollama.com/download (default `http://127.0.0.1:11434`)
3. **Qt Multimedia system libs** (Ubuntu/Debian example):

   ```bash
   sudo apt update
   sudo apt install python3-venv python3-pip \
     libpulse0 libxcb-cursor0 libegl1 libgl1 libxkbcommon0 \
     libxcb-xinerama0 libnss3
   ```

4. **LLM** — create/pull a model; defaults use **`eagle-clm`** (see below)
5. **whisper-server** + GGML model under `tools/whisper/` (optional until you use Listen)
6. **Piper** + voice ONNX under `tools/piper/` (optional until you use TTS)

## Build & run (Linux)

```bash
cd /path/to/IB_Group_Discussion
./scripts/build_linux.sh
./scripts/run_linux.sh
```

Or manually:

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
pip install -e .
python -m eaglegd.app.main
# optional: --config /path/to/eagle_config.json
```

## Ollama model

```bash
ollama pull qwen3.5:4b-q4_K_M
ollama create eagle-clm -f model/Modelfile.eagle-clm
ollama list
```

Default config model name: `eagle-clm` (override in user config under the app data dir).

## Speech tools (not in git)

`.gitignore` excludes large binaries. After clone, install locally:

| Component | Typical path (Linux) |
|-----------|----------------------|
| whisper-server | `tools/whisper/whisper-server` (or `tools/whisper/bin/`) |
| Whisper model | `tools/whisper/models/ggml-small-q5_1.bin` or `ggml-base.bin` |
| Piper | `tools/piper/piper/piper` |
| English voice | `tools/piper/voices/en_US-lessac-medium.onnx` + `.json` |
| Hindi voice | per `resources/config/eagle_config.defaults.json` `tts.voices.hi` |

Download hints: [whisper.cpp releases](https://github.com/ggml-org/whisper.cpp/releases), [Piper releases](https://rhasspy.github.io/piper-samples/), [Piper voices](https://huggingface.co/rhasspy/piper-voices).

## Configuration

Defaults: `resources/config/eagle_config.defaults.json`.  
Runtime overrides: `~/.config/IndusInternational/Eagle GD/eagle_config.json` (Qt `AppConfigLocation`).

**Do not commit:** `.env`, `key.json`, API keys, `logs/`, `tts_cache/`, `generated_speech/`. See `SECURITY.md`.

## Tests

```bash
source .venv/bin/activate
pytest -q
```

## Documentation

- `docs/CHANGELOG.md` — milestone history
- `docs/DECISIONS.md` — architecture decisions
- `docs/EAGLE_BUILD_SPEC.md` — full product spec
- `docs/ARCHITECTURE.md` — high-level map

## Troubleshooting

| Issue | What to try |
|-------|-------------|
| `ModuleNotFoundError: PySide6` | `source .venv/bin/activate && pip install -r requirements.txt` |
| Qt platform plugin / EGL errors | Install the apt packages listed above; try `QT_QPA_PLATFORM=xcb` |
| Ollama unreachable | Start Ollama; check `http://127.0.0.1:11434` |
| STT / TTS not ready | Open Readiness panel (Ctrl+Shift+R); fix `tools/whisper` and `tools/piper` paths |

## Third-party licenses

Ollama, whisper.cpp, Piper, Qt (PySide6), and model weights have their own licenses. This repository’s license applies only to EagleGD source here; see `LICENSE`.
