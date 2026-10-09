# Eagle GD — Admin guide (robot PC)

## Windows audio: disable enhancements

Eagle’s speech can sound harsh or louder than the app’s own meters if Windows boosts the Realtek output.

On the robot NUC:

1. Right-click the speaker icon → **Open Sound settings** → **Speakers (Realtek)** → **Device properties**.
2. Open **Additional device properties**.
3. **Enhancements** (or **Advanced**): turn **off** “Disable all enhancements” is inverted on some skins — the goal is **Audio enhancements off**, especially **Loudness Equalization**.
4. Apply, then use Readiness → **Test tone** and **Test voice**.

The app already applies `tts.volume` (default 0.7) and a limiter. If a recording still peaks at 1.0 while the log shows `peak 0.70`, the boost is outside Eagle (device enhancements or a second capture source).

## Speech input (whisper-server)

Eagle starts `whisper-server` itself when it opens and stops it when it closes. You do not run it by hand.

**Files Eagle needs** (in the project's `tools` folder, or next to the installed app):

| File | Where |
|---|---|
| `whisper-server.exe` and its DLLs (`whisper.dll`, `ggml*.dll`) | `tools/whisper/Release/` |
| A model: `ggml-base.bin` (faster) and/or `ggml-small-q5_1.bin` (better Hindi) | `tools/whisper/models/` |

`stt.model` is tried first, then `stt.fallback_model`. Readiness (Ctrl+Shift+R) shows which model is in use and whether the server is ready.

**Using it in class**

- Press **Listen** (or **Space**, or the clicker's PageDown), speak, then press **Stop listening** (or Space again). The text appears in the box; edit it if needed, then press Enter.
- Pressing **Enter** while listening stops listening and sends the question as soon as the text is ready.
- Pressing **Listen** while Eagle is talking stops her first.
- After each Stop the status line says how long the text took ("text ready 0.8 s after Stop"). If it is often over 2 s on the robot, switch to `ggml-base.bin` (`stt.model`) or raise `stt.threads`.
- For a Hindi lesson, choose **Hindi** in the language box: it is more reliable than **Auto**.

**If speech input does not work**

1. Readiness → *whisper-server (speech to text)* must say **OK**. If not, the row tells you what is missing; fix it and press **Refresh checks** (this also restarts a failed server).
2. Open the log folder and read `whisper-server.log` (the server's own output) and `eaglegd_<date>.log` (lines with `eagle.stt`).
3. "Port 8178 is used by another program": close the other program or change `stt.port`.
4. If nothing is transcribed, check the microphone picker and the green level bar while you speak. A quiet room plus a far-away mic may need `stt.vad.start_threshold_db` lowered from 9 to 6.

