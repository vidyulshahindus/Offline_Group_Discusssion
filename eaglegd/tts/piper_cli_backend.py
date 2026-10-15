from __future__ import annotations

import json
import random
import struct
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

from PySide6.QtCore import QObject, QProcess, QTimer, Signal


@dataclass
class PiperJob:
    turn_id: int = -1
    sentence_index: int = -1
    text: str = ""
    output_path: str = ""
    queued_at_ms: int = 0


def _read_wav_pcm(path: Path) -> tuple[bytes, int, int, int]:
    data = path.read_bytes()
    if len(data) < 44 or not data.startswith(b"RIFF") or data[8:12] != b"WAVE":
        return b"", 0, 0, 0
    offset = 12
    sample_rate = 0
    channels = 0
    bits = 0
    data_offset = -1
    data_size = 0
    while offset + 8 <= len(data):
        chunk_id = data[offset : offset + 4]
        (chunk_size,) = struct.unpack_from("<I", data, offset + 4)
        payload = offset + 8
        if chunk_id == b"fmt " and payload + 16 <= len(data):
            channels = struct.unpack_from("<H", data, payload + 2)[0]
            sample_rate = struct.unpack_from("<I", data, payload + 4)[0]
            bits = struct.unpack_from("<H", data, payload + 14)[0]
        elif chunk_id == b"data":
            data_offset = payload
            data_size = min(chunk_size, len(data) - payload)
            break
        offset = payload + chunk_size + (chunk_size & 1)
    if data_offset < 0:
        return b"", 0, 0, 0
    pcm = data[data_offset : data_offset + data_size]
    return pcm, sample_rate, channels, bits


