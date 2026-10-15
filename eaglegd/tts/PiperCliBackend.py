from __future__ import annotations

import json
import logging
import random
from dataclasses import dataclass
from pathlib import Path

from PySide6.QtCore import QDateTime, QProcess, QStandardPaths, QTimer, Signal, QObject

_log = logging.getLogger("eagle.piper")


@dataclass
class _Job:
    turnId: int = -1
    sentenceIndex: int = -1
    text: str = ""
    outputPath: str = ""
    queuedAtMs: int = 0


class PiperCliBackend(QObject):
    wavReady = Signal(int, int, str, int)
    synthesisFailed = Signal(int, int, str)

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        temp = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.TempLocation)
        self._output_dir = Path(temp or "/tmp") / "eaglegd_piper"
        self._output_dir.mkdir(parents=True, exist_ok=True)
        self._executable = ""
        self._model_onnx = ""
        self._config_json = ""
        self._length_scale = 1.1
        self._process: QProcess | None = None
        self._queue: list[_Job] = []
        self._waiting_for_path = False
        self._stdout_buffer = b""
        self._active_job = _Job()
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
            self.stopWorker()

    def isConfigured(self) -> bool:
        return (
            bool(self._executable)
            and Path(self._executable).is_file()
            and Path(self._model_onnx).is_file()
        )

    def isWorkerRunning(self) -> bool:
        return self._process is not None and self._process.state() != QProcess.ProcessState.NotRunning

    def hasPendingJobs(self) -> bool:
        return self._waiting_for_path or bool(self._queue)

    def startWorker(self) -> None:
        if not self.isConfigured() or self.isWorkerRunning():
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
            _log.warning("Piper worker failed to start: %s", self._process.errorString())
            self._process.deleteLater()
            self._process = None
        else:
            _log.info("Piper worker started %s", self._model_onnx)

    def stopWorker(self) -> None:
        self._job_timer.stop()
        self._queue.clear()
        self._waiting_for_path = False
        self._stdout_buffer = b""
        proc = self._process
        self._process = None
        if proc is None:
            return
        # Disconnect slots before killing so finished handlers don't re-enter.
        for signal in (
            proc.readyReadStandardOutput,
            proc.readyReadStandardError,
            proc.finished,
            proc.errorOccurred,
        ):
            try:
                signal.disconnect()
            except (RuntimeError, TypeError):
                pass
        if proc.state() != QProcess.ProcessState.NotRunning:
            proc.terminate()
            if not proc.waitForFinished(2000):
                proc.kill()
                proc.waitForFinished(1000)
        proc.deleteLater()

    def synthesize(self, turn_id: int, sentence_index: int, text: str) -> None:
        if not self.isConfigured() or not text.strip():
            self.synthesisFailed.emit(turn_id, sentence_index, "Piper not configured")
            return
        job = _Job(
            turnId=turn_id,
            sentenceIndex=sentence_index,
            text=text,
            outputPath=self._next_output_path(),
            queuedAtMs=QDateTime.currentMSecsSinceEpoch(),
        )
        self._queue.append(job)
        self.startWorker()
        self._pump_queue()

    def lengthScale(self) -> float:
        return self._length_scale

    def lastColdSynthMs(self) -> int:
        return self._last_cold_synth_ms

    def lastWarmSynthMs(self) -> int:
        return self._last_warm_synth_ms

    def _next_output_path(self) -> str:
        return str(
            self._output_dir
            / f"job_{QDateTime.currentMSecsSinceEpoch()}_{random.randint(0, 2**31 - 1)}.wav"
        )

    def _pump_queue(self) -> None:
        if self._waiting_for_path or not self._queue or not self._process:
            return
        self._active_job = self._queue[0]
        self._waiting_for_path = True
        self._job_started_ms = QDateTime.currentMSecsSinceEpoch()
        out_path = self._active_job.outputPath.replace("\\", "/")
        payload = json.dumps({"text": self._active_job.text, "output_file": out_path}) + "\n"
        written = self._process.write(payload.encode("utf-8"))
        if written != len(payload.encode("utf-8")):
            self._fail_front_job("Failed to write to Piper stdin")
            return
        self._job_timer.start()

    def _on_stderr_ready(self) -> None:
        if not self._process:
            return
        err = self._process.readAllStandardError().data().strip()
        if err:
            _log.warning("stderr: %s", err.decode("utf-8", errors="replace"))

    def _on_job_timeout(self) -> None:
        if self._waiting_for_path:
            _log.warning("Piper job timed out waiting for WAV path")
            self._fail_front_job("Piper synthesis timed out")

    def _on_stdout_ready(self) -> None:
        if not self._process:
            return
        self._stdout_buffer += self._process.readAllStandardOutput().data()
        while True:
            idx = self._stdout_buffer.find(b"\n")
            if idx < 0:
                break
            line = self._stdout_buffer[:idx]
            self._stdout_buffer = self._stdout_buffer[idx + 1 :]
            path = line.decode("utf-8", errors="replace").strip().strip('"')
            if path and self._waiting_for_path:
                self._complete_job(path)

    def _complete_job(self, wav_path: str) -> None:
        self._job_timer.stop()
        self._waiting_for_path = False
        if self._queue:
            self._queue.pop(0)
        synth_ms = QDateTime.currentMSecsSinceEpoch() - self._job_started_ms
        self._jobs_completed_on_worker += 1
        if self._jobs_completed_on_worker == 1:
            self._last_cold_synth_ms = synth_ms
        else:
            self._last_warm_synth_ms = synth_ms
        path = Path(wav_path)
        if not path.is_file() and Path(self._active_job.outputPath).is_file():
            path = Path(self._active_job.outputPath)
        if not path.is_file() or path.stat().st_size <= 44:
            _log.warning("Empty or unreadable WAV %s", path)
            path.unlink(missing_ok=True)
            self.synthesisFailed.emit(
                self._active_job.turnId,
                self._active_job.sentenceIndex,
                "Empty WAV from Piper",
            )
        else:
            _log.info(
                "synth ms=%s cold=%s warm=%s wav %s",
                synth_ms,
                self._last_cold_synth_ms,
                self._last_warm_synth_ms,
                path,
            )
            self.wavReady.emit(
                self._active_job.turnId,
                self._active_job.sentenceIndex,
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
        self.synthesisFailed.emit(job.turnId, job.sentenceIndex, error)
        self._pump_queue()

    def _on_process_finished(self, exit_code: int, _status) -> None:
        _log.warning("Piper worker exited %s", exit_code)
        err = ""
        if self._process:
            err = self._process.readAllStandardError().data().decode("utf-8", errors="replace")
            self._process.deleteLater()
            self._process = None
        self._stdout_buffer = b""
        if self._waiting_for_path and self._queue:
            self._fail_front_job(err or "Piper worker exited")
        self._waiting_for_path = False
        if self._queue:
            self.startWorker()
            self._pump_queue()

    def _on_process_error(self, error: QProcess.ProcessError) -> None:
        if error == QProcess.ProcessError.FailedToStart:
            self._fail_front_job("Piper failed to start")
