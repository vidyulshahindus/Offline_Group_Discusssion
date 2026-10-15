from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QObject, QProcess, QStandardPaths, QUrl, Signal, Slot
from PySide6.QtMultimedia import QAudioDevice, QAudioOutput, QMediaDevices, QMediaPlayer


class PiperSpeaker(QObject):
    speakingChanged = Signal(bool)
    errorOccurred = Signal(str)
    statusChanged = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._player = QMediaPlayer(self)
        self._audio = QAudioOutput(self)
        self._audio.setVolume(1.0)
        self._player.setAudioOutput(self._audio)
        self._player.playbackStateChanged.connect(self._on_playback_state_changed)

        self._piper: QProcess | None = None
        self._wav_path = ""
        self._piper_exe_override = ""
        self._voice_onnx_override = ""
        self._playing = False

    def isBusy(self) -> bool:
        return self._piper is not None or self._playing

    def speakers(self) -> list[QAudioDevice]:
        return list(QMediaDevices.audioOutputs())

    def setSpeaker(self, device: QAudioDevice) -> None:
        if not device.isNull():
            self._audio.setDevice(device)

    def setPiperPaths(self, executable: str | Path, voice_onnx_path: str | Path) -> None:
        self._piper_exe_override = str(executable) if executable else ""
        self._voice_onnx_override = str(voice_onnx_path) if voice_onnx_path else ""

    def speak(self, text: str) -> None:
        spoken = text.strip()
        if not spoken:
            return
        self.stop()

        executable = self._piper_executable()
        model = self._voice_model_path()
        if not executable or not model:
            self.errorOccurred.emit(
                "Piper was not found. Expected tools/piper/piper/piper.exe and "
                "tools/piper/voices/en_US-lessac-medium.onnx."
            )
            return

        temp = QStandardPaths.writableLocation(
            QStandardPaths.StandardLocation.TempLocation
        )
        self._wav_path = str(Path(temp) / "ollama-chat-piper.wav")
        Path(self._wav_path).unlink(missing_ok=True)

        self._piper = QProcess(self)
        self._piper.setWorkingDirectory(str(Path(executable).parent))
        self._piper.setProcessChannelMode(QProcess.ProcessChannelMode.SeparateChannels)
        self._piper.finished.connect(self._on_piper_finished)
        args = ["--model", model, "--output_file", self._wav_path]
        self._piper.start(executable, args)
        if not self._piper.waitForStarted(3000):
            message = self._piper.errorString()
            self._piper.deleteLater()
            self._piper = None
            self.errorOccurred.emit(f"Could not start Piper: {message}")
            return

        self._piper.write(spoken.encode("utf-8"))
        self._piper.write(b"\n")
        self._piper.closeWriteChannel()
        self.speakingChanged.emit(True)
        self.statusChanged.emit("Speaking…")

    def stop(self) -> None:
        if self._piper is not None:
            self._piper.disconnect(self)
            self._piper.kill()
            self._piper.deleteLater()
            self._piper = None
        if self._player.playbackState() != QMediaPlayer.PlaybackState.StoppedState:
            self._player.stop()
        self._player.setSource(QUrl())
        self._playing = False
        self.speakingChanged.emit(False)

    @Slot(int, QProcess.ExitStatus)
    def _on_piper_finished(self, exit_code: int, _status: QProcess.ExitStatus) -> None:
        if self._piper is None:
            return
        stderr_text = bytes(self._piper.readAllStandardError()).decode(
            "utf-8", errors="replace"
        ).strip()
        self._piper.deleteLater()
        self._piper = None

        if exit_code != 0 or not Path(self._wav_path).is_file():
            self.speakingChanged.emit(False)
            self.statusChanged.emit("Ready")
            if stderr_text:
                self.errorOccurred.emit(stderr_text)
            else:
                self.errorOccurred.emit(
                    f"Piper could not create speech (exit {exit_code})."
                )
            return
        self._play_wav(self._wav_path)

    def _play_wav(self, path: str) -> None:
        self._playing = True
        self._player.setSource(QUrl.fromLocalFile(path))
        self._player.play()
        self.speakingChanged.emit(True)
        self.statusChanged.emit("Speaking…")

    def _on_playback_state_changed(self) -> None:
        if not self._playing:
            return
        if self._player.playbackState() == QMediaPlayer.PlaybackState.StoppedState:
            self._playing = False
            self.speakingChanged.emit(False)
            self.statusChanged.emit("Ready")

    def _piper_executable(self) -> str:
        if self._piper_exe_override and Path(self._piper_exe_override).is_file():
            return str(Path(self._piper_exe_override).resolve())
        return self._piper_exe_override

    def _voice_model_path(self) -> str:
        if self._voice_onnx_override and Path(self._voice_onnx_override).is_file():
            return str(Path(self._voice_onnx_override).resolve())
        return self._voice_onnx_override
