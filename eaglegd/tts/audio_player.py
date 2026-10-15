from __future__ import annotations

import math
import struct
import tempfile
from pathlib import Path

from PySide6.QtCore import QObject, QElapsedTimer, QTimer, Signal
from PySide6.QtMultimedia import QAudioDevice, QAudioFormat, QAudioSink, QMediaDevices

from eaglegd.tts import pcm_converter
from eaglegd.tts.pcm_converter import ConversionStats
from eaglegd.tts.pcm_queue_device import PcmQueueDevice

_TEST_TONE_TURN_ID = -2


class AudioPlayer(QObject):
    speech_started = Signal(int)
    speech_finished = Signal(int, bool)
    first_audio_delivered = Signal(int)
    playback_error = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._device = PcmQueueDevice(self)
        self._sink: QAudioSink | None = None
        self._source_format = pcm_converter.source_format_22050_mono_int16()
        self._requested_format = QAudioFormat()
        self._playback_format = QAudioFormat()
        self._output_device = QAudioDevice()
        self._play_wall = QElapsedTimer()
        self._active_turn_id = -1
        self._cancelled = False
        self._first_audio_signalled = False
        self._waiting_for_drain = False
        self._input_complete = False
        self._last_processed_usecs = -1
        self._stall_ticks = 0
        self._input_gain = 0.7
        self._source_frames_this_play = 0
        self._converted_bytes_this_play = 0
        self._processed_usecs_at_play_start = 0
        self._processed_accumulated = 0
        self._pulled_at_segment_start = 0
        self._pulled_at_play_start = 0
        self._converted_dump = bytearray()
        self._device.readyRead.connect(self._kick_sink)

    def set_output_device(self, device: QAudioDevice) -> None:
        if not device.isNull():
            self._output_device = device
        else:
            self._output_device = QAudioDevice()
        if self._sink and not self._waiting_for_drain:
            self._restart_sink()

    def set_volume(self, volume: float) -> None:
        if self._sink:
            self._sink.setVolume(volume)

    def set_input_gain(self, gain: float) -> None:
        if 0.0 < gain <= 2.0:
            self._input_gain = gain

    @property
    def input_gain(self) -> float:
        return self._input_gain

    def mark_input_complete(self) -> None:
        self._input_complete = True
        self._device.set_stream_open(False)
        self._schedule_drain_check()

    def process_speech_pcm(
        self, src22050_mono_int16: bytes, stats: ConversionStats | None = None
    ) -> bytes:
        dst = (
            self._playback_format
            if self._playback_format.isValid()
            else pcm_converter.mix_format_48000_stereo_float()
        )
        return pcm_converter.convert_mono_int16(
            src22050_mono_int16, self._source_format, dst, self._input_gain, stats
        )

    def enqueue_pcm(
        self,
        turn_id: int,
        sentence_index: int,
        pcm: bytes,
        counts_toward_t1: bool = True,
    ) -> None:
        del sentence_index
        if not pcm:
            self.playback_error.emit("Empty audio data from speech engine")
            return
        self._cancelled = False
        self._waiting_for_drain = True
        self._device.set_stream_open(True)
        new_turn = self._active_turn_id != turn_id
        if new_turn:
            if self._active_turn_id != -1:
                self._finish_current_turn(True)
                self._waiting_for_drain = True
            self._active_turn_id = turn_id
            if counts_toward_t1:
                self._first_audio_signalled = False
            self.speech_started.emit(turn_id)
        if not self._prepare_sink():
            self._finish_current_turn(False)
            return
        if new_turn:
            self._begin_play_stats(turn_id)
        else:
            self._input_complete = False

        src_frames = pcm_converter.frame_count_for_mono_int16(pcm)
        stats = ConversionStats()
        converted = self.process_speech_pcm(pcm, stats)
        self._source_frames_this_play += src_frames
        self._converted_bytes_this_play += len(converted)
        self._append_converted_dump(converted)
        self._device.enqueue(converted)
        self._kick_sink()
        if counts_toward_t1 and not self._first_audio_signalled:
            self._first_audio_signalled = True
            self.first_audio_delivered.emit(turn_id)
        self._schedule_drain_check()

    def insert_silence_ms(self, ms: int) -> None:
        src_samples = self._source_format.sampleRate() * ms // 1000
        silence = bytes(src_samples * 2)
        converted = self.process_speech_pcm(silence)
        self._source_frames_this_play += src_samples
        self._converted_bytes_this_play += len(converted)
        self._append_converted_dump(converted)
        self._device.enqueue(converted)
        self._kick_sink()
        self._schedule_drain_check()

    def play_test_tone(self, milliseconds: int = 1000) -> None:
        rate = self._source_format.sampleRate()
        samples = max(1, rate * milliseconds // 1000)
        pcm = bytearray()
        for i in range(samples):
            env = 0.3 if (i < samples // 20 or i > samples - samples // 20) else 1.0
            val = int(math.sin(2.0 * math.pi * 440.0 * i / rate) * 0.45 * env * 32767.0)
            pcm.extend(struct.pack("<h", val))
        self.enqueue_pcm(_TEST_TONE_TURN_ID, 0, bytes(pcm), False)
        self.mark_input_complete()
        QTimer.singleShot(milliseconds + 2500, self, self._test_tone_timeout)

    def _test_tone_timeout(self) -> None:
        if self._active_turn_id != _TEST_TONE_TURN_ID:
            return
        self._finish_current_turn(False)

    def stop(self) -> None:
        self._cancelled = True
        self._device.clear_queue()
        if self._sink:
            self._sink.stop()
            self._sink = None
        self._first_audio_signalled = False
        self._finish_current_turn(True)

    def is_playing(self) -> bool:
        return bool(self._sink and self._sink.state() == QAudio.State.ActiveState)

    def is_playback_busy(self) -> bool:
        return self._waiting_for_drain or self._device.has_data() or self.is_playing()

    def processed_usecs(self) -> int:
        return max(0, self._processed_accumulated + self._segment_played_usecs())

    def playback_format(self) -> QAudioFormat:
        return self._playback_format

    def _pick_playback_format(self, device: QAudioDevice) -> QAudioFormat:
        preferred = device.preferredFormat()
        if preferred.isValid() and preferred.sampleRate() >= 44100:
            return preferred
        return pcm_converter.mix_format_48000_stereo_float()

    def _prepare_sink(self) -> bool:
        if self._sink:
            return True
        device = (
            self._output_device
            if not self._output_device.isNull()
            else QMediaDevices.defaultAudioOutput()
        )
        if device.isNull():
            self.playback_error.emit("No audio output device")
            return False
        self._requested_format = self._pick_playback_format(device)
        self._sink = QAudioSink(device, self._requested_format, self)
        self._playback_format = self._sink.format()
        if not self._playback_format.isValid():
            self._playback_format = self._requested_format
        self._sink.setVolume(1.0)
        self._device.set_bytes_per_frame(max(1, self._playback_format.bytesPerFrame()))
        self._sink.stateChanged.connect(lambda _state: self._schedule_drain_check())
        return True

    def _kick_sink(self) -> None:
        if not self._sink or not self._device.has_data():
            return
        state = self._sink.state()
        if state == QAudio.State.ActiveState:
            return
        if state in (QAudio.State.IdleState, QAudio.State.SuspendedState):
            self._processed_accumulated += self._segment_played_usecs()
            self._pulled_at_segment_start = self._device.bytes_pulled
            self._sink.stop()
        self._sink.start(self._device)

    def _restart_sink(self) -> None:
        self._device.clear_queue()
        if self._sink:
            self._sink.stop()
            self._sink = None

    def _begin_play_stats(self, turn_id: int) -> None:
        del turn_id
        self._source_frames_this_play = 0
        self._converted_bytes_this_play = 0
        self._converted_dump.clear()
        self._processed_usecs_at_play_start = 0
        self._processed_accumulated = 0
        self._pulled_at_segment_start = self._device.bytes_pulled
        self._pulled_at_play_start = self._device.bytes_pulled
        self._play_wall.start()
        self._input_complete = False
        self._device.set_stream_open(True)
        self._last_processed_usecs = -1
        self._stall_ticks = 0

    def _segment_played_usecs(self) -> int:
        bpf = max(1, self._playback_format.bytesPerFrame())
        rate = max(1, self._playback_format.sampleRate())
        pulled = max(0, self._device.bytes_pulled - self._pulled_at_segment_start)
        pulled_us = pulled * 1_000_000 // (rate * bpf)
        clock_us = self._sink.processedUSecs() if self._sink else 0
        return max(clock_us, pulled_us)

    def _finish_current_turn(self, cancelled: bool) -> None:
        if self._active_turn_id == -1 and not self._waiting_for_drain:
            return
        turn_id = self._active_turn_id
        self._waiting_for_drain = False
        self._input_complete = True
        self._active_turn_id = -1
        if turn_id != -1:
            self.speech_finished.emit(turn_id, cancelled)

    def _converted_dump_path(self) -> Path:
        return Path(tempfile.gettempdir()) / "eaglegd_piper" / "converted.wav"

    def _append_converted_dump(self, pcm: bytes) -> None:
        self._converted_dump.extend(pcm)
        pcm_converter.write_wav(
            str(self._converted_dump_path()), bytes(self._converted_dump), self._playback_format
        )

    def _schedule_drain_check(self) -> None:
        QTimer.singleShot(80, self, self._check_playback_drained)

    def _check_playback_drained(self) -> None:
        if self._cancelled or not self._waiting_for_drain or self._active_turn_id == -1:
            return
        if not self._sink:
            return
        if not self._input_complete:
            if self._device.has_data():
                self._kick_sink()
            self._schedule_drain_check()
            return
        if self._device.has_data():
            self._kick_sink()
            self._schedule_drain_check()
            return
        state = self._sink.state()
        processed = self.processed_usecs()
        if state == QAudio.State.ActiveState:
            if processed == self._last_processed_usecs:
                self._stall_ticks += 1
            else:
                self._stall_ticks = 0
            self._last_processed_usecs = processed
            if self._stall_ticks < 4:
                self._schedule_drain_check()
                return
        self._first_audio_signalled = False
        self._finish_current_turn(False)
