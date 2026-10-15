from __future__ import annotations

import math
import struct
from dataclasses import dataclass

from PySide6.QtMultimedia import QAudioFormat


@dataclass
class ConversionStats:
    clipped_samples: int = 0
    limiter_engaged: int = 0
    peak: float = 0.0
    peak_pre: float = 0.0
    rms: float = 0.0
    peak_db: float = -120.0
    rms_db: float = -120.0
    out_frames: int = 0
    out_bytes: int = 0


def sample_format_name(fmt: QAudioFormat.SampleFormat) -> str:
    mapping = {
        QAudioFormat.SampleFormat.UInt8: "UInt8",
        QAudioFormat.SampleFormat.Int16: "Int16",
        QAudioFormat.SampleFormat.Int32: "Int32",
        QAudioFormat.SampleFormat.Float: "Float",
    }
    return mapping.get(fmt, "Unknown")


def source_format_22050_mono_int16() -> QAudioFormat:
    fmt = QAudioFormat()
    fmt.setSampleRate(22050)
    fmt.setChannelCount(1)
    fmt.setSampleFormat(QAudioFormat.SampleFormat.Int16)
    return fmt


def mix_format_48000_stereo_float() -> QAudioFormat:
    fmt = QAudioFormat()
    fmt.setSampleRate(48000)
    fmt.setChannelCount(2)
    fmt.setSampleFormat(QAudioFormat.SampleFormat.Float)
    return fmt


def frame_count_for_mono_int16(pcm: bytes) -> int:
    return len(pcm) // 2


def expected_usecs(source_frames: int, source_rate: int) -> int:
    if source_rate <= 0:
        return 0
    return source_frames * 1_000_000 // source_rate


def _int16_mono_to_float(pcm: bytes) -> list[float]:
    out: list[float] = []
    for i in range(0, len(pcm), 2):
        (value,) = struct.unpack_from("<h", pcm, i)
        out.append(value / 32768.0)
    return out


def _resample_linear(input_samples: list[float], src_rate: int, dst_rate: int) -> list[float]:
    if not input_samples:
        return []
    if src_rate == dst_rate:
        return list(input_samples)
    dst_count = max(1, round(len(input_samples) * dst_rate / src_rate))
    out: list[float] = []
    for i in range(dst_count):
        pos = i * (src_rate / dst_rate)
        idx = min(int(pos), len(input_samples) - 1)
        nxt = min(idx + 1, len(input_samples) - 1)
        frac = pos - idx
        sample = input_samples[idx] * (1.0 - frac) + input_samples[nxt] * frac
        out.append(max(-1.0, min(1.0, sample)))
    return out


def soft_limit(sample: float, knee: float = 0.9, ceiling: float = 0.75) -> float:
    abs_s = abs(sample)
    if abs_s <= knee:
        return sample
    sign = 1.0 if sample >= 0 else -1.0
    t = (abs_s - knee) / max(1e-6, 1.0 - knee)
    return sign * (knee + (ceiling - knee) * math.tanh(t * 3.0))


def convert_mono_int16(
    src_mono_int16: bytes,
    src_format: QAudioFormat,
    dst_format: QAudioFormat,
    gain: float = 0.7,
    stats: ConversionStats | None = None,
) -> bytes:
    st = stats or ConversionStats()
    mono = _int16_mono_to_float(src_mono_int16)
    src_rate = max(1, src_format.sampleRate())
    dst_rate = max(1, dst_format.sampleRate())
    resampled = _resample_linear(mono, src_rate, dst_rate)
    channels = max(1, dst_format.channelCount())
    interleaved: list[float] = []
    peak_pre = 0.0
    acc = 0.0
    for sample in resampled:
        scaled = sample * gain
        peak_pre = max(peak_pre, abs(scaled))
        limited = soft_limit(scaled)
        if limited != scaled:
            st.limiter_engaged += 1
        if abs(limited) >= 1.0:
            st.clipped_samples += 1
        st.peak = max(st.peak, abs(limited))
        acc += limited * limited
        for _ in range(channels):
            interleaved.append(limited)
    st.peak_pre = peak_pre
    if resampled:
        st.rms = math.sqrt(acc / len(resampled))
    st.peak_db = 20.0 * math.log10(max(st.peak, 1e-9))
    st.rms_db = 20.0 * math.log10(max(st.rms, 1e-9))

    fmt = dst_format.sampleFormat()
    if fmt == QAudioFormat.SampleFormat.Float:
        out = struct.pack(f"<{len(interleaved)}f", *interleaved)
    elif fmt == QAudioFormat.SampleFormat.Int16:
        packed = [int(max(-1.0, min(1.0, s)) * 32767) for s in interleaved]
        out = struct.pack(f"<{len(packed)}h", *packed)
    else:
        out = struct.pack(f"<{len(interleaved)}f", *interleaved)
    st.out_frames = len(interleaved) // channels
    st.out_bytes = len(out)
    return out


def write_wav(path: str, pcm: bytes, fmt: QAudioFormat) -> bool:
    from pathlib import Path

    rate = fmt.sampleRate()
    ch = fmt.channelCount()
    sample_fmt = fmt.sampleFormat()
    if sample_fmt == QAudioFormat.SampleFormat.Float:
        bits = 16
        audio_format = 1
        body = b""
        for i in range(0, len(pcm), 4):
            (val,) = struct.unpack_from("<f", pcm, i)
            body += struct.pack("<h", int(max(-1.0, min(1.0, val)) * 32767))
    else:
        bits = 16
        audio_format = 1
        body = pcm
    header = bytearray()
    header.extend(b"RIFF")
    header.extend(struct.pack("<I", 36 + len(body)))
    header.extend(b"WAVEfmt ")
    header.extend(struct.pack("<I", 16))
    header.extend(struct.pack("<H", audio_format))
    header.extend(struct.pack("<H", ch))
    header.extend(struct.pack("<I", rate))
    header.extend(struct.pack("<I", rate * ch * bits // 8))
    header.extend(struct.pack("<H", ch * bits // 8))
    header.extend(struct.pack("<H", bits))
    header.extend(b"data")
    header.extend(struct.pack("<I", len(body)))
    try:
        Path(path).write_bytes(bytes(header) + body)
        return True
    except OSError:
        return False
