from __future__ import annotations

import math
import struct
import tempfile
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

from PySide6.QtCore import QObject, QTimer, QUrl, Signal
from PySide6.QtMultimedia import QAudioDevice, QAudioOutput, QMediaDevices, QMediaPlayer

_ENVELOPE_HOP_MS = 50
_TEST_TONE_TURN_ID = -2


@dataclass
class _WavPcm:
    samples: bytes = b""
    sample_rate: int = 0
    channels: int = 1
    bits: int = 16
    is_float: bool = False
    duration_ms: int = 0


@dataclass
class _QueueItem:
    turn_id: int = -1
    sentence_index: int = -1
    wav_path: str = ""
    counts_toward_t1: bool = True
    delete_after: bool = True
    wav_duration_ms: int = 0
    envelope: list[float] = field(default_factory=list)


def _read_wav(path: Path) -> _WavPcm:
    wav = _WavPcm()
    if not path.is_file():
        return wav
    data = path.read_bytes()
    if len(data) < 44 or not data.startswith(b"RIFF") or data[8:12] != b"WAVE":
        return wav
    offset = 12
    data_offset = -1
    data_size = 0
    audio_format = 1
    while offset + 8 <= len(data):
        chunk_id = data[offset : offset + 4]
        (chunk_size,) = struct.unpack_from("<I", data, offset + 4)
        payload = offset + 8
        if chunk_id == b"fmt " and payload + 16 <= len(data):
            audio_format = struct.unpack_from("<H", data, payload)[0]
            wav.channels = struct.unpack_from("<H", data, payload + 2)[0]
            wav.sample_rate = struct.unpack_from("<I", data, payload + 4)[0]
            wav.bits = struct.unpack_from("<H", data, payload + 14)[0]
            wav.is_float = audio_format == 3
        elif chunk_id == b"data":
            data_offset = payload
            data_size = min(chunk_size, len(data) - payload)
            break
        offset = payload + chunk_size + (chunk_size & 1)
    if data_offset < 0 or wav.sample_rate <= 0 or wav.channels <= 0:
        return wav
    wav.samples = data[data_offset : data_offset + data_size]
    bytes_per_frame = max(1, wav.channels * (wav.bits // 8))
    frames = len(wav.samples) // bytes_per_frame
    wav.duration_ms = (frames * 1000) // wav.sample_rate
    return wav


def _rms_envelope(wav: _WavPcm) -> list[float]:
    bins: list[float] = []
    if wav.sample_rate <= 0 or not wav.samples:
        return bins
    hop = max(1, wav.sample_rate * _ENVELOPE_HOP_MS // 1000)
    ch = max(1, wav.channels)
    if wav.is_float and wav.bits == 32:
        count = len(wav.samples) // 4 // ch
        for i in range(0, count, hop):
            end = min(count, i + hop)
            acc = 0.0
            n = 0
            for f in range(i, end):
                for c in range(ch):
                    off = (f * ch + c) * 4
                    (val,) = struct.unpack_from("<f", wav.samples, off)
                    acc += val * val
                    n += 1
            bins.append(math.sqrt(acc / n) if n else 0.0)
        return bins
    if wav.bits == 16:
        count = len(wav.samples) // 2 // ch
        for i in range(0, count, hop):
            end = min(count, i + hop)
            acc = 0.0
            n = 0
            for f in range(i, end):
                for c in range(ch):
                    off = (f * ch + c) * 2
                    (val,) = struct.unpack_from("<h", wav.samples, off)
                    v = val / 32768.0
                    acc += v * v
                    n += 1
            bins.append(math.sqrt(acc / n) if n else 0.0)
    return bins


def _write_tone_wav(milliseconds: int) -> str:
    ms = max(50, milliseconds)
    rate = 22050
    n = rate * ms // 1000
    pcm = bytearray()
    for i in range(n):
        sample = int(math.sin(2.0 * math.pi * 440.0 * i / rate) * 0.4 * 32767.0)
        pcm.extend(struct.pack("<h", sample))
    header = bytearray()
    header.extend(b"RIFF")
    header.extend(struct.pack("<I", 36 + len(pcm)))
    header.extend(b"WAVEfmt ")
    header.extend(struct.pack("<I", 16))
    header.extend(struct.pack("<HHIIHH", 1, 1, rate, rate * 2, 2, 16))
    header.extend(b"data")
    header.extend(struct.pack("<I", len(pcm)))
    path = Path(tempfile.gettempdir()) / "eaglegd_tone.wav"
    path.write_bytes(bytes(header) + bytes(pcm))
    return str(path)


class SpeechQueuePlayer(QObject):
    k_test_tone_turn_id = _TEST_TONE_TURN_ID

    speech_started = Signal(int)
    sentence_started = Signal(int, int)
    speech_finished = Signal(int, bool)
    first_audio_delivered = Signal(int)
    level = Signal(float)
    playback_error = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._output = QAudioOutput(self)
        self._output.setVolume(0.7)
        default = QMediaDevices.defaultAudioOutput()
        if not default.isNull():
            self._output.setDevice(default)
        self._players = [QMediaPlayer(self), QMediaPlayer(self)]
        for i in range(2):
            self._wire_player(i)
        self._loaded_path = ["", ""]
        self._front = 0
        self._current = _QueueItem()
        self._have_current = False
        self._awaiting_end = False
        self._input_complete = False
        self._t1_emitted = False
        self._finishing = False
        self._pending: deque[_QueueItem] = deque()
        self._silence_timer = QTimer(self)
        self._silence_timer.setSingleShot(True)
        self._silence_timer.timeout.connect(self._start_front)
        self._end_of_media_ms = -1
        self._last_gap_ms = -1
        self._silence_ms = 200
        self._active_turn_id = -1
        self._destroying = False

    def _wire_player(self, index: int) -> None:
        player = self._players[index]
        player.playbackStateChanged.connect(
            lambda state, idx=index: self._on_playback_state(idx, int(state))
        )
        player.mediaStatusChanged.connect(
            lambda status, idx=index: self._on_media_status(idx, int(status))
        )
        player.positionChanged.connect(
            lambda pos, idx=index: self._on_position(idx, pos)
        )
        player.errorOccurred.connect(
            lambda _err, msg, idx=index: self._on_player_error(idx, msg)
        )

    def set_output_device(self, device: QAudioDevice) -> None:
        if not device.isNull():
            self._output.setDevice(device)

    def set_volume(self, volume: float) -> None:
        self._output.setVolume(max(0.0, min(1.0, volume)))

    def set_sentence_silence_ms(self, milliseconds: int) -> None:
        self._silence_ms = max(0, milliseconds)

    def enqueue_wav(
        self,
        turn_id: int,
        sentence_index: int,
        wav_path: str,
        counts_toward_t1: bool = True,
        delete_after_playback: bool = True,
    ) -> None:
        path = Path(wav_path)
        if not wav_path or not path.is_file():
            self.playback_error.emit("Missing WAV for playback")
            return
        if counts_toward_t1 and sentence_index >= 0:
            self.drop_fillers()
        if not self._have_current and not self._pending:
            self._t1_emitted = False
            self._input_complete = False
            self._finishing = False
            self._end_of_media_ms = -1
            self._active_turn_id = turn_id
        item = self._inspect_wav(
            wav_path, turn_id, sentence_index, counts_toward_t1, delete_after_playback
        )
        self._pending.append(item)
        self._preload_back()
        self._try_play_next()

    def mark_input_complete(self) -> None:
        self._input_complete = True
        if not self.is_playback_busy():
            self._finish_turn(False)

    def drop_fillers(self) -> None:
        kept: deque[_QueueItem] = deque()
        while self._pending:
            item = self._pending.popleft()
            if item.sentence_index < 0:
                self._release_item(item)
            else:
                kept.append(item)
        self._pending = kept
        if self._have_current and self._current.sentence_index < 0:
            self._silence_timer.stop()
            self._awaiting_end = False
            self._have_current = False
            for player in self._players:
                player.stop()
            self._release_item(self._current)
            self._current = _QueueItem()
            self._front = 1 - self._front

    def play_test_tone(self, milliseconds: int = 1000) -> None:
        self.stop()
        path = _write_tone_wav(milliseconds)
        if not path:
            self.playback_error.emit("Could not write test tone WAV")
            self.speech_finished.emit(_TEST_TONE_TURN_ID, True)
            return
        self.enqueue_wav(_TEST_TONE_TURN_ID, 0, path, False, True)
        self.mark_input_complete()

    def stop(self) -> None:
        self._silence_timer.stop()
        self._awaiting_end = False
        turn_id = (
            self._current.turn_id
            if self._have_current
            else (self._pending[0].turn_id if self._pending else self._active_turn_id)
        )
        had_work = self._have_current or bool(self._pending) or self._silence_timer.isActive()
        for player in self._players:
            player.stop()
            player.setAudioOutput(None)
            player.setSource(QUrl())
        self._loaded_path = ["", ""]
        self._have_current = False
        self._current = _QueueItem()
        self._clear_queue(True)
        self._input_complete = True
        if had_work and not self._finishing:
            self._finishing = True
            if not self._destroying:
                self.speech_finished.emit(turn_id, True)

    def is_playing(self) -> bool:
        return (
            self._have_current
            and self._players[self._front].playbackState()
            == QMediaPlayer.PlaybackState.PlayingState
        )

    def is_playback_busy(self) -> bool:
        return self._have_current or bool(self._pending) or self._silence_timer.isActive()

    @property
    def last_inter_sentence_gap_ms(self) -> int:
        return self._last_gap_ms

    def _bind_output(self, index: int) -> None:
        for i, player in enumerate(self._players):
            if i != index:
                player.setAudioOutput(None)
        self._players[index].setAudioOutput(self._output)

    def _load_player(self, index: int, path: str) -> None:
        abs_path = str(Path(path).resolve())
        if self._loaded_path[index] == abs_path:
            return
        self._players[index].setSource(QUrl.fromLocalFile(abs_path))
        self._loaded_path[index] = abs_path

    def _preload_back(self) -> None:
        if not self._pending:
            return
        back = (1 - self._front) if self._have_current else self._front
        self._load_player(back, self._pending[0].wav_path)

    def _try_play_next(self) -> None:
        if self._have_current or self._silence_timer.isActive() or not self._pending:
            if (
                not self._pending
                and self._input_complete
                and not self._have_current
                and not self._silence_timer.isActive()
            ):
                self._finish_turn(False)
            return
        delay = 0
        if self._end_of_media_ms >= 0:
            elapsed = int(datetime.now(timezone.utc).timestamp() * 1000) - self._end_of_media_ms
            delay = max(0, self._silence_ms - elapsed)
        if delay > 0:
            self._silence_timer.start(delay)
            return
        self._start_front()

    def _start_front(self) -> None:
        if self._have_current or not self._pending:
            return
        self._current = self._pending.popleft()
        self._have_current = True
        self._awaiting_end = True
        self._bind_output(self._front)
        self._load_player(self._front, self._current.wav_path)
        self._players[self._front].setPosition(0)
        self._players[self._front].play()
        self._preload_back()

    def _on_playback_state(self, index: int, state: int) -> None:
        if index != self._front or not self._have_current:
            return
        if state != int(QMediaPlayer.PlaybackState.PlayingState):
            return
        now = int(datetime.now(timezone.utc).timestamp() * 1000)
        if self._end_of_media_ms >= 0:
            self._last_gap_ms = now - self._end_of_media_ms
        duration_ms = self._players[index].duration()
        if duration_ms <= 0:
            duration_ms = self._current.wav_duration_ms
        self.sentence_started.emit(self._current.sentence_index, duration_ms)
        if self._current.counts_toward_t1 and not self._t1_emitted:
            self._t1_emitted = True
            self.speech_started.emit(self._current.turn_id)
            self.first_audio_delivered.emit(self._current.turn_id)

    def _on_media_status(self, index: int, status: int) -> None:
        if index != self._front or not self._have_current or not self._awaiting_end:
            return
        if status != int(QMediaPlayer.MediaStatus.EndOfMedia):
            return
        self._awaiting_end = False
        self._end_of_media_ms = int(datetime.now(timezone.utc).timestamp() * 1000)
        self._players[index].stop()
        self._players[index].setAudioOutput(None)
        self._release_item(self._current)
        self._have_current = False
        self._current = _QueueItem()
        self._front = 1 - self._front
        self._try_play_next()

    def _on_position(self, index: int, position_ms: int) -> None:
        if index != self._front or not self._have_current:
            return
        if not self._current.envelope:
            return
        bin_idx = position_ms // _ENVELOPE_HOP_MS
        if 0 <= bin_idx < len(self._current.envelope):
            self.level.emit(self._current.envelope[bin_idx])

    def _on_player_error(self, index: int, error: str) -> None:
        self.playback_error.emit(error)
        if index == self._front and self._have_current:
            self._awaiting_end = False
            self._release_item(self._current)
            self._have_current = False
            self._current = _QueueItem()
            self._front = 1 - self._front
            self._try_play_next()

    def _finish_turn(self, cancelled: bool) -> None:
        if self._finishing:
            return
        if not cancelled and (
            self._have_current or self._pending or self._silence_timer.isActive()
        ):
            return
        if not cancelled and not self._input_complete:
            return
        self._finishing = True
        self.speech_finished.emit(self._active_turn_id, cancelled)

    def _clear_queue(self, delete_files: bool) -> None:
        while self._pending:
            item = self._pending.popleft()
            if delete_files:
                self._release_item(item)

    def _release_item(self, item: _QueueItem) -> None:
        if item.delete_after and item.wav_path:
            Path(item.wav_path).unlink(missing_ok=True)

    def _inspect_wav(
        self,
        path: str,
        turn_id: int,
        sentence_index: int,
        counts_toward_t1: bool,
        delete_after: bool,
    ) -> _QueueItem:
        wav = _read_wav(Path(path))
        return _QueueItem(
            turn_id=turn_id,
            sentence_index=sentence_index,
            wav_path=path,
            counts_toward_t1=counts_toward_t1,
            delete_after=delete_after,
            wav_duration_ms=wav.duration_ms,
            envelope=_rms_envelope(wav),
        )

    def __del__(self) -> None:
        self._destroying = True
