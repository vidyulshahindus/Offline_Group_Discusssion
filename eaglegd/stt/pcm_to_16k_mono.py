from __future__ import annotations

import struct
from array import array

from PySide6.QtMultimedia import QAudioFormat


class PcmTo16kMono:
    def __init__(self) -> None:
        self._format = QAudioFormat()

    def set_input_format(self, fmt: QAudioFormat) -> None:
        self._format = fmt

    @staticmethod
    def _sample_at(data: bytes, offset: int, sample_format: QAudioFormat.SampleFormat) -> float:
        if sample_format == QAudioFormat.SampleFormat.UInt8:
            value = data[offset]
            return (value - 128) / 128.0
        if sample_format == QAudioFormat.SampleFormat.Int16:
            (value,) = struct.unpack_from("<h", data, offset)
            return value / 32768.0
        if sample_format == QAudioFormat.SampleFormat.Int32:
            (value,) = struct.unpack_from("<i", data, offset)
            return value / 2147483648.0
        if sample_format == QAudioFormat.SampleFormat.Float:
            (value,) = struct.unpack_from("<f", data, offset)
            return float(value)
        return 0.0

    def convert(self, raw: bytes) -> bytes:
        channels = max(1, self._format.channelCount())
        bytes_per_sample = max(1, self._format.bytesPerSample())
        frame_bytes = bytes_per_sample * channels
        if frame_bytes <= 0 or len(raw) < frame_bytes:
            return b""

        fmt = self._format.sampleFormat()
        frame_count = len(raw) // frame_bytes
        mono: list[float] = []
        for frame in range(frame_count):
            base = frame * frame_bytes
            total = 0.0
            for ch in range(channels):
                off = base + ch * bytes_per_sample
                total += self._sample_at(raw, off, fmt)
            mono.append(total / channels)

        src_rate = max(1, self._format.sampleRate())
        dst_count = max(1, round(len(mono) * (16000.0 / src_rate)))
        out = array("h", [0] * dst_count)
        for i in range(dst_count):
            position = i * (src_rate / 16000.0)
            index = min(int(position), len(mono) - 1)
            nxt = min(index + 1, len(mono) - 1)
            fraction = position - index
            sample = mono[index] * (1.0 - fraction) + mono[nxt] * fraction
            sample = max(-1.0, min(1.0, sample))
            out[i] = int(sample * 32767.0)
        return out.tobytes()
