# Eagle GD — Admin guide (robot PC)

## Windows audio: disable enhancements

Eagle’s speech can sound harsh or louder than the app’s own meters if Windows boosts the Realtek output.

On the robot NUC:

1. Right-click the speaker icon → **Open Sound settings** → **Speakers (Realtek)** → **Device properties**.
2. Open **Additional device properties**.
3. **Enhancements** (or **Advanced**): turn **off** “Disable all enhancements” is inverted on some skins — the goal is **Audio enhancements off**, especially **Loudness Equalization**.
4. Apply, then use Readiness → **Test tone** and **Test voice**.

The app already applies `tts.volume` (default 0.7) and a limiter. If a recording still peaks at 1.0 while the log shows `peak 0.70`, the boost is outside Eagle (device enhancements or a second capture source).
