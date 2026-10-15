from __future__ import annotations

from collections import deque

from PySide6.QtCore import QIODevice, Signal


class PcmQueueDevice(QIODevice):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._chunks: deque[bytes] = deque()
        self._current = bytearray()
        self._bytes_pulled = 0
        self._partial_reads_dropped = 0
        self._bytes_per_frame = 1
        self._stream_open = True
        self.open(QIODevice.OpenModeFlag.ReadOnly | QIODevice.OpenModeFlag.Unbuffered)

    def set_bytes_per_frame(self, bytes_per_frame: int) -> None:
        self._bytes_per_frame = max(1, bytes_per_frame)

    def set_stream_open(self, open_: bool) -> None:
        self._stream_open = open_

    def enqueue(self, pcm: bytes) -> None:
        if not pcm:
            return
        bpf = max(1, self._bytes_per_frame)
        rem = len(pcm) % bpf
        aligned = pcm if rem == 0 else pcm + bytes(bpf - rem)
        self._chunks.append(aligned)
        self.readyRead.emit()

    def clear_queue(self) -> None:
        self._chunks.clear()
        self._current.clear()
        self._bytes_pulled = 0
        self._partial_reads_dropped = 0

    def has_data(self) -> bool:
        return self.queued_bytes() >= self._bytes_per_frame

    def queued_bytes(self) -> int:
        total = len(self._current)
        for chunk in self._chunks:
            total += len(chunk)
        return total

    @property
    def bytes_pulled(self) -> int:
        return self._bytes_pulled

    @property
    def partial_reads_dropped(self) -> int:
        return self._partial_reads_dropped

    def isSequential(self) -> bool:
        return True

    def bytesAvailable(self) -> int:
        raw = self.queued_bytes() + super().bytesAvailable()
        bpf = max(1, self._bytes_per_frame)
        return raw - (raw % bpf)

    def atEnd(self) -> bool:
        if self._stream_open:
            return False
        return self.queued_bytes() < self._bytes_per_frame

    def readData(self, max_size: int) -> bytes:
        bpf = max(1, self._bytes_per_frame)
        max_size -= max_size % bpf
        if max_size < bpf:
            self._partial_reads_dropped += 1
            return b""

        written = bytearray()
        while len(written) + bpf <= max_size:
            if len(self._current) < bpf and self._chunks:
                if not self._current:
                    self._current = bytearray(self._chunks.popleft())
                else:
                    self._current.extend(self._chunks.popleft())
                continue
            if len(self._current) < bpf:
                break
            room = max_size - len(written)
            to_copy = min(room, len(self._current))
            to_copy -= to_copy % bpf
            if to_copy < bpf:
                break
            written.extend(self._current[:to_copy])
            del self._current[:to_copy]
        self._bytes_pulled += len(written)
        return bytes(written)

    def writeData(self, data: bytes) -> int:
        del data
        return -1
