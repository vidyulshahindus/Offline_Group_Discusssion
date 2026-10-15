# EagleGD

**Eagle Group Discussion** — a Qt 6 / C++ desktop app for classroom use with local [Ollama](https://ollama.com) LLMs, curriculum-aligned unit packs (IB / CLM), speech input (whisper.cpp server), and speech output (Piper).

Built for Windows (MinGW) as the primary dev target; Linux presets exist in `CMakePresets.json` but are not fully verified on every milestone.

## What works today (milestones)

| Area | Status |
|------|--------|
| **M0–M1** | App shell, Ollama streaming, prompts, history, warm-up, metrics overlay |
| **M2** | Sentence streaming TTS, fillers, soft word limit (spoken text = on-screen text) |
| **M3** | Listen / VAD / whisper-server STT, barge-in, mic level, language lock |
| **M4a** | Unit packs, unit/topic/period UI, system prompt unit card, safe Ollama aux queue |
| **M4b** | BM25 `Retriever` — optional `Notes:` in doubt turns from unit knowledge |

Planned next: CLM discussion modes (M5+). See `docs/EAGLE_BUILD_SPEC.md` and `docs/CHANGELOG.md`.

## Repository layout

```text
src/                 Application (app, ui, llm, core, stt, tts, curriculum)
resources/           Config defaults, prompts, curriculum JSON, QRC assets
model/               Ollama Modelfile for eagle-clm (weights live in Ollama’s store)
docs/                CHANGELOG, DECISIONS, build spec
tests/unit/          Qt Test targets (run via CTest)
tools/               Local whisper-server, Piper, models (not in git — see below)
CMakeLists.txt
CMakePresets.json
```

## Prerequisites

1. **Git** — https://git-scm.com/download/win  
2. **Ollama** — https://ollama.com/download (default `http://127.0.0.1:11434`)  
3. **Qt 6.9+** with **MinGW 64-bit**, **CMake**, **Ninja** (Qt Online Installer)  
4. **LLM** — create/pull a model; defaults in `resources/config/eagle_config.defaults.json` use **`eagle-clm`** (see [Ollama model](#ollama-model)).  
5. **whisper-server** + GGML model under `tools/whisper/` (see [Speech tools](#speech-tools-not-in-git))  
6. **Piper** + voice ONNX under `tools/piper/` (see [Speech tools](#speech-tools-not-in-git))

## Build (Windows, CMake presets)

Set Qt on your PATH or pass `CMAKE_PREFIX_PATH` once per configure.

**PowerShell** (adjust Qt path if needed):

```powershell
cd "C:\Users\softw\Past_Projects\Eagle Group Discussion"
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.9.1\mingw_64"
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.9.1\mingw_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;$env:PATH"

cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release
ctest --preset windows-mingw-release --output-on-failure
```

Executable:

```text
build\mingw-release\EagleGD.exe
```

### Deploy Qt DLLs (run outside Qt Creator)

```powershell
C:\Qt\6.9.1\mingw_64\bin\windeployqt.exe .\build\mingw-release\EagleGD.exe
```

Keep a `tools/` folder next to the exe (or under `../../tools` from `build/mingw-release`) so whisper-server and Piper resolve.

### Qt Creator

Open `CMakeLists.txt`, kit **Desktop Qt 6.9.x MinGW 64-bit**, configure, build, run.

## Ollama model

Weights are **not** in this repo. They live under `%USERPROFILE%\.ollama\models\` after you pull/create models.

Example profile (from `model/Modelfile.eagle-clm`):

```powershell
ollama pull qwen3.5:4b-q4_K_M
ollama create eagle-clm -f model/Modelfile.eagle-clm
ollama list
```

Default config model name: `eagle-clm` (override in user config under the app data dir or a copied `eagle_config.json`).

## Speech tools (not in git)

`.gitignore` excludes large binaries. After clone, install locally:

| Component | Typical path |
|-----------|----------------|
| whisper-server | `tools/whisper/Release/whisper-server.exe` (+ DLLs in same folder) |
| Whisper model | `tools/whisper/models/ggml-small-q5_1.bin` or `ggml-base.bin` |
| Piper | `tools/piper/piper/piper.exe` |
| English voice | `tools/piper/voices/en_US-lessac-medium.onnx` + `.json` |
| Hindi voice | per `resources/config/eagle_config.defaults.json` `tts.voice_hi` |

Download hints: [whisper.cpp releases](https://github.com/ggml-org/whisper.cpp/releases), [Piper releases](https://github.com/rhasspy/piper/releases), [Piper voices on Hugging Face](https://huggingface.co/rhasspy/piper-voices).

The app supervises **whisper-server** on `stt.port` (default **8178**), not legacy `whisper-cli` per utterance.

## Configuration

Defaults: `resources/config/eagle_config.defaults.json`.  
Runtime overrides: copy to your data directory (see `Paths::dataDir()` / Readiness panel) as documented in `docs/EAGLE_BUILD_SPEC.md`.

**Do not commit:** `.env`, `key.json`, API keys, `logs/`, `tts_cache/`, `generated_speech/`. See `SECURITY.md`.

## Tests

```powershell
ctest --preset windows-mingw-release --output-on-failure
```

Eleven unit test executables (prompt builder, Ollama backend mock, STT/VAD, curriculum, retriever, etc.).

## Documentation

- `docs/CHANGELOG.md` — milestone history  
- `docs/DECISIONS.md` — architecture decisions  
- `docs/EAGLE_BUILD_SPEC.md` — full product spec  
- `docs/ARCHITECTURE.md` — high-level map  

## Upload to GitHub

Your repo is already a **local git** history (`master`, latest commits through **M4b**). There is **no `origin` remote** until you add one.

### 1. Create an empty GitHub repository

1. Sign in at https://github.com  
2. **New repository**  
3. Name (example): `EagleGD` or `eagle-group-discussion`  
4. Choose **Private** (recommended for school work) or **Public**  
5. **Do not** add a README, license, or `.gitignore` (you already have them locally)  
6. Copy the HTTPS URL, e.g. `https://github.com/<your-username>/<repo>.git`

### 2. Review before push

```powershell
cd "C:\Users\softw\Past_Projects\Eagle Group Discussion"
git status
git log -3 --oneline
```

Confirm `git status` is clean and that you are **not** about to add:

- `tools/` binaries, `build/`, `.env`, keys, `New-discussion-eleven/`, personal folders listed in `.gitignore`

If you changed README / SECURITY locally, commit first:

```powershell
git add README.md SECURITY.md LICENSE .gitignore
git commit -m "Add GitHub-oriented README, security notes, and license."
```

### 3. Add remote and push

```powershell
git branch -M main
git remote add origin https://github.com/<your-username>/<repo>.git
git push -u origin main
```

Use a **Personal Access Token** as the password if GitHub prompts (Settings → Developer settings → Personal access tokens), or sign in via **Git Credential Manager** / **GitHub Desktop**.

### 4. Alternative: GitHub Desktop

1. Install https://desktop.github.com  
2. **File → Add local repository** → select this folder  
3. **Publish repository** → choose name and visibility  

### 5. After push

- Add a short **description** on GitHub (“Qt 6 classroom assistant with Ollama + curriculum unit packs”).  
- Optionally enable **Dependabot** / **branch protection** for collaboration.  
- Do **not** put Ollama API keys in the repo; Ollama is local by default.

## Troubleshooting

| Issue | What to try |
|-------|-------------|
| CMake cannot find Qt | Set `CMAKE_PREFIX_PATH` to `...\Qt\6.9.x\mingw_64` |
| Missing DLL at runtime | Run `windeployqt` on `EagleGD.exe` |
| Ollama unreachable | Start Ollama; check `http://127.0.0.1:11434` |
| STT / TTS not ready | Open Readiness panel; fix `tools/whisper` and `tools/piper` paths |
| Warm-up / unit change issues | See `docs/DECISIONS.md` M4a aux queue notes |

## Third-party licenses

Ollama, whisper.cpp, Piper, Qt, and model weights have their own licenses. This repository’s license applies only to EagleGD source here; see `LICENSE`.
