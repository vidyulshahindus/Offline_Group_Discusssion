from __future__ import annotations

import struct


def pcm16k_mono_to_wav(pcm16le: bytes) -> bytes:
    header = bytearray()
    header.extend(b"RIFF")
    header.extend(struct.pack("<I", 36 + len(pcm16le)))
    header.extend(b"WAVE")
    header.extend(b"fmt ")
    header.extend(struct.pack("<I", 16))
    header.extend(struct.pack("<H", 1))
    header.extend(struct.pack("<H", 1))
    header.extend(struct.pack("<I", 16000))
    header.extend(struct.pack("<I", 16000 * 2))
    header.extend(struct.pack("<H", 2))
    header.extend(struct.pack("<H", 16))
    header.extend(b"data")
    header.extend(struct.pack("<I", len(pcm16le)))
    return bytes(header) + pcm16le
