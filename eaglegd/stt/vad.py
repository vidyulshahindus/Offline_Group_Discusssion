from __future__ import annotations

import math
import struct
from dataclasses import dataclass

_SAMPLE_RATE = 16000
_MIN_FLOOR_DB = -70.0
_MAX_FLOOR_DB = -25.0
_CALIBRATION_MS = 300


@dataclass
class VadConfig:
    frame_ms: int = 30
    start_threshold_db: float = 9.0
    end_hangover_ms: int = 700
    min_segment_ms: int = 500
    max_segment_ms: int = 12000
    pre_roll_ms: int = 200


@dataclass
class VadSegment:
    pcm16k: bytes
    start_ms: int = 0
    end_ms: int = 0


class Vad:
    def __init__(self, config: VadConfig | None = None) -> None:
        self._config = config or VadConfig()
        self._frame_samples = 0
        self._noise_floor_db = -50.0
        self._in_speech = False
        self._above_count = 0
        self._below_ms = 0
        self._segment_start_ms = 0
        self._stream_ms = 0
        self._pre_roll = b""
        self._segment_pcm = bytearray()
        self._max_segment_samples = 0
        self._last_frame_db = _MIN_FLOOR_DB
        self._floor_locked = False
        self._calibration_dbs: list[float] = []
        self._calibration_ms = 0
        self._init_from_config()

    def _init_from_config(self) -> None:
        self._frame_samples = max(1, self._config.frame_ms * _SAMPLE_RATE // 1000)
        self._max_segment_samples = max(
            self._frame_samples, self._config.max_segment_ms * _SAMPLE_RATE // 1000
        )
        pre_roll_samples = max(0, self._config.pre_roll_ms * _SAMPLE_RATE // 1000)
        self._pre_roll = bytes(pre_roll_samples * 2)

    def reset_session(self, inherited_noise_floor_db: float, use_inherited_floor: bool) -> None:
        self._floor_locked = use_inherited_floor
        self._noise_floor_db = (
            max(_MIN_FLOOR_DB, min(inherited_noise_floor_db, _MAX_FLOOR_DB))
            if use_inherited_floor
            else -50.0
        )
        self._in_speech = False
        self._above_count = 0
        self._below_ms = 0
        self._stream_ms = 0
        self._segment_pcm = bytearray()
        self._calibration_dbs.clear()
        self._calibration_ms = 0
        self._last_frame_db = _MIN_FLOOR_DB
        if not self._floor_locked:
            self._noise_floor_db = -50.0

    @property
    def noise_floor_db(self) -> float:
        return self._noise_floor_db

    @property
    def last_frame_db(self) -> float:
        return self._last_frame_db

    @property
    def in_speech(self) -> bool:
        return self._in_speech

    @staticmethod
    def _frame_db(samples: bytes) -> float:
        count = len(samples) // 2
        if count <= 0:
            return _MIN_FLOOR_DB
        acc = 0.0
        for i in range(0, len(samples), 2):
            (value,) = struct.unpack_from("<h", samples, i)
            v = value / 32768.0
            acc += v * v
        rms = math.sqrt(acc / count)
        return 20.0 * math.log10(max(rms, 1e-9))

    def _finalize_calibration(self) -> None:
        if not self._calibration_dbs:
            self._noise_floor_db = -50.0
        else:
            sorted_dbs = sorted(self._calibration_dbs)
            idx = max(0, int(len(sorted_dbs) * 0.20) - 1)
            self._noise_floor_db = max(
                _MIN_FLOOR_DB, min(sorted_dbs[idx], _MAX_FLOOR_DB)
            )
        self._floor_locked = True
        self._calibration_dbs.clear()

    def push_pcm16k(self, pcm16k: bytes, stream_ms_base: int) -> list[VadSegment]:
        out: list[VadSegment] = []
        if not pcm16k:
            return out
        total_samples = len(pcm16k) // 2
        offset = 0
        while offset + self._frame_samples <= total_samples:
            frame_start = offset * 2
            frame_end = frame_start + self._frame_samples * 2
            frame_bytes = pcm16k[frame_start:frame_end]
            db = self._frame_db(frame_bytes)
            self._last_frame_db = db
            frame_ms = stream_ms_base + self._stream_ms

            if not self._floor_locked:
                self._calibration_dbs.append(db)
                self._calibration_ms += self._config.frame_ms
                if self._calibration_ms >= _CALIBRATION_MS:
                    self._finalize_calibration()

            start_thresh = self._noise_floor_db + self._config.start_threshold_db
            end_thresh = start_thresh - 3.0

            if self._floor_locked and not self._in_speech:
                if db >= start_thresh:
                    self._above_count += 1
                else:
                    self._above_count = 0
                if self._above_count >= 3:
                    self._in_speech = True
                    self._segment_start_ms = frame_ms - self._config.pre_roll_ms
                    self._segment_pcm = bytearray(self._pre_roll)
                    self._below_ms = 0

            if self._floor_locked and self._in_speech:
                self._segment_pcm.extend(frame_bytes)
                if db < end_thresh:
                    self._below_ms += self._config.frame_ms
                else:
                    self._below_ms = 0
                if len(self._segment_pcm) // 2 >= self._max_segment_samples:
                    out.append(self._finalize_segment(stream_ms_base))
                elif self._below_ms >= self._config.end_hangover_ms:
                    out.append(self._finalize_segment(stream_ms_base))
            elif self._floor_locked and self._pre_roll:
                pre = bytearray(self._pre_roll)
                pre = pre[self._frame_samples * 2 :] + frame_bytes
                self._pre_roll = bytes(pre)

            offset += self._frame_samples
            self._stream_ms += self._config.frame_ms
        return out

    def _finalize_segment(self, stream_ms_base: int) -> VadSegment:
        seg = VadSegment(
            pcm16k=bytes(self._segment_pcm),
            start_ms=self._segment_start_ms,
            end_ms=stream_ms_base + self._stream_ms,
        )
        duration_ms = (len(seg.pcm16k) // 2) * 1000 // _SAMPLE_RATE
        if duration_ms < self._config.min_segment_ms:
            seg.pcm16k = b""
        self._in_speech = False
        self._above_count = 0
        self._below_ms = 0
        self._segment_pcm = bytearray()
        return seg

    def flush(self, stream_ms_base: int) -> list[VadSegment]:
        if not self._floor_locked and self._calibration_dbs:
            self._finalize_calibration()
        out: list[VadSegment] = []
        if self._in_speech and self._segment_pcm:
            duration_ms = (len(self._segment_pcm) // 2) * 1000 // _SAMPLE_RATE
            if duration_ms >= 300:
                out.append(self._finalize_segment(stream_ms_base))
            else:
                self._in_speech = False
                self._segment_pcm = bytearray()
        return out