class PiperCliBackend(QObject):
    wav_ready = Signal(int, int, str, int)
    synthesis_failed = Signal(int, int, str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._executable = ""
        self._model_onnx = ""
        self._config_json = ""
        self._length_scale = 1.1
        self._output_dir = Path(tempfile.gettempdir()) / "eaglegd_piper"
        self._output_dir.mkdir(parents=True, exist_ok=True)
        self._process: QProcess | None = None
        self._queue: list[PiperJob] = []
        self._waiting_for_path = False
        self._stdout_buffer = bytearray()
        self._active_job = PiperJob()
        self._jobs_completed_on_worker = 0
        self._last_cold_synth_ms = -1
        self._last_warm_synth_ms = -1
        self._job_started_ms = 0
        self._job_timer = QTimer(self)
        self._job_timer.setSingleShot(True)
        self._job_timer.setInterval(45000)
        self._job_timer.timeout.connect(self._on_job_timeout)

    def configure(self, executable: str, model_onnx: str, length_scale: float) -> None:
        scale_changed = abs(self._length_scale - length_scale) > 0.001
        self._executable = executable
        self._model_onnx = model_onnx
        self._config_json = f"{model_onnx}.json"
        self._length_scale = length_scale
        if scale_changed and self._process:
            self.stop_worker()

    def is_configured(self) -> bool:
        return (
            bool(self._executable)
            and Path(self._executable).is_file()
            and Path(self._model_onnx).is_file()
        )

    def is_worker_running(self) -> bool:
        return bool(self._process and self._process.state() != QProcess.ProcessState.NotRunning)

    def has_pending_jobs(self) -> bool:
        return self._waiting_for_path or bool(self._queue)

    @property
    def length_scale(self) -> float:
        return self._length_scale

    @property
    def last_cold_synth_ms(self) -> int:
        return self._last_cold_synth_ms

    @property
    def last_warm_synth_ms(self) -> int:
        return self._last_warm_synth_ms

    def start_worker(self) -> None:
        if not self.is_configured() or self.is_worker_running():
            return
        self._jobs_completed_on_worker = 0
        self._process = QProcess(self)
        self._process.setProcessChannelMode(QProcess.ProcessChannelMode.SeparateChannels)
        self._process.setWorkingDirectory(str(Path(self._executable).parent))
        self._process.readyReadStandardOutput.connect(self._on_stdout_ready)
        self._process.readyReadStandardError.connect(self._on_stderr_ready)
        self._process.finished.connect(self._on_process_finished)
        self._process.errorOccurred.connect(self._on_process_error)
        args = [
            "--model",
            self._model_onnx,
            "--config",
            self._config_json,
            "--json-input",
            "--output_dir",
            str(self._output_dir),
            "--length_scale",
            f"{self._length_scale:.2f}",
            "--sentence_silence",
            "0",
        ]
        self._process.start(self._executable, args)
        if not self._process.waitForStarted(8000):
            self._process.deleteLater()
            self._process = None

    def stop_worker(self) -> None:
        self._job_timer.stop()
        self._queue.clear()
        self._waiting_for_path = False
        self._stdout_buffer.clear()
        if self._process:
            self._process.disconnect(self)
            self._process.terminate()
            self._process.waitForFinished(2000)
            if self._process.state() != QProcess.ProcessState.NotRunning:
                self._process.kill()
            self._process.deleteLater()
            self._process = None

    def synthesize(self, turn_id: int, sentence_index: int, text: str) -> None:
        if not self.is_configured() or not text.strip():
            self.synthesis_failed.emit(
                turn_id, sentence_index, "Piper not configured"
            )
            return
        job = PiperJob(
            turn_id=turn_id,
            sentence_index=sentence_index,
            text=text,
            output_path=self._next_output_path(),
            queued_at_ms=int(datetime.now(timezone.utc).timestamp() * 1000),
        )
        self._queue.append(job)
        self.start_worker()
        self._pump_queue()

    def _next_output_path(self) -> str:
        stamp = int(datetime.now(timezone.utc).timestamp() * 1000)
        return str(self._output_dir / f"job_{stamp}_{random.randint(0, 2**31 - 1)}.wav")

    def _pump_queue(self) -> None:
        if self._waiting_for_path or not self._queue or not self._process:
            return
        self._active_job = self._queue[0]
        self._waiting_for_path = True
        self._job_started_ms = int(datetime.now(timezone.utc).timestamp() * 1000)
        out_path = self._active_job.output_path.replace("\\", "/")
        payload = (
            json.dumps({"text": self._active_job.text, "output_file": out_path}, ensure_ascii=False)
            + "\n"
        ).encode("utf-8")
        written = self._process.write(payload)
        if written != len(payload):
            self._fail_front_job("Failed to write to Piper stdin")
            return
        self._job_timer.start()

    def _on_stderr_ready(self) -> None:
        if not self._process:
            return
        _ = bytes(self._process.readAllStandardError())

    def _on_job_timeout(self) -> None:
        if not self._waiting_for_path:
            return
        self._fail_front_job("Piper synthesis timed out")

    def _on_stdout_ready(self) -> None:
        if not self._process:
            return
        self._stdout_buffer.extend(bytes(self._process.readAllStandardOutput()))
        while True:
            try:
                newline = self._stdout_buffer.index(b"\n")
            except ValueError:
                break
            line = self._stdout_buffer[:newline]
            del self._stdout_buffer[: newline + 1]
            path = line.decode("utf-8", errors="replace").strip().strip('"')
            if path and self._waiting_for_path:
                self._complete_job(path)

    def _complete_job(self, wav_path: str) -> None:
        self._job_timer.stop()
        self._waiting_for_path = False
        if self._queue:
            self._queue.pop(0)
        synth_ms = int(datetime.now(timezone.utc).timestamp() * 1000) - self._job_started_ms
        self._jobs_completed_on_worker += 1
        if self._jobs_completed_on_worker == 1:
            self._last_cold_synth_ms = synth_ms
        else:
            self._last_warm_synth_ms = synth_ms

        path = Path(wav_path)
        if not path.is_file():
            alt = Path(self._active_job.output_path)
            if alt.is_file():
                path = alt
        last_copy = self._output_dir / "last.wav"
        if path.is_file():
            if last_copy.exists():
                last_copy.unlink()
            last_copy.write_bytes(path.read_bytes())
        pcm, _, _, _ = _read_wav_pcm(path)
        if not pcm or not path.is_file():
            if path.is_file():
                path.unlink(missing_ok=True)
            self.synthesis_failed.emit(
                self._active_job.turn_id,
                self._active_job.sentence_index,
                "Empty WAV from Piper",
            )
        else:
            self.wav_ready.emit(
                self._active_job.turn_id,
                self._active_job.sentence_index,
                str(path),
                synth_ms,
            )
        self._pump_queue()

    def _fail_front_job(self, error: str) -> None:
        self._job_timer.stop()
        self._waiting_for_path = False
        if not self._queue:
            return
        job = self._queue.pop(0)
        self.synthesis_failed.emit(job.turn_id, job.sentence_index, error)
        self._pump_queue()

    def _on_process_finished(self, exit_code: int, _status) -> None:
        del exit_code
        err = bytes(self._process.readAllStandardError()).decode("utf-8", errors="replace") if self._process else ""
        if self._process:
            self._process.deleteLater()
        self._process = None
        self._stdout_buffer.clear()
        if self._waiting_for_path and self._queue:
            self._fail_front_job(err or "Piper worker exited")
        self._waiting_for_path = False
        if self._queue:
            self.start_worker()
            self._pump_queue()

    def _on_process_error(self, error: QProcess.ProcessError) -> None:
        if error == QProcess.ProcessError.FailedToStart:
            self._fail_front_job("Piper failed to start")
