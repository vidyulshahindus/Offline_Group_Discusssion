# Ollama Chat (Qt 6)

A ChatGPT-style desktop chat client for local [Ollama](https://ollama.com) models, built with **Qt 6** and **C++** on Windows.

## Features

- Dark chat UI with streaming replies
- Model picker: **ornith:9b** (default) and **qwen3.5:4b**
- Word limit options: 50 / 75 / 100 / 125 / 150 / 200
- Speech-to-text with **whisper.cpp** (microphone picker + live-ish transcription)
- Text-to-speech with **Piper** (speaker picker + Listen toggle)
- New chat, Stop generating / Stop speaking, Enter to send, Shift+Enter for newline
- Exit confirmation

## Repository layout

```text
src/                 Qt C++ sources
resources/           styles and Qt resources
tools/whisper/       whisper.cpp (binaries + model — download locally)
tools/piper/         Piper TTS (binaries + voice — download locally)
CMakeLists.txt
README.md
```

Large model/voice binaries are **not** meant to be committed to GitHub (see `.gitignore`). Download them after clone.

---

## Before you create the `.exe` (dependencies)

Install these on Windows first.

### 1. Git (for GitHub)

- Download: https://git-scm.com/download/win  
- During setup, keep **“Git from the command line and also from 3rd-party software”**.

### 2. Ollama (required to chat)

1. Install from https://ollama.com/download  
2. Confirm the service is running (tray icon / `http://127.0.0.1:11434`).  
3. Pull models you will use:

```powershell
ollama pull ornith:9b
ollama pull qwen3.5:4b
```

4. Check:

```powershell
ollama list
ollama ps
```

Do **not** run a second `ollama serve` if Ollama is already running (port `11434` will already be in use).

### 3. Qt 6 (required to build)

Install with [Qt Online Installer](https://www.qt.io/download-qt-installer):

- **Qt 6.9.1** → **MinGW 64-bit** (matches this README), or  
- **Qt 6.9.3** → **MSVC 2022 64-bit**

Also install from the installer:

- Qt Creator (optional but convenient)
- CMake and Ninja (under Qt Tools), or use system CMake/Ninja
- MinGW 13.1 (if using the MinGW kit)

Typical paths used below:

- Qt: `C:\Qt\6.9.1\mingw_64`
- MinGW: `C:\Qt\Tools\mingw1310_64`
- CMake: `C:\Qt\Tools\CMake_64\bin`
- Ninja: `C:\Qt\Tools\Ninja`

### 4. whisper.cpp + Piper (required for Mic / Listen)

After cloning the repo, place tools like this:

```text
tools/whisper/Release/whisper-cli.exe
tools/whisper/models/ggml-base.bin
tools/piper/piper/piper.exe          (+ DLLs / espeak-ng-data next to it)
tools/piper/voices/en_US-lessac-medium.onnx
tools/piper/voices/en_US-lessac-medium.onnx.json
```

**whisper.cpp (Windows x64 binary + model)**

1. Download `whisper-bin-x64.zip` from  
   https://github.com/ggml-org/whisper.cpp/releases  
2. Extract so `whisper-cli.exe` is under `tools/whisper/Release/`.  
3. Download model:

```powershell
New-Item -ItemType Directory -Force -Path tools\whisper\models | Out-Null
Invoke-WebRequest `
  -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin" `
  -OutFile "tools\whisper\models\ggml-base.bin"
```

**Piper (Windows binary + English voice)**

1. Download `piper_windows_amd64.zip` from  
   https://github.com/rhasspy/piper/releases/tag/2023.11.14-2  
2. Extract into `tools/piper/` so you get `tools/piper/piper/piper.exe`.  
3. Download voice:

```powershell
New-Item -ItemType Directory -Force -Path tools\piper\voices | Out-Null
Invoke-WebRequest `
  -Uri "https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_US/lessac/medium/en_US-lessac-medium.onnx" `
  -OutFile "tools\piper\voices\en_US-lessac-medium.onnx"
Invoke-WebRequest `
  -Uri "https://huggingface.co/rhasspy/piper-voices/resolve/v1.0.0/en/en_US/lessac/medium/en_US-lessac-medium.onnx.json" `
  -OutFile "tools\piper\voices\en_US-lessac-medium.onnx.json"
```

### 5. Windows permissions

- Allow **microphone** access for desktop apps:  
  **Settings → Privacy & security → Microphone**
- Speakers/headphones should appear in Windows sound settings (Piper uses the selected output device).

---

## Build the `.exe` (Windows, MinGW Qt 6.9.1)

From **PowerShell** in the project root:

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.9.1\mingw_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja"
cd <path-to-this-repo>
cmake -G Ninja -DCMAKE_PREFIX_PATH=C:/Qt/6.9.1/mingw_64 -B build
cmake --build build
```

Output:

```text
build\OllamaChat.exe
```

### Build with Qt Creator

1. Open `CMakeLists.txt`.  
2. Choose kit **Desktop Qt 6.9.1 MinGW 64-bit** (or your MSVC kit).  
3. Configure → Build → Run.

---

## After the `.exe` is created

### 1. Deploy Qt runtime next to the exe (required to run outside Qt Creator)

```powershell
C:\Qt\6.9.1\mingw_64\bin\windeployqt.exe .\build\OllamaChat.exe
```

This copies Qt DLLs and plugins (including **Multimedia**) beside `OllamaChat.exe`. Without this step, double-clicking the exe on another PC (or even this PC without Qt on PATH) often fails with missing DLL errors.

### 2. Keep `tools/` reachable

The app looks for whisper/Piper relative to the exe, for example:

- `../tools/whisper/...` when running from `build/`
- or `tools/...` next to the exe

If you copy only `OllamaChat.exe` to another folder, also copy:

- the `windeployqt` DLLs/plugins folder contents  
- the `tools/` tree (whisper + piper)

### 3. Start Ollama, then the app

```powershell
# Ollama should already be running after install
ollama list
.\build\OllamaChat.exe
```

### 4. First-run checklist in the UI

1. Model dropdown: `ornith:9b` or `qwen3.5:4b`  
2. Word limit as needed  
3. Mic dropdown → pick input → **Mic** → speak → **Stop**  
4. Speaker dropdown → pick output → keep **Listen** on to hear Piper  
5. If devices are missing, click **↻** to refresh  

### 5. Optional: zip a portable folder

For sharing a built Windows build (not source):

```text
OllamaChat/
  OllamaChat.exe
  Qt DLLs + platforms/ + multimedia/ ...   (from windeployqt)
  tools/
    whisper/
    piper/
```

Recipients still need **Ollama + models** installed separately.

---

## Upload this project to GitHub

### A. Create an empty repo on GitHub

1. Sign in at https://github.com  
2. **New repository**  
3. Name it (example: `ollama-chat-qt`)  
4. Leave it **empty** (no README / no license if you already have local files)  
5. Copy the repo URL, e.g. `https://github.com/<you>/<repo>.git`

### B. Install Git for Windows (if `git` is not recognized)

Download: https://git-scm.com/download/win  
Open a **new** PowerShell after install and check:

```powershell
git --version
```

### C. Do not upload secrets

Before the first commit, confirm these are ignored / removed:

- `.env`, API keys, `utility/key.json`  
- `build/`  
- Large `tools/` binaries (models, onnx, whisper Release zips)  
- Optional: exclude `New-discussion-eleven/` if that is a separate private project  

This repo’s `.gitignore` already covers common cases. If you keep `New-discussion-eleven/`, open it and **remove real API keys** before pushing.

### D. First push from PowerShell

```powershell
cd C:\Users\softw\Past_Projects\ollama

git init
git add .
git status
# Review carefully: no key.json, no .env, no huge .bin/.onnx if ignored correctly

git commit -m "Add Qt Ollama Chat desktop client for Windows."

git branch -M main
git remote add origin https://github.com/<you>/<repo>.git
git push -u origin main
```

If GitHub asks you to sign in, use a **Personal Access Token** as the password (or GitHub CLI / Git Credential Manager).

### E. Easier alternative: GitHub Desktop

1. Install https://desktop.github.com  
2. **File → Add Local Repository** → select this folder  
3. Publish repository to your GitHub account  

---

## Troubleshooting

| Problem | What to try |
|---------|-------------|
| Missing DLL when starting exe | Run `windeployqt` again on `OllamaChat.exe` |
| Chat stuck on “Generating…” | Model may be thinking/loading; wait, or click Stop; try New chat |
| Mic / Listen fails | Confirm `tools/whisper` and `tools/piper` paths; allow mic privacy |
| Cannot reach Ollama | Start Ollama app; open `http://127.0.0.1:11434` |
| `ollama serve` bind error | Ollama is already running — that is OK |
| Git not found | Install Git for Windows and reopen the terminal |

## License note

Ollama, whisper.cpp, Piper, and model weights each have their own licenses. Check those projects before redistributing binaries or models.
