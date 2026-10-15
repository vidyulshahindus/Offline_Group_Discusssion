from __future__ import annotations

from PySide6.QtCore import QObject, QIODevice, Signal
from PySide6.QtMultimedia import QAudio, QAudioDevice, QAudioFormat, QAudioSource, QMediaDevices

from eaglegd.stt.pcm_to_16k_mono import PcmTo16kMono


class AudioCapture(QObject):
    pcm16k_ready = Signal(bytes)
    error_occurred = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._device = QMediaDevices.defaultAudioInput()
        self._input_format = QAudioFormat()
        self._source: QAudioSource | None = None
        self._capture: QIODevice | None = None
        self._converter = PcmTo16kMono()
        self._capturing = False

    def microphones(self) -> list[QAudioDevice]:
        return list(QMediaDevices.audioInputs())

    def set_microphone(self, device: QAudioDevice) -> None:
        if self._capturing:
            return
        if not device.isNull():
            self._device = device

    @property
    def device(self) -> QAudioDevice:
        return self._device

    @property
    def input_format(self) -> QAudioFormat:
        return self._input_format

    def is_capturing(self) -> bool:
        return self._capturing

    def source_state(self):
        return self._source.state() if self._source else QAudio.State.StoppedState

    def source_error(self):
        return self._source.error() if self._source else QAudio.Error.NoError

    def start(self) -> None:
        if self._capturing:
            return
        if self._device.isNull():
            self.error_occurred.emit("No microphone selected.")
            return

        preferred = QAudioFormat()
        preferred.setSampleRate(16000)
        preferred.setChannelCount(1)
        preferred.setSampleFormat(QAudioFormat.SampleFormat.Int16)
        self._input_format = (
            preferred
            if self._device.isFormatSupported(preferred)
            else self._device.preferredFormat()
        )
        self._converter.set_input_format(self._input_format)

        self._source = QAudioSource(self._device, self._input_format, self)
        self._capture = self._source.start()
        if not self._capture:
            err = (
                "start() returned null IO device"
                if self._source.error() == QAudio.Error.NoError
                else str(int(self._source.error()))
            )
            self._source.deleteLater()
            self._source = None
            self.error_occurred.emit(f"Could not open the selected microphone: {err}")
            return
        self._capture.readyRead.connect(self._on_audio_ready)
        self._capturing = True

    def stop(self) -> None:
        if not self._capturing:
            return
        if self._capture:
            self._on_audio_ready()
        if self._source:
            self._source.stop()
            self._source.deleteLater()
            self._source = None
        self._capture = None
        self._capturing = False

    def _on_audio_ready(self) -> None:
        if not self._capture:
            return
        raw = bytes(self._capture.readAll())
        pcm = self._converter.convert(raw)
        if pcm:
            self.pcm16k_ready.emit(pcm)
