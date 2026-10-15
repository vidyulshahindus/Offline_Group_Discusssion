from __future__ import annotations

import logging
import math
import struct
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path

from PySide6.QtCore import QDateTime, QStandardPaths, QTimer, QUrl, Signal, QObject
from PySide6.QtMultimedia import QAudioDevice, QAudioOutput, QMediaDevices, QMediaPlayer

_log = logging.getLogger("eagle.speech")

_ENVELOPE_HOP_MS = 50


@dataclass
class _WavPcm:
    samples: bytes = b""
    sample_rate: int = 0
    channels: int = 1
    bits: int = 16
    duration_ms: int = 0


@dataclass
class _Item:
    turnId: int = -1
    sentenceIndex: int = -1
    wavPath: str = ""
    countsTowardT1: bool = True
    deleteAfter: bool = True
    wavDurationMs: int = 0
    envelope: list[float] = field(default_factory=list)


def _read_wav(path: str) -> _WavPcm:
    wav = _WavPcm()
    data = Path(path).read_bytes()
    if len(data) < 44 or not data.startswith(b"RIFF") or data[8:12] != b"WAVE":
        return wav
    offset = 12
    data_offset = -1
    data_size = 0
    while offset + 8 <= len(data):
        chunk_id = data[offset : offset + 4]
        chunk_size = struct.unpack_from("<I", data, offset + 4)[0]
        payload = offset + 8
        if chunk_id == b"fmt " and payload + 16 <= len(data):
            wav.channels = struct.unpack_from("<H", data, payload + 2)[0]
            wav.sample_rate = struct.unpack_from("<I", data, payload + 4)[0]
            wav.bits = struct.unpack_from("<H", data, payload + 14)[0]
        elif chunk_id == b"data":
            data_offset = payload
            data_size = min(chunk_size, len(data) - payload)
            break
        offset = payload + chunk_size + (chunk_size & 1)
    if data_offset < 0 or wav.sample_rate <= 0:
        return wav
    wav.samples = data[data_offset : data_offset + data_size]
    bytes_per_frame = max(1, wav.channels * (wav.bits // 8))
    frames = len(wav.samples) // bytes_per_frame
    wav.duration_ms = (frames * 1000) // wav.sample_rate
    return wav


def _rms_envelope(wav: _WavPcm) -> list[float]:
    bins: list[float] = []
    if wav.sample_rate <= 0 or not wav.samples or wav.bits != 16:
        return bins
    hop = max(1, wav.sample_rate * _ENVELOPE_HOP_MS // 1000)
    ch = max(1, wav.channels)
    samples = struct.unpack(f"<{len(wav.samples)//2}h", wav.samples)
    frames = len(samples) // ch
    for i in range(0, frames, hop):
        end = min(frames, i + hop)
        acc = 0.0
        n = 0
        for f in range(i, end):
            for c in range(ch):
                v = samples[f * ch + c] / 32768.0
                acc += v * v
                n += 1
        bins.append(math.sqrt(acc / n) if n else 0.0)
    return bins


class SpeechQueuePlayer(QObject):
    kTestToneTurnId = -2

    speechStarted = Signal(int)
    sentenceStarted = Signal(int, int)
    speechFinished = Signal(int, bool)
    firstAudioDelivered = Signal(int)
    level = Signal(float)
    playbackError = Signal(str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._output = QAudioOutput(self)
        self._output.setVolume(0.7)
        default = QMediaDevices.defaultAudioOutput()
        if not default.isNull():
            self._output.setDevice(default)
        self._players = [QMediaPlayer(self), QMediaPlayer(self)]
        for index, player in enumerate(self._players):
            player.playbackStateChanged.connect(
                lambda state, idx=index: self._on_playback_state(idx, state)
            )
            player.mediaStatusChanged.connect(
                lambda status, idx=index: self._on_media_status(idx, status)
            )
            player.positionChanged.connect(
                lambda pos, idx=index: self._on_position(idx, pos)
            )
            player.errorOccurred.connect(
                lambda _err, msg, idx=index: self._on_player_error(idx, msg)
            )
        self._silence_timer = QTimer(self)
        self._silence_timer.setSingleShot(True)
        self._silence_timer.timeout.connect(self._start_front)
        self._loaded_path = ["", ""]
        self._front = 0
        self._current = _Item()
        self._have_current = False
        self._pending: deque[_Item] = deque()
        self._silence_ms = 0
        self._input_complete = False
        self._finishing = False
        self._destroying = False
        self._t1_emitted = False
        self._active_turn_id = -1
        self._end_of_media_ms = -1
        self._awaiting_end = False
        self._last_gap_ms = 0

    def setOutputDevice(self, device: QAudioDevice) -> None:
        if not device.isNull():
            self._output.setDevice(device)
            _log.info("output device %s", device.description())

    def setVolume(self, volume: float) -> None:
        self._output.setVolume(max(0.0, min(1.0, volume)))

    def setSentenceSilenceMs(self, milliseconds: int) -> None:
        self._silence_ms = max(0, milliseconds)

    def enqueueWav(
        self,
        turn_id: int,
        sentence_index: int,
        wav_path: str,
        counts_toward_t1: bool = True,
        delete_after_playback: bool = True,
    ) -> None:
        if not wav_path or not Path(wav_path).is_file():
            _log.warning(
                "enqueueWav missing %s turn %s idx %s", wav_path, turn_id, sentence_index
            )
            self.playbackError.emit("Missing WAV for playback")
            return
        if counts_toward_t1 and sentence_index >= 0:
            self.dropFillers()
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

    def markInputComplete(self) -> None:
        self._input_complete = True
        if not self.isPlaybackBusy():
            self._finish_turn(False)

    def dropFillers(self) -> None:
        kept: deque[_Item] = deque()
        while self._pending:
            item = self._pending.popleft()
            if item.sentenceIndex < 0:
                self._release_item(item)
            else:
                kept.append(item)
        self._pending = kept
        if self._have_current and self._current.sentenceIndex < 0:
            self._silence_timer.stop()
            self._awaiting_end = False
            self._have_current = False
            for player in self._players:
                player.stop()
            self._release_item(self._current)
            self._current = _Item()
            self._front = 1 - self._front

    def playTestTone(self, milliseconds: int = 1000) -> None:
        self.stop()
        path = self._write_tone_wav(milliseconds)
        if not path:
            self.playbackError.emit("Could not write test tone WAV")
            self.speechFinished.emit(self.kTestToneTurnId, True)
            return
        self.enqueueWav(self.kTestToneTurnId, 0, path, False, True)
        self.markInputComplete()

    def stop(self) -> None:
        self._silence_timer.stop()
        self._awaiting_end = False
        turn_id = (
            self._current.turnId
            if self._have_current
            else (self._pending[0].turnId if self._pending else self._active_turn_id)
        )
        had_work = self._have_current or bool(self._pending) or self._silence_timer.isActive()
        for player in self._players:
            player.stop()
            player.setAudioOutput(None)
            player.setSource(QUrl())
        self._loaded_path = ["", ""]
        self._have_current = False
        self._current = _Item()
        self._clear_queue(True)
        self._input_complete = True
        if had_work and not self._finishing:
            self._finishing = True
            if not self._destroying:
                self.speechFinished.emit(turn_id, True)

    def isPlaying(self) -> bool:
        return (
            self._have_current
            and self._players[self._front].playbackState()
            == QMediaPlayer.PlaybackState.PlayingState
        )

    def isPlaybackBusy(self) -> bool:
        return self._have_current or bool(self._pending) or self._silence_timer.isActive()

    def lastInterSentenceGapMs(self) -> int:
        return self._last_gap_ms

    def _bind_output(self, index: int) -> None:
        for i, player in enumerate(self._players):
            player.setAudioOutput(self._output if i == index else None)

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
        self._load_player(back, self._pending[0].wavPath)

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
            elapsed = QDateTime.currentMSecsSinceEpoch() - self._end_of_media_ms
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
        self._load_player(self._front, self._current.wavPath)
        self._players[self._front].setPosition(0)
        self._players[self._front].play()
        self._preload_back()

    def _on_playback_state(self, index: int, state: QMediaPlayer.PlaybackState) -> None:
        if index != self._front or not self._have_current:
            return
        if state == QMediaPlayer.PlaybackState.PlayingState:
            now = QDateTime.currentMSecsSinceEpoch()
            if self._end_of_media_ms >= 0:
                self._last_gap_ms = now - self._end_of_media_ms
            duration_ms = self._players[index].duration()
            if duration_ms <= 0:
                duration_ms = self._current.wavDurationMs
            self.sentenceStarted.emit(self._current.sentenceIndex, int(duration_ms))
            if self._current.countsTowardT1 and not self._t1_emitted:
                self._t1_emitted = True
                self.speechStarted.emit(self._current.turnId)
                self.firstAudioDelivered.emit(self._current.turnId)
            return
        # FFmpeg backend sometimes skips EndOfMedia and only goes Stopped.
        if state == QMediaPlayer.PlaybackState.StoppedState and self._awaiting_end:
            self._advance_after_current_clip()

    def _on_media_status(self, index: int, status: QMediaPlayer.MediaStatus) -> None:
        if index != self._front or not self._have_current or not self._awaiting_end:
            return
        if status != QMediaPlayer.MediaStatus.EndOfMedia:
            return
        self._advance_after_current_clip(stop_player=True)

    def _advance_after_current_clip(self, stop_player: bool = False) -> None:
        if not self._have_current or not self._awaiting_end:
            return
        index = self._front
        self._awaiting_end = False
        self._end_of_media_ms = QDateTime.currentMSecsSinceEpoch()
        if stop_player:
            self._players[index].stop()
        self._players[index].setAudioOutput(None)
        self._release_item(self._current)
        self._have_current = False
        self._current = _Item()
        self._front = 1 - self._front
        self._try_play_next()

    def _on_position(self, index: int, position_ms: int) -> None:
        if index != self._front or not self._have_current or not self._current.envelope:
            return
        bin_idx = int(position_ms / _ENVELOPE_HOP_MS)
        if 0 <= bin_idx < len(self._current.envelope):
            self.level.emit(self._current.envelope[bin_idx])

    def _on_player_error(self, index: int, error: str) -> None:
        _log.warning("QMediaPlayer error %s %s", index, error)
        self.playbackError.emit(error)
        if index == self._front and self._have_current:
            self._awaiting_end = False
            self._release_item(self._current)
            self._have_current = False
            self._current = _Item()
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
        self.speechFinished.emit(self._active_turn_id, cancelled)

    def _clear_queue(self, delete_files: bool) -> None:
        while self._pending:
            item = self._pending.popleft()
            if delete_files:
                self._release_item(item)

    def _release_item(self, item: _Item) -> None:
        if item.deleteAfter and item.wavPath:
            Path(item.wavPath).unlink(missing_ok=True)

    def _inspect_wav(
        self,
        path: str,
        turn_id: int,
        sentence_index: int,
        counts_toward_t1: bool,
        delete_after: bool,
    ) -> _Item:
        wav = _read_wav(path)
        return _Item(
            turnId=turn_id,
            sentenceIndex=sentence_index,
            wavPath=path,
            countsTowardT1=counts_toward_t1,
            deleteAfter=delete_after,
            wavDurationMs=wav.duration_ms,
            envelope=_rms_envelope(wav),
        )

    @staticmethod
    def _write_tone_wav(milliseconds: int) -> str:
        ms = max(50, milliseconds)
        rate = 22050
        n = rate * ms // 1000
        pcm = bytearray()
        for i in range(n):
            sample = int(math.sin(2.0 * math.pi * 440.0 * i / rate) * 0.4 * 32767.0)
            pcm.extend(struct.pack("<h", sample))
        header = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt "
        header += struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16)
        header += b"data" + struct.pack("<I", len(pcm))
        temp = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.TempLocation)
        path = Path(temp or "/tmp") / "eaglegd_tone.wav"
        path.write_bytes(header + pcm)
        return str(path)
